/*
 * Xournal++
 *
 * The microphone, delivered as a stream of raw samples on a file descriptor.
 *
 * Same capture path as the audio recorder -- PortAudio into an AudioQueue -- with the file writer
 * replaced by something that writes 32-bit floats to a pipe. The video recorder hands the other
 * end of that pipe to ffmpeg, so one process muxes the picture and the sound together and there is
 * no temporary file and no second pass at the end of a recording.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>  // for unique_ptr
#include <thread>  // for thread

template <typename T>
class AudioQueue;
class PortAudioProducer;
class Settings;

namespace xoj::audio {

class PipedAudioSource final {
public:
    explicit PipedAudioSource(Settings& settings);
    ~PipedAudioSource();

    PipedAudioSource(const PipedAudioSource&) = delete;
    auto operator=(const PipedAudioSource&) -> PipedAudioSource& = delete;

    /**
     * Open the configured input device and start capturing.
     *
     * Capture has to begin before the format is known -- PortAudio reports the sample rate and
     * channel count of the stream it actually opened, not of the one that was asked for -- so this
     * comes first and getSampleRate() only means anything afterwards.
     */
    bool open();

    [[nodiscard]] int getSampleRate() const { return this->sampleRate; }
    [[nodiscard]] int getChannels() const { return this->channels; }

    /**
     * Begin writing samples to @p fd, which this object takes ownership of and closes when it
     * stops. Whatever was captured between open() and here is discarded, so that the first sample
     * written lines up with the first video frame rather than with the moment the device opened.
     */
    void startWriting(int fd);

    /// Stop capturing, flush what is left, and close the descriptor. Safe to call more than once.
    void stop();

private:
    void writerLoop();

    Settings& settings;

    std::unique_ptr<AudioQueue<float>> queue;
    std::unique_ptr<PortAudioProducer> producer;

    int fd = -1;
    int sampleRate = 0;
    int channels = 0;

    std::thread writerThread;
};

}  // namespace xoj::audio
