#include "PipedAudioSource.h"

#include <algorithm>  // for for_each
#include <iterator>   // for back_inserter
#include <vector>     // for vector

#include <glib.h>
#include <unistd.h>  // for write, close

#include "audio/AudioQueue.h"           // for AudioQueue
#include "audio/PortAudioProducer.h"    // for PortAudioProducer
#include "control/settings/Settings.h"  // for Settings

namespace xoj::audio {

namespace {
/// Samples handed to write() at a time, per channel. Small enough to keep latency low.
constexpr size_t FRAMES_PER_WRITE = 512;

/// How long open() will wait for PortAudio to report the format of the stream it opened.
constexpr int FORMAT_TIMEOUT_MS = 2000;
constexpr int FORMAT_POLL_MS = 10;
}  // namespace

PipedAudioSource::PipedAudioSource(Settings& settings):
        settings(settings),
        queue(std::make_unique<AudioQueue<float>>()),
        producer(std::make_unique<PortAudioProducer>(settings, *queue)) {}

PipedAudioSource::~PipedAudioSource() { stop(); }

auto PipedAudioSource::open() -> bool {
    if (!this->producer->startRecording()) {
        return false;
    }

    for (int waited = 0; waited < FORMAT_TIMEOUT_MS; waited += FORMAT_POLL_MS) {
        auto [rate, channelCount] = this->queue->getAudioAttributes();
        if (rate > 0 && channelCount > 0) {
            this->sampleRate = static_cast<int>(rate);
            this->channels = static_cast<int>(channelCount);
            return true;
        }
        g_usleep(FORMAT_POLL_MS * 1000);
    }

    g_warning("PipedAudioSource: the input device never reported a format");
    this->producer->stopRecording();
    return false;
}

void PipedAudioSource::startWriting(int fd) {
    this->fd = fd;
    this->writerThread = std::thread([this] { this->writerLoop(); });
}

void PipedAudioSource::writerLoop() {
    const size_t bufferSize = FRAMES_PER_WRITE * static_cast<size_t>(std::max(1, this->channels));
    std::vector<float> buffer;
    buffer.reserve(bufferSize);

    auto lock = this->queue->acquire_lock();

    // Throw away everything captured while the format was being negotiated and ffmpeg was starting
    // up. Those samples are real, but they belong to a moment before the first frame was drawn, and
    // keeping them would put the whole soundtrack ahead of the picture by however long that took.
    while (!this->queue->empty()) {
        buffer.resize(0);
        this->queue->pop(std::back_inserter(buffer), bufferSize);
    }

    const auto gain = static_cast<float>(this->settings.getAudioGain());

    while (!(this->queue->hasStreamEnded() && this->queue->empty())) {
        this->queue->waitForProducer(lock);

        while (this->queue->size() > bufferSize || (this->queue->hasStreamEnded() && !this->queue->empty())) {
            buffer.resize(0);
            this->queue->pop(std::back_inserter(buffer), bufferSize);
            if (buffer.empty()) {
                break;
            }
            if (gain != 1.0F) {
                std::for_each(buffer.begin(), buffer.end(), [gain](auto& sample) { sample *= gain; });
            }

            const char* bytes = reinterpret_cast<const char*>(buffer.data());
            size_t remaining = buffer.size() * sizeof(float);
            while (remaining > 0) {
                const ssize_t written = ::write(this->fd, bytes, remaining);
                if (written <= 0) {
                    // ffmpeg is gone. Nothing useful is left to do here; the video side notices the
                    // child's exit and tells the user.
                    return;
                }
                bytes += written;
                remaining -= static_cast<size_t>(written);
            }
        }
    }
}

void PipedAudioSource::stop() {
    if (this->producer->isRecording()) {
        this->producer->stopRecording();
    }
    // Not a flag the loop watches: end-of-stream lets it drain what is still queued and finish on
    // its own, so the last fraction of a second of speech makes it into the file.
    this->queue->signalEndOfStream();

    if (this->writerThread.joinable()) {
        this->writerThread.join();
    }

    if (this->fd >= 0) {
        // Closing this is what tells ffmpeg the audio stream has ended.
        ::close(this->fd);
        this->fd = -1;
    }
}

}  // namespace xoj::audio
