// Table of contents
//   1. Small helpers ............ shell quoting, ffmpeg lookup
//   2. Microphone processing .... AudioFilterConfig
//   3. Command line ............. VideoRecorderConfig
//   4. Starting and stopping .... start / stop / teardown and the GLib watches
//   5. Frames ................... the render tick and the writer thread
//   6. Output paths ............. buildOutputPath

#include "VideoRecorder.h"

#include <algorithm>  // for max, min
#include <array>      // for array
#include <chrono>     // for steady_clock
#include <cmath>      // for abs, sqrt
#include <cstdio>     // for snprintf
#include <ctime>      // for localtime, time
#include <utility>    // for move

#ifndef G_OS_WIN32
#include <csignal>     // for SIGINT, SIGKILL, SIGPIPE
#include <mutex>       // for once_flag, call_once
#include <sys/wait.h>  // for waitpid, WNOHANG
#include <unistd.h>    // for pipe, dup2, write, close
#else
#include <io.h>  // for _close, _write
#define close _close
#define write _write
#endif

#ifdef ENABLE_AUDIO
#include "audio/PipedAudioSource.h"  // for PipedAudioSource
#endif
#include "control/Control.h"            // for Control
#include "control/settings/Settings.h"  // for Settings
#include "gui/CanvasFrame.h"            // for drawCurrentPage
#include "util/PathUtil.h"              // for getDataPath
#include "util/i18n.h"                  // for _, FS, _F

// ===========================================================================================
// 1. Small helpers
// ===========================================================================================

namespace {

/// How long stop() waits at each escalation step before getting less polite.
constexpr int GRACEFUL_QUIT_TIMEOUT_MS = 5000;
constexpr int INTERRUPT_TIMEOUT_MS = 2000;
constexpr int POLL_INTERVAL_MS = 20;

/// How many stderr lines to keep for the "why did it die" message.
constexpr size_t MAX_REMEMBERED_ERRORS = 12;

/**
 * The descriptor ffmpeg reads audio from inside the child process. 0 is the video pipe and 1 and 2
 * are its own output, so the first one free is 3.
 */
constexpr int CHILD_AUDIO_FD = 3;

/// Ceiling on how many duplicate frames one catch-up burst may emit, so a stall cannot spiral.
constexpr int MAX_CATCHUP_FRAMES = 4;

/**
 * Writing to a pipe whose reader has gone raises SIGPIPE, which by default ends the process. Both
 * writer threads here would rather see the error return and stop tidily, so the signal is ignored
 * once, the first time a recording starts.
 */
void ignoreBrokenPipes() {
#ifndef G_OS_WIN32
    static std::once_flag once;
    std::call_once(once, [] { std::signal(SIGPIPE, SIG_IGN); });
#endif
}

/**
 * Wrap a string so a shell would see exactly the original characters. Used only for the command
 * line shown in the preferences dialog -- the real spawn never goes through a shell.
 */
auto shellQuote(const std::string& s) -> std::string {
    const bool needsQuotes = s.empty() || s.find_first_of(" \t\n\"'\\$`*?[]();&|<>#~") != std::string::npos;
    if (!needsQuotes) {
        return s;
    }
    std::string out = "'";
    for (char c: s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

}  // namespace

auto VideoRecorder::resolveFfmpeg(const std::string& configuredPath) -> fs::path {
    if (!configuredPath.empty()) {
        return fs::path(configuredPath);
    }

    // A GUI app launched from the Dock inherits a minimal PATH, so this frequently comes up empty
    // on macOS even though ffmpeg is installed. The usual package prefixes are therefore tried by
    // hand afterwards rather than leaving the user to type an absolute path.
    if (gchar* found = g_find_program_in_path("ffmpeg"); found != nullptr) {
        fs::path path(found);
        g_free(found);
        return path;
    }

    for (const char* candidate: {"/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/usr/bin/ffmpeg",
                                 "/opt/local/bin/ffmpeg", "/snap/bin/ffmpeg"}) {
        if (fs::exists(candidate)) {
            return fs::path(candidate);
        }
    }

    return {};
}

auto VideoRecorder::resolveFfmpeg(const Settings& settings) -> fs::path {
    return resolveFfmpeg(settings.getVideoRecordingFfmpegPath());
}

// ===========================================================================================
// 2. Microphone processing
// ===========================================================================================

namespace {

/**
 * A number as ffmpeg's option parser wants to read it: a plain decimal point, whatever the locale
 * would otherwise print, and no trailing noise. std::to_string would write "-0,600000" in a French
 * locale, and ffmpeg would refuse it.
 */
auto filterNumber(double value) -> std::string {
    std::array<char, G_ASCII_DTOSTR_BUF_SIZE> buffer{};
    g_ascii_formatd(buffer.data(), static_cast<gint>(buffer.size()), "%.4g", value);
    return buffer.data();
}

/// True when a gain is close enough to 0 dB that writing the band would be a no-op.
auto isFlat(double dB) -> bool { return std::abs(dB) < 0.05; }

}  // namespace

auto AudioFilterConfig::fromSettings(const Settings& settings) -> AudioFilterConfig {
    AudioFilterConfig filters;

    filters.compressor = settings.isMicCompressorEnabled();
    filters.compressorThreshold = settings.getMicCompressorThreshold();
    filters.compressorRatio = settings.getMicCompressorRatio();
    filters.compressorAttack = settings.getMicCompressorAttack();
    filters.compressorRelease = settings.getMicCompressorRelease();
    filters.compressorOutputGain = settings.getMicCompressorOutputGain();

    filters.equalizer = settings.isMicEqualizerEnabled();
    filters.eqLow = settings.getMicEqualizerLow();
    filters.eqMid = settings.getMicEqualizerMid();
    filters.eqHigh = settings.getMicEqualizerHigh();

    filters.noiseSuppression = settings.getMicNoiseSuppression();
    filters.rnnoiseModel = settings.getMicRnnoiseModel();

    return filters;
}

auto AudioFilterConfig::resolveRnnoiseModel() const -> fs::path {
    if (!rnnoiseModel.empty() && fs::exists(fs::path(rnnoiseModel))) {
        return fs::path(rnnoiseModel);
    }

    const fs::path bundled = Util::getDataPath() / "resources" / "rnnoise" / "sh.rnnn";
    if (fs::exists(bundled)) {
        return bundled;
    }

    return {};
}

auto AudioFilterConfig::buildFilterChain() const -> std::string {
    std::vector<std::string> stages;

    if (compressor && compressorRatio > 1.0) {
        // ffmpeg reads a "dB" suffix as an amplitude, so the threshold goes in exactly as it is
        // written in the dialog. The output gain does not go through acompressor's own makeup,
        // which cannot be negative; a separate volume stage covers the whole range.
        std::string stage = "acompressor=threshold=" + filterNumber(compressorThreshold) +
                            "dB:ratio=" + filterNumber(compressorRatio) +
                            ":attack=" + filterNumber(compressorAttack) +
                            ":release=" + filterNumber(compressorRelease);
        stages.push_back(std::move(stage));
    }
    if (compressor && !isFlat(compressorOutputGain)) {
        stages.push_back("volume=" + filterNumber(compressorOutputGain) + "dB");
    }

    if (equalizer) {
        // A shelf below the low crossover, a shelf above the high one, and a broad peak spanning
        // what is left. Not the same filter shapes as a three-band equalizer built from a single
        // crossover pair, but the same three controls doing the same three things to the same
        // three parts of the spectrum, which is what a setting copied across is meant to mean.
        if (!isFlat(eqLow)) {
            stages.push_back("bass=g=" + filterNumber(eqLow) + ":f=" + filterNumber(LOW_CROSSOVER) +
                             ":width_type=q:w=0.707");
        }
        if (!isFlat(eqMid)) {
            const double centre = std::sqrt(LOW_CROSSOVER * HIGH_CROSSOVER);
            stages.push_back("equalizer=f=" + filterNumber(centre) + ":width_type=o:w=2.5:g=" + filterNumber(eqMid));
        }
        if (!isFlat(eqHigh)) {
            stages.push_back("treble=g=" + filterNumber(eqHigh) + ":f=" + filterNumber(HIGH_CROSSOVER) +
                             ":width_type=q:w=0.707");
        }
    }

    if (noiseSuppression == "rnnoise") {
        // The recurrent network wants a trained model, and unlike the rest of the chain it cannot
        // work without one. Rather than record with no suppression at all when the file is missing,
        // fall through to the spectral denoiser, which needs nothing.
        if (const fs::path model = resolveRnnoiseModel(); !model.empty()) {
            std::string path = model.string();
            // ffmpeg's filtergraph parser splits on these, so a model somewhere like
            // "/Users/me/My Models:2024/x.rnnn" has to arrive escaped.
            std::string escaped;
            for (char c: path) {
                if (c == '\\' || c == ':' || c == ',' || c == '\'' || c == '[' || c == ']') {
                    escaped += '\\';
                }
                escaped += c;
            }
            stages.push_back("arnndn=m=" + escaped);
        } else {
            stages.emplace_back("afftdn=nf=-25");
        }
    } else if (noiseSuppression == "fft") {
        stages.emplace_back("afftdn=nf=-25");
    }

    std::string chain;
    for (const std::string& stage: stages) {
        if (!chain.empty()) {
            chain += ",";
        }
        chain += stage;
    }
    return chain;
}

// ===========================================================================================
// 3. Command line
// ===========================================================================================

auto VideoRecorderConfig::fromSettings(const Settings& settings) -> VideoRecorderConfig {
    VideoRecorderConfig config;

    config.ffmpeg = VideoRecorder::resolveFfmpeg(settings);
    config.width = settings.getVideoRecordingWidth();
    config.height = settings.getVideoRecordingHeight();
    config.fps = settings.getVideoRecordingFps();
    config.videoBitrate = settings.getVideoRecordingVideoBitrate();
    config.audioBitrate = settings.getVideoRecordingAudioBitrate();
    config.videoCodec = settings.getVideoRecordingVideoCodec();
    config.audioCodec = settings.getVideoRecordingAudioCodec();
    config.container = settings.getVideoRecordingContainer();
    config.extraArguments = settings.getVideoRecordingExtraArguments();
#ifdef ENABLE_AUDIO
    config.withAudio = settings.isVideoRecordingWithAudio();
    config.audioSampleRate = static_cast<int>(settings.getAudioSampleRate());
#else
    // Nothing to record sound with in this build; the preference is left alone so that it comes
    // back if the same settings file is used by a build that has audio support.
    config.withAudio = false;
#endif
    config.audioChannels = 2;
    config.audioFilters = AudioFilterConfig::fromSettings(settings);

    return config;
}

auto VideoRecorderConfig::buildCommandLine(const fs::path& file, int audioFd) const -> std::vector<std::string> {
    std::vector<std::string> argv;

    const int frameRate = std::max(1, fps);
    const int frameWidth = std::max(2, width);
    const int frameHeight = std::max(2, height);

    argv.emplace_back(ffmpeg.empty() ? "ffmpeg" : ffmpeg.string());
    argv.emplace_back("-hide_banner");
    argv.emplace_back("-loglevel");
    argv.emplace_back("warning");
    argv.emplace_back("-y");

    // --- video in, straight off the pipe ---------------------------------------------------
    // Cairo's ARGB32 is BGRA in memory on a little-endian machine, which is what "bgra" means to
    // ffmpeg. No conversion happens on our side; handing the encoder a format it knows is cheaper
    // than doing the work twice.
    argv.emplace_back("-f");
    argv.emplace_back("rawvideo");
    argv.emplace_back("-pixel_format");
    argv.emplace_back("bgra");
    argv.emplace_back("-video_size");
    argv.emplace_back(std::to_string(frameWidth) + "x" + std::to_string(frameHeight));
    argv.emplace_back("-framerate");
    argv.emplace_back(std::to_string(frameRate));
    argv.emplace_back("-i");
    argv.emplace_back("pipe:0");

    // --- audio in, off a second descriptor -------------------------------------------------
    if (audioFd >= 0) {
        argv.emplace_back("-f");
        argv.emplace_back("f32le");
        argv.emplace_back("-ar");
        argv.emplace_back(std::to_string(std::max(8000, audioSampleRate)));
        argv.emplace_back("-ac");
        argv.emplace_back(std::to_string(std::max(1, audioChannels)));
        argv.emplace_back("-i");
        argv.emplace_back("pipe:" + std::to_string(audioFd));
    }

    // --- encoders --------------------------------------------------------------------------
    // Required by every hardware H.264 encoder and by anything that will play the file back.
    argv.emplace_back("-vf");
    argv.emplace_back("format=yuv420p");

    argv.emplace_back("-c:v");
    argv.emplace_back(videoCodec.empty() ? "libx264" : videoCodec);
    argv.emplace_back("-b:v");
    argv.emplace_back(std::to_string(std::max(1, videoBitrate)) + "k");

    if (audioFd >= 0) {
        if (const std::string chain = audioFilters.buildFilterChain(); !chain.empty()) {
            argv.emplace_back("-af");
            argv.emplace_back(chain);
        }
        argv.emplace_back("-c:a");
        argv.emplace_back(audioCodec.empty() ? "aac" : audioCodec);
        argv.emplace_back("-b:a");
        argv.emplace_back(std::to_string(std::max(1, audioBitrate)) + "k");
    } else {
        argv.emplace_back("-an");
    }

    if (container == "mp4" || container == "mov") {
        // Fragmented, so a recording lost to a crash or a flat battery is still playable up to the
        // point it stopped, and faststart so a finished one is seekable immediately.
        argv.emplace_back("-movflags");
        argv.emplace_back("+faststart+frag_keyframe+empty_moov");
    }

    // Deliberately last, so a hand-written option always wins over the ones derived from settings.
    if (!extraArguments.empty()) {
        gint count = 0;
        gchar** parts = nullptr;
        GError* error = nullptr;
        if (g_shell_parse_argv(extraArguments.c_str(), &count, &parts, &error)) {
            for (gint i = 0; i < count; i++) {
                argv.emplace_back(parts[i]);
            }
            g_strfreev(parts);
        } else {
            g_warning("VideoRecorder: ignoring unparsable extra ffmpeg arguments: %s",
                      error != nullptr ? error->message : "unknown error");
            g_clear_error(&error);
        }
    }

    argv.emplace_back(file.string());
    return argv;
}

auto VideoRecorderConfig::describeCommandLine(const fs::path& file) const -> std::string {
    std::string line;
    for (const auto& arg: buildCommandLine(file, withAudio ? CHILD_AUDIO_FD : -1)) {
        if (!line.empty()) {
            line += " ";
        }
        line += shellQuote(arg);
    }
    return line;
}

// ===========================================================================================
// 4. Starting and stopping
// ===========================================================================================

VideoRecorder::VideoRecorder(Control& control): control(control) {}

VideoRecorder::~VideoRecorder() {
    if (this->running) {
        this->stop();
    }
}

auto VideoRecorder::start(const fs::path& file, std::string* error) -> bool {
    if (this->running) {
        return false;
    }

    ignoreBrokenPipes();

    this->config = VideoRecorderConfig::fromSettings(*this->control.getSettings());
    if (this->config.ffmpeg.empty()) {
        if (error != nullptr) {
            *error = _("No ffmpeg binary was found. Install ffmpeg, or set its full path under "
                       "\"Preferences > Video Recording\".");
        }
        return false;
    }

    // Sound first: PortAudio reports the format of the stream it actually opened, and that has to
    // be on the command line before ffmpeg is spawned.
    int audioPipe[2] = {-1, -1};
#ifdef ENABLE_AUDIO
    if (this->config.withAudio) {
        this->audio = std::make_unique<xoj::audio::PipedAudioSource>(*this->control.getSettings());
        if (!this->audio->open()) {
            this->audio.reset();
            if (error != nullptr) {
                *error = _("The microphone could not be opened. Choose an input device under "
                           "\"Preferences > Audio Recording\", or turn the sound off under "
                           "\"Preferences > Video Recording\".");
            }
            return false;
        }
        this->config.audioSampleRate = this->audio->getSampleRate();
        this->config.audioChannels = this->audio->getChannels();

        if (pipe(audioPipe) != 0) {
            this->audio.reset();
            if (error != nullptr) {
                *error = _("Could not create a pipe for the sound.");
            }
            return false;
        }
    }
#endif

    const std::vector<std::string> args = config.buildCommandLine(file, audioPipe[0] >= 0 ? CHILD_AUDIO_FD : -1);
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a: args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    g_message("VideoRecorder: %s", config.describeCommandLine(file).c_str());

    gint childStdin = -1;
    gint childStderr = -1;
    GError* spawnError = nullptr;

    // The audio pipe is handed over by GLib rather than by a child-setup function of our own. That
    // matters more than it looks: the alternative, G_SPAWN_LEAVE_DESCRIPTORS_OPEN, also leaves the
    // *writing* ends of both pipes open in the child, so closing our copies at the end of a
    // recording never produces an end of file and ffmpeg waits for input that cannot arrive.
    const gint sourceFds[] = {audioPipe[0]};
    const gint targetFds[] = {CHILD_AUDIO_FD};
    const gsize fdCount = audioPipe[0] >= 0 ? 1 : 0;

    const gboolean ok = g_spawn_async_with_pipes_and_fds(
            nullptr, argv.data(), nullptr, G_SPAWN_DO_NOT_REAP_CHILD, nullptr, nullptr, -1, -1, -1, sourceFds,
            targetFds, fdCount, &this->pid, &childStdin, nullptr, &childStderr, &spawnError);
    if (!ok) {
        if (error != nullptr) {
            *error = FS(_F("Could not start ffmpeg: {1}") %
                        (spawnError != nullptr ? spawnError->message : "unknown error"));
        }
        g_clear_error(&spawnError);
        this->pid = 0;
        if (audioPipe[0] >= 0) {
            close(audioPipe[0]);
            close(audioPipe[1]);
        }
#ifdef ENABLE_AUDIO
        this->audio.reset();
#endif
        return false;
    }

    this->running = true;
    this->filename = file;
    this->videoFd = childStdin;
    this->recentErrors.clear();
    this->stopping = false;
    this->renderTimeTotal = 0;
    this->renderCount = 0;
    this->hasPending = false;
    this->pending.clear();
    this->spare.clear();
    this->frameCache.invalidate();
    this->lastFramePage = PageRef{};
    this->lastFrameGeneration = 0;
    this->lastFrameHadOverlays = false;

    this->stderrChannel = g_io_channel_unix_new(childStderr);
    g_io_channel_set_encoding(this->stderrChannel, nullptr, nullptr);
    g_io_channel_set_flags(this->stderrChannel, G_IO_FLAG_NONBLOCK, nullptr);
    this->stderrWatch = g_io_add_watch(this->stderrChannel, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP),
                                       &VideoRecorder::onStderrReadable, this);
    this->childWatch = g_child_watch_add(this->pid, &VideoRecorder::onChildExited, this);

    this->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(2, config.width),
                                               std::max(2, config.height));

    // The first frame goes in before anything else, so that a recording started on a still canvas
    // is not a stretch of nothing until the pen next moves.
    renderFrame();

#ifdef ENABLE_AUDIO
    if (audioPipe[0] >= 0) {
        close(audioPipe[0]);  // the child has its own copy now
        this->audio->startWriting(audioPipe[1]);
    }
#endif

    this->writerThread = std::thread([this] { this->writerLoop(); });

    const int intervalMs = std::max(1, 1000 / std::max(1, config.fps));
    this->renderTimer = g_timeout_add(static_cast<guint>(intervalMs), &VideoRecorder::onRenderTick, this);

    return true;
}

void VideoRecorder::stop() {
    if (!this->running) {
        return;
    }

    // Take the child watch down first: from here on the exit is expected, and letting the callback
    // fire would both reap the child behind our back and pop an error dialog for a normal stop.
    if (this->childWatch != 0) {
        g_source_remove(this->childWatch);
        this->childWatch = 0;
    }
    if (this->renderTimer != 0) {
        g_source_remove(this->renderTimer);
        this->renderTimer = 0;
    }

    // Stop the picture before the sound: the writer has to be gone before the video pipe closes,
    // and ffmpeg will not finish until both of its inputs have.
    this->stopping = true;
    this->frameReady.notify_all();
    if (this->writerThread.joinable()) {
        this->writerThread.join();
    }

    if (this->videoFd >= 0) {
        close(this->videoFd);
        this->videoFd = -1;
    }
#ifdef ENABLE_AUDIO
    if (this->audio) {
        this->audio->stop();
        this->audio.reset();
    }
#endif

    const GPid child = this->pid;

#ifndef G_OS_WIN32
    auto waitFor = [&](int timeoutMs) {
        for (int waited = 0; waited < timeoutMs; waited += POLL_INTERVAL_MS) {
            int status = 0;
            const pid_t result = waitpid(child, &status, WNOHANG);
            if (result == child || result < 0) {
                return true;
            }
            g_usleep(POLL_INTERVAL_MS * 1000);
        }
        return false;
    };

    // Both pipes are closed, so ffmpeg sees end of input and shuts down on its own; there is no
    // "q" to send, because its stdin is the video stream.
    if (!waitFor(GRACEFUL_QUIT_TIMEOUT_MS)) {
        g_warning("VideoRecorder: ffmpeg did not finish on its own, interrupting it");
        kill(child, SIGINT);
        if (!waitFor(INTERRUPT_TIMEOUT_MS)) {
            g_warning("VideoRecorder: ffmpeg ignored SIGINT, killing it -- the recording may be truncated");
            kill(child, SIGKILL);
            int status = 0;
            waitpid(child, &status, 0);
        }
    }
#endif

    g_spawn_close_pid(child);
    this->pid = 0;
    this->running = false;

    teardown();

    if (this->renderCount > 0) {
        g_message("VideoRecorder: wrote %s (%lld frames drawn, %.1f ms each on average)",
                  this->filename.string().c_str(), this->renderCount,
                  static_cast<double>(this->renderTimeTotal) / static_cast<double>(this->renderCount) / 1000.0);
    }
    this->filename.clear();
}

void VideoRecorder::teardown() {
    if (this->stderrWatch != 0) {
        g_source_remove(this->stderrWatch);
        this->stderrWatch = 0;
    }
    if (this->stderrChannel != nullptr) {
        g_io_channel_shutdown(this->stderrChannel, FALSE, nullptr);
        g_io_channel_unref(this->stderrChannel);
        this->stderrChannel = nullptr;
    }
    if (this->videoFd >= 0) {
        close(this->videoFd);
        this->videoFd = -1;
    }
    if (this->surface != nullptr) {
        cairo_surface_destroy(this->surface);
        this->surface = nullptr;
    }
    this->pending.clear();
    this->pending.shrink_to_fit();
    this->spare.clear();
    this->spare.shrink_to_fit();
    this->frameCache.invalidate();
}

void VideoRecorder::onChildExited(GPid pid, gint status, gpointer data) {
    auto* self = static_cast<VideoRecorder*>(data);

    // stop() removes this watch before it waits, so reaching here means ffmpeg gave up on its own.
    self->childWatch = 0;
    self->running = false;

    if (self->renderTimer != 0) {
        g_source_remove(self->renderTimer);
        self->renderTimer = 0;
    }
    self->stopping = true;
    self->frameReady.notify_all();
    if (self->writerThread.joinable()) {
        self->writerThread.join();
    }
#ifdef ENABLE_AUDIO
    if (self->audio) {
        self->audio->stop();
        self->audio.reset();
    }
#endif

    // Before anything else: the last thing ffmpeg wrote is the reason it stopped, and it may still
    // be sitting in the pipe unread when this fires.
    self->drainStderr();

    g_spawn_close_pid(pid);
    self->pid = 0;

    std::string message = _("The recording stopped unexpectedly.");
    if (!self->recentErrors.empty()) {
        message += "\n\n" + self->recentErrors.back();
    } else if (status != 0) {
        message += "\n\n" + FS(_F("ffmpeg exited with status {1}.") % status);
    }

    self->teardown();
    self->filename.clear();

    if (self->unexpectedExitCallback) {
        self->unexpectedExitCallback(message);
    }
}

auto VideoRecorder::onStderrReadable(GIOChannel* source, GIOCondition condition, gpointer data) -> gboolean {
    auto* self = static_cast<VideoRecorder*>(data);
    (void)source;

    self->drainStderr();

    if ((condition & G_IO_HUP) != 0) {
        self->stderrWatch = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void VideoRecorder::drainStderr() {
    if (this->stderrChannel == nullptr) {
        return;
    }

    gchar buffer[1024];
    gsize read = 0;
    while (g_io_channel_read_chars(this->stderrChannel, buffer, sizeof(buffer) - 1, &read, nullptr) ==
                   G_IO_STATUS_NORMAL &&
           read > 0) {
        buffer[read] = '\0';
        std::string text(buffer);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }
        if (text.empty()) {
            continue;
        }

        g_message("ffmpeg: %s", text.c_str());
        this->recentErrors.push_back(text);
        if (this->recentErrors.size() > MAX_REMEMBERED_ERRORS) {
            this->recentErrors.erase(this->recentErrors.begin());
        }
    }
}

auto VideoRecorder::isRecording() const -> bool { return this->running; }

auto VideoRecorder::getFilename() const -> const fs::path& { return this->filename; }

void VideoRecorder::setUnexpectedExitCallback(std::function<void(const std::string&)> callback) {
    this->unexpectedExitCallback = std::move(callback);
}

void VideoRecorder::onToolViewSettled(const PageRef& page, const xoj::view::ToolView* v) {
    if (this->running) {
        this->frameCache.drawSettled(page, v);
    }
}

// ===========================================================================================
// 5. Frames
// ===========================================================================================

auto VideoRecorder::onRenderTick(gpointer data) -> gboolean {
    auto* self = static_cast<VideoRecorder*>(data);
    if (!self->running) {
        return G_SOURCE_REMOVE;
    }
    // Every tick, unconditionally. An earlier version only redrew when something had told it the
    // canvas had changed, which is most of the time nothing at all -- but a change reaching the
    // page by a route that does not send that signal (an undo, a background change, a layer being
    // hidden) then never reached the video either, and a recording that silently stops following
    // the page is worse than one that costs a few percent of a core.
    const gint64 before = g_get_monotonic_time();
    self->renderFrame();
    self->renderTimeTotal += g_get_monotonic_time() - before;
    self->renderCount++;
    return G_SOURCE_CONTINUE;
}

void VideoRecorder::renderFrame() {
    if (this->surface == nullptr) {
        return;
    }

    cairo_t* cr = cairo_create(this->surface);
    const auto layout = xoj::canvas::drawCurrentPage(&this->control, cr, cairo_image_surface_get_width(this->surface),
                                                     cairo_image_surface_get_height(this->surface),
                                                     this->control.getSettings()->getProjectorBackgroundColor(),
                                                     &this->frameCache);
    cairo_destroy(cr);
    cairo_surface_flush(this->surface);

    // Pixel-identical to the frame already in the writer's hands? Then there is nothing to pack
    // and nothing to hand over -- the writer keeps re-sending its copy on its own clock, which is
    // what it does between frames anyway. Most of a lecture is a still page being talked about.
    const std::uint64_t generation = this->frameCache.getGeneration();
    if (!layout.overlaysDrawn && !this->lastFrameHadOverlays && layout.page == this->lastFramePage &&
        generation == this->lastFrameGeneration) {
        return;
    }
    this->lastFramePage = layout.page;
    this->lastFrameGeneration = generation;
    this->lastFrameHadOverlays = layout.overlaysDrawn;

    const int frameWidth = cairo_image_surface_get_width(this->surface);
    const int frameHeight = cairo_image_surface_get_height(this->surface);
    const int stride = cairo_image_surface_get_stride(this->surface);
    const unsigned char* pixels = cairo_image_surface_get_data(this->surface);
    if (pixels == nullptr) {
        return;
    }

    const size_t rowBytes = static_cast<size_t>(frameWidth) * 4;

    // A buffer nobody is using, if there is one. resize() then costs nothing: it is already the
    // right size, so no memory is asked for and none is zeroed before being overwritten anyway.
    std::vector<unsigned char> frame;
    {
        std::lock_guard<std::mutex> lock(this->frameMutex);
        frame = std::move(this->spare);
        this->spare.clear();
    }
    frame.resize(rowBytes * static_cast<size_t>(frameHeight));

    // Copied row by row rather than in one go: cairo pads each row out to its own alignment, and
    // rawvideo expects rows packed end to end.
    for (int y = 0; y < frameHeight; y++) {
        std::copy_n(pixels + static_cast<size_t>(y) * static_cast<size_t>(stride), rowBytes,
                    frame.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * rowBytes));
    }

    {
        std::lock_guard<std::mutex> lock(this->frameMutex);
        // A frame the writer never got to is not lost work worth keeping -- this one supersedes it
        // -- but its memory is exactly what the next frame wants.
        if (this->hasPending) {
            this->spare = std::move(this->pending);
        }
        this->pending = std::move(frame);
        this->hasPending = true;
    }
    this->frameReady.notify_one();
}

void VideoRecorder::writerLoop() {
    using clock = std::chrono::steady_clock;
    const auto started = clock::now();
    const double frameInterval = 1.0 / std::max(1, this->config.fps);

    long long emitted = 0;

    // The frame being sent, owned by this thread alone from the moment it is swapped out of
    // `pending` -- so it can go down the pipe with no lock held and nothing else touching it.
    std::vector<unsigned char> frame;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(this->frameMutex);
            // Wait either for the moment this frame is due or for a fresh one to arrive, whichever
            // comes first -- a new frame does not shorten the wait, it only replaces what will be
            // sent when the wait is over.
            const auto due = started + std::chrono::duration_cast<clock::duration>(
                                               std::chrono::duration<double>(emitted * frameInterval));
            this->frameReady.wait_until(lock, due, [this, due] { return this->stopping || clock::now() >= due; });

            if (this->stopping) {
                return;
            }
            if (this->hasPending) {
                // Swapped, not copied: the buffer this thread has finished with goes back for the
                // UI thread to draw the next frame into.
                std::swap(frame, this->pending);
                this->hasPending = false;
                this->spare = std::move(this->pending);
            }
        }

        if (frame.empty()) {
            continue;
        }

        // How many frames wall-clock time says should have gone out by now. Emitting to that
        // number, rather than one per pass, is what keeps the picture level with the sound: the
        // video track has no timestamps of its own, so its length is purely a matter of count.
        const double elapsed = std::chrono::duration<double>(clock::now() - started).count();
        const long long target = static_cast<long long>(elapsed / frameInterval) + 1;
        const long long behind = std::min<long long>(target - emitted, MAX_CATCHUP_FRAMES);

        for (long long i = 0; i < std::max<long long>(1, behind); i++) {
            const char* bytes = reinterpret_cast<const char*>(frame.data());
            size_t remaining = frame.size();
            while (remaining > 0) {
                const ssize_t written = ::write(this->videoFd, bytes, remaining);
                if (written <= 0) {
                    return;  // ffmpeg is gone; the child watch will explain why
                }
                bytes += written;
                remaining -= static_cast<size_t>(written);
            }
            emitted++;
        }
    }
}

// ===========================================================================================
// 6. Output paths
// ===========================================================================================

auto VideoRecorder::buildOutputPath(std::string* error) const -> fs::path {
    const Settings* settings = this->control.getSettings();

    fs::path folder = settings->getVideoFolder();
    if (folder.empty()) {
        // Falling back to the audio folder keeps a setup that only ever configured the audio folder
        // working, and keeps a session's sound and picture next to each other.
        folder = settings->getAudioFolder();
    }

    if (folder.empty() || !fs::is_directory(folder)) {
        if (error != nullptr) {
            *error = _("The video folder is not set or does not exist. Set it under "
                       "\"Preferences > Video Recording\".");
        }
        return {};
    }

    std::array<char, 64> buffer{};
    const time_t secs = time(nullptr);
    const tm* t = localtime(&secs);
    snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02d_%02d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    std::string container = settings->getVideoRecordingContainer();
    if (container.empty()) {
        container = "mov";
    }

    return folder / (std::string(buffer.data()) + "." + container);
}
