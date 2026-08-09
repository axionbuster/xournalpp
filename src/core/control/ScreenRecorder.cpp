// Table of contents
//   1. Small helpers ................ argv conversion, shell quoting, device-list parsing
//   2. Locating ffmpeg .............. resolveFfmpeg
//   3. Enumerating capture devices .. listVideoDevices / listAudioDevices
//   4. Building the command line .... ScreenRecorderConfig
//   5. Running ffmpeg ............... start / stop / reap and the GLib watches
//   6. Output paths ................. buildOutputPath

#include "ScreenRecorder.h"

#include <algorithm>  // for find_if, max
#include <array>      // for array
#include <cstdio>     // for snprintf
#include <cstdlib>    // for strtol
#include <ctime>      // for localtime, time
#include <utility>    // for move

#ifndef G_OS_WIN32
#include <csignal>     // for SIGINT, SIGKILL
#include <sys/wait.h>  // for waitpid, WNOHANG
#include <unistd.h>    // for write, close
#else
#include <io.h>
#include <windows.h>
#endif

#include "control/settings/Settings.h"  // for Settings
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

/**
 * Run a command to completion and hand back its stderr. ffmpeg prints its device list to stderr,
 * not stdout, and exits non-zero afterwards, so neither of those is an error here.
 */
auto captureStderr(const std::vector<std::string>& args) -> std::string {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a: args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    gchar* stderrOut = nullptr;
    GError* error = nullptr;
    const gboolean ok = g_spawn_sync(nullptr, argv.data(), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr,
                                     &stderrOut, nullptr, &error);
    if (!ok) {
        g_message("ScreenRecorder: could not run %s: %s", args.empty() ? "ffmpeg" : args[0].c_str(),
                  error != nullptr ? error->message : "unknown error");
        g_clear_error(&error);
        return {};
    }

    std::string result = stderrOut != nullptr ? stderrOut : "";
    g_free(stderrOut);
    return result;
}

/**
 * Pull "[3] Capture screen 0" style entries out of an ffmpeg device listing.
 *
 * ffmpeg prints one section per device kind, each introduced by a header line, and the two
 * sections restart numbering from zero. @p sectionHeader selects which one to read; anything
 * before it, and anything after the next header, is skipped.
 */
auto parseAvFoundationDevices(const std::string& log, const char* sectionHeader) -> std::vector<CaptureDevice> {
    std::vector<CaptureDevice> devices;
    bool inSection = false;

    size_t pos = 0;
    while (pos <= log.size()) {
        const size_t eol = log.find('\n', pos);
        const std::string line = log.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? log.size() + 1 : eol + 1;

        if (line.find(sectionHeader) != std::string::npos) {
            inSection = true;
            continue;
        }
        if (inSection && line.find("devices:") != std::string::npos) {
            break;  // the next section starts here
        }
        if (!inSection) {
            continue;
        }

        // The interesting part is the LAST bracketed group: the leading ones are ffmpeg's own
        // "[AVFoundation indev @ 0x...]" log prefix.
        const size_t open = line.rfind('[');
        if (open == std::string::npos) {
            continue;
        }
        const size_t close = line.find(']', open);
        if (close == std::string::npos || close <= open + 1) {
            continue;
        }

        const std::string indexText = line.substr(open + 1, close - open - 1);
        char* end = nullptr;
        const long index = std::strtol(indexText.c_str(), &end, 10);
        if (indexText.empty() || end == nullptr || *end != '\0') {
            continue;
        }

        std::string name = line.substr(close + 1);
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
            name.erase(name.begin());
        }
        while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) {
            name.pop_back();
        }

        CaptureDevice device;
        device.index = static_cast<int>(index);
        device.name = name;
        device.isScreen = name.rfind("Capture screen", 0) == 0;
        devices.push_back(std::move(device));
    }

    return devices;
}

}  // namespace

// ===========================================================================================
// 2. Locating ffmpeg
// ===========================================================================================

ScreenRecorder::ScreenRecorder(Settings& settings): settings(settings) {}

ScreenRecorder::~ScreenRecorder() {
    if (this->running) {
        this->stop();
    }
}

auto ScreenRecorder::resolveFfmpeg(const std::string& configuredPath) -> fs::path {
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

auto ScreenRecorder::resolveFfmpeg(const Settings& settings) -> fs::path {
    return resolveFfmpeg(settings.getScreenRecordingFfmpegPath());
}

// ===========================================================================================
// 3. Enumerating capture devices
// ===========================================================================================

void ScreenRecorder::listCaptureDevices(const fs::path& ffmpeg, std::vector<CaptureDevice>& video,
                                        std::vector<CaptureDevice>& audio) {
    video.clear();
    audio.clear();

    if (ffmpeg.empty()) {
        return;
    }

#ifdef __APPLE__
    // ffmpeg is asked to open a device it cannot open; the listing is a side effect of that, which
    // is why it lands on stderr and why the non-zero exit status is expected rather than an error.
    const std::string log =
            captureStderr({ffmpeg.string(), "-hide_banner", "-f", "avfoundation", "-list_devices", "true", "-i", ""});
    video = parseAvFoundationDevices(log, "AVFoundation video devices:");
    audio = parseAvFoundationDevices(log, "AVFoundation audio devices:");
#else
    // x11grab and gdigrab do not enumerate: the display is named directly (":0.0", "desktop"), so
    // there is nothing to list and the whole desktop is what gets captured.
#endif
}

auto ScreenRecorder::listVideoDevices(const fs::path& ffmpeg) -> std::vector<CaptureDevice> {
    std::vector<CaptureDevice> video;
    std::vector<CaptureDevice> audio;
    listCaptureDevices(ffmpeg, video, audio);
    return video;
}

auto ScreenRecorder::listAudioDevices(const fs::path& ffmpeg) -> std::vector<CaptureDevice> {
    std::vector<CaptureDevice> video;
    std::vector<CaptureDevice> audio;
    listCaptureDevices(ffmpeg, video, audio);
    return audio;
}

// ===========================================================================================
// 4. Building the command line
// ===========================================================================================

auto ScreenRecorderConfig::fromSettings(const Settings& settings) -> ScreenRecorderConfig {
    ScreenRecorderConfig config;

    config.ffmpeg = ScreenRecorder::resolveFfmpeg(settings);
    config.width = settings.getScreenRecordingWidth();
    config.height = settings.getScreenRecordingHeight();
    config.fps = settings.getScreenRecordingFps();
    config.videoBitrate = settings.getScreenRecordingVideoBitrate();
    config.audioBitrate = settings.getScreenRecordingAudioBitrate();
    config.audioSampleRate = settings.getScreenRecordingAudioSampleRate();
    config.videoCodec = settings.getScreenRecordingVideoCodec();
    config.audioCodec = settings.getScreenRecordingAudioCodec();
    config.container = settings.getScreenRecordingContainer();
    config.extraArguments = settings.getScreenRecordingExtraArguments();
    config.captureCursor = settings.isScreenRecordingCaptureCursor();
    config.audioDevice = settings.getScreenRecordingAudioDevice();
    config.audioDeviceName = settings.getScreenRecordingAudioDeviceName();
    config.regionX = settings.getScreenRecordingRegionX();
    config.regionY = settings.getScreenRecordingRegionY();
    config.regionWidth = settings.getScreenRecordingRegionWidth();
    config.regionHeight = settings.getScreenRecordingRegionHeight();

    const int configured = settings.getScreenRecordingVideoDevice();
    if (configured >= 0) {
        config.videoDevice = configured;
    } else {
        // The grabber numbers cameras and screens together and puts the cameras first, so device 0
        // is usually the webcam. "First screen" therefore has to be looked up, not assumed.
        const std::vector<CaptureDevice> devices = ScreenRecorder::listVideoDevices(config.ffmpeg);
        const auto screen =
                std::find_if(devices.begin(), devices.end(), [](const CaptureDevice& d) { return d.isScreen; });
        config.videoDevice = screen != devices.end() ? screen->index : 0;
    }

    return config;
}

auto ScreenRecorderConfig::buildCommandLine(const fs::path& file) const -> std::vector<std::string> {
    std::vector<std::string> argv;

    argv.emplace_back(ffmpeg.empty() ? "ffmpeg" : ffmpeg.string());
    argv.emplace_back("-hide_banner");
    argv.emplace_back("-loglevel");
    argv.emplace_back("warning");
    argv.emplace_back("-y");

    const int frameRate = std::max(1, fps);

    // --- input ---------------------------------------------------------------------------
#ifdef __APPLE__
    // One avfoundation input for both streams, not two. AVFoundation timestamps the picture and
    // the sound off the same clock, so asking it for both is what keeps them in sync; two separate
    // inputs drift apart by however long the screen grabber took to start up.
    argv.emplace_back("-f");
    argv.emplace_back("avfoundation");
    argv.emplace_back("-capture_cursor");
    argv.emplace_back(captureCursor ? "1" : "0");
    argv.emplace_back("-framerate");
    argv.emplace_back(std::to_string(frameRate));
    // Asked for explicitly because otherwise ffmpeg requests yuv420p, is told by the device that it
    // cannot have it, and settles on uyvy422 -- which then has to be converted for the encoder on
    // every frame. nv12 is both offered by the screen input and what the hardware H.264 encoder
    // wants, so naming it removes a full-frame colour conversion at 60 fps.
    argv.emplace_back("-pixel_format");
    argv.emplace_back("nv12");
    argv.emplace_back("-i");
    argv.emplace_back(std::to_string(videoDevice) + ":" + (audioDevice < 0 ? "none" : std::to_string(audioDevice)));
#elif defined(G_OS_WIN32)
    argv.emplace_back("-f");
    argv.emplace_back("gdigrab");
    argv.emplace_back("-draw_mouse");
    argv.emplace_back(captureCursor ? "1" : "0");
    argv.emplace_back("-framerate");
    argv.emplace_back(std::to_string(frameRate));
    argv.emplace_back("-i");
    argv.emplace_back("desktop");

    if (audioDevice >= 0 && !audioDeviceName.empty()) {
        argv.emplace_back("-f");
        argv.emplace_back("dshow");
        argv.emplace_back("-i");
        argv.emplace_back("audio=" + audioDeviceName);
    }
#else
    argv.emplace_back("-f");
    argv.emplace_back("x11grab");
    argv.emplace_back("-draw_mouse");
    argv.emplace_back(captureCursor ? "1" : "0");
    argv.emplace_back("-framerate");
    argv.emplace_back(std::to_string(frameRate));
    argv.emplace_back("-i");
    {
        const gchar* display = g_getenv("DISPLAY");
        argv.emplace_back(display != nullptr && *display != '\0' ? display : ":0.0");
    }

    if (audioDevice >= 0) {
        argv.emplace_back("-f");
        argv.emplace_back("pulse");
        argv.emplace_back("-i");
        argv.emplace_back(audioDeviceName.empty() ? "default" : audioDeviceName);
    }
#endif

    // --- filters -------------------------------------------------------------------------
    std::string chain;
    if (regionWidth > 0 && regionHeight > 0) {
        chain += "crop=" + std::to_string(regionWidth) + ":" + std::to_string(regionHeight) + ":" +
                 std::to_string(std::max(0, regionX)) + ":" + std::to_string(std::max(0, regionY));
    }
    if (width > 0 && height > 0) {
        if (!chain.empty()) {
            chain += ",";
        }
        // Letterbox rather than stretch. A Retina panel is captured at its backing resolution and a
        // cropped region is whatever the user typed, so the incoming aspect ratio is frequently not
        // the output's -- distorting the picture to hide that would be the wrong answer.
        chain += "scale=" + std::to_string(width) + ":" + std::to_string(height) +
                 ":force_original_aspect_ratio=decrease:flags=bicubic";
        chain += ",pad=" + std::to_string(width) + ":" + std::to_string(height) + ":(ow-iw)/2:(oh-ih)/2";
    }
    if (!chain.empty()) {
        chain += ",";
    }
    // Required by every hardware H.264 encoder and by anything that will play the file back.
    chain += "format=yuv420p";

    argv.emplace_back("-vf");
    argv.emplace_back(chain);

    // --- encoders ------------------------------------------------------------------------
    argv.emplace_back("-r");
    argv.emplace_back(std::to_string(frameRate));

    argv.emplace_back("-c:v");
    argv.emplace_back(videoCodec.empty() ? "libx264" : videoCodec);
    argv.emplace_back("-b:v");
    argv.emplace_back(std::to_string(std::max(1, videoBitrate)) + "k");

    if (audioDevice >= 0) {
        argv.emplace_back("-c:a");
        argv.emplace_back(audioCodec.empty() ? "aac" : audioCodec);
        argv.emplace_back("-b:a");
        argv.emplace_back(std::to_string(std::max(1, audioBitrate)) + "k");
        argv.emplace_back("-ar");
        argv.emplace_back(std::to_string(std::max(8000, audioSampleRate)));
        argv.emplace_back("-ac");
        argv.emplace_back("2");
    } else {
        argv.emplace_back("-an");
    }

    if (container == "mp4" || container == "mov") {
        // Fragmented, so a recording lost to a crash or a flat battery is still playable up to the
        // point it stopped, and faststart so a finished one is seekable immediately. This is the
        // same trade OBS's "hybrid" formats make.
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
            g_warning("ScreenRecorder: ignoring unparsable extra ffmpeg arguments: %s",
                      error != nullptr ? error->message : "unknown error");
            g_clear_error(&error);
        }
    }

    argv.emplace_back(file.string());
    return argv;
}

auto ScreenRecorderConfig::describeCommandLine(const fs::path& file) const -> std::string {
    std::string line;
    for (const auto& arg: buildCommandLine(file)) {
        if (!line.empty()) {
            line += " ";
        }
        line += shellQuote(arg);
    }
    return line;
}

// ===========================================================================================
// 5. Running ffmpeg
// ===========================================================================================

auto ScreenRecorder::start(const fs::path& file, std::string* error) -> bool {
    if (this->running) {
        return false;
    }

    const ScreenRecorderConfig config = ScreenRecorderConfig::fromSettings(settings);
    if (config.ffmpeg.empty()) {
        if (error != nullptr) {
            *error = _("No ffmpeg binary was found. Install ffmpeg, or set its full path under "
                       "\"Preferences > Screen Recording\".");
        }
        return false;
    }

    const std::vector<std::string> args = config.buildCommandLine(file);
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a: args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    g_message("ScreenRecorder: %s", config.describeCommandLine(file).c_str());

    gint childStdin = -1;
    gint childStderr = -1;
    GError* spawnError = nullptr;
    const gboolean ok =
            g_spawn_async_with_pipes(nullptr, argv.data(), nullptr, static_cast<GSpawnFlags>(G_SPAWN_DO_NOT_REAP_CHILD),
                                     nullptr, nullptr, &this->pid, &childStdin, nullptr, &childStderr, &spawnError);
    if (!ok) {
        if (error != nullptr) {
            *error = FS(_F("Could not start ffmpeg: {1}") %
                        (spawnError != nullptr ? spawnError->message : "unknown error"));
        }
        g_clear_error(&spawnError);
        this->pid = 0;
        return false;
    }

    this->running = true;
    this->filename = file;
    this->stdinFd = childStdin;
    this->recentErrors.clear();

    this->stderrChannel = g_io_channel_unix_new(childStderr);
    g_io_channel_set_encoding(this->stderrChannel, nullptr, nullptr);
    g_io_channel_set_flags(this->stderrChannel, G_IO_FLAG_NONBLOCK, nullptr);
    this->stderrWatch = g_io_add_watch(this->stderrChannel, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP),
                                       &ScreenRecorder::onStderrReadable, this);

    this->childWatch = g_child_watch_add(this->pid, &ScreenRecorder::onChildExited, this);

    return true;
}

auto ScreenRecorder::onStderrReadable(GIOChannel* source, GIOCondition condition, gpointer data) -> gboolean {
    auto* self = static_cast<ScreenRecorder*>(data);

    gchar buffer[1024];
    gsize read = 0;
    while (g_io_channel_read_chars(source, buffer, sizeof(buffer) - 1, &read, nullptr) == G_IO_STATUS_NORMAL &&
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
        self->recentErrors.push_back(text);
        if (self->recentErrors.size() > MAX_REMEMBERED_ERRORS) {
            self->recentErrors.erase(self->recentErrors.begin());
        }
    }

    if ((condition & G_IO_HUP) != 0) {
        self->stderrWatch = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void ScreenRecorder::onChildExited(GPid pid, gint status, gpointer data) {
    auto* self = static_cast<ScreenRecorder*>(data);

    // stop() removes this watch before it waits, so reaching here means ffmpeg gave up on its own.
    self->childWatch = 0;
    self->running = false;
    g_spawn_close_pid(pid);
    self->pid = 0;

    std::string message = _("The screen recording stopped unexpectedly.");
    if (!self->recentErrors.empty()) {
        message += "\n\n" + self->recentErrors.back();
    } else if (status != 0) {
        message += "\n\n" + FS(_F("ffmpeg exited with status {1}.") % status);
    }

    self->reap();
    self->filename.clear();

    if (self->unexpectedExitCallback) {
        self->unexpectedExitCallback(message);
    }
}

void ScreenRecorder::reap() {
    if (this->stderrWatch != 0) {
        g_source_remove(this->stderrWatch);
        this->stderrWatch = 0;
    }
    if (this->stderrChannel != nullptr) {
        g_io_channel_shutdown(this->stderrChannel, FALSE, nullptr);
        g_io_channel_unref(this->stderrChannel);
        this->stderrChannel = nullptr;
    }
    if (this->stdinFd >= 0) {
#ifdef G_OS_WIN32
        _close(this->stdinFd);
#else
        close(this->stdinFd);
#endif
        this->stdinFd = -1;
    }
}

void ScreenRecorder::stop() {
    if (!this->running) {
        return;
    }

    // Take the watch down first: from here on the exit is expected, and letting the callback fire
    // would both reap the child behind our back and pop an error dialog for a normal stop.
    if (this->childWatch != 0) {
        g_source_remove(this->childWatch);
        this->childWatch = 0;
    }

    // "q" on stdin is ffmpeg's own request to stop cleanly; it finishes the current frame, flushes
    // the encoders and writes the container index. A signal skips all of that.
    if (this->stdinFd >= 0) {
#ifdef G_OS_WIN32
        _write(this->stdinFd, "q\n", 2);
#else
        [[maybe_unused]] const ssize_t written = write(this->stdinFd, "q\n", 2);
#endif
    }

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

    if (!waitFor(GRACEFUL_QUIT_TIMEOUT_MS)) {
        g_warning("ScreenRecorder: ffmpeg did not quit on request, interrupting it");
        kill(child, SIGINT);
        if (!waitFor(INTERRUPT_TIMEOUT_MS)) {
            g_warning("ScreenRecorder: ffmpeg ignored SIGINT, killing it -- the recording may be truncated");
            kill(child, SIGKILL);
            int status = 0;
            waitpid(child, &status, 0);
        }
    }
#else
    if (WaitForSingleObject(child, GRACEFUL_QUIT_TIMEOUT_MS) != WAIT_OBJECT_0) {
        g_warning("ScreenRecorder: ffmpeg did not quit on request, terminating it -- the recording may be truncated");
        TerminateProcess(child, 1);
        WaitForSingleObject(child, INTERRUPT_TIMEOUT_MS);
    }
#endif

    g_spawn_close_pid(child);
    this->pid = 0;
    this->running = false;

    reap();

    g_message("ScreenRecorder: wrote %s", this->filename.string().c_str());
    this->filename.clear();
}

auto ScreenRecorder::isRecording() const -> bool { return this->running; }

auto ScreenRecorder::getFilename() const -> const fs::path& { return this->filename; }

void ScreenRecorder::setUnexpectedExitCallback(std::function<void(const std::string&)> callback) {
    this->unexpectedExitCallback = std::move(callback);
}

// ===========================================================================================
// 6. Output paths
// ===========================================================================================

auto ScreenRecorder::buildOutputPath(std::string* error) const -> fs::path {
    fs::path folder = settings.getVideoFolder();
    if (folder.empty()) {
        // Falling back to the audio folder keeps a setup that only ever configured the audio
        // folder working, and keeps a session's sound and picture next to each other.
        folder = settings.getAudioFolder();
    }

    if (folder.empty() || !fs::is_directory(folder)) {
        if (error != nullptr) {
            *error = _("The video folder is not set or does not exist. Set it under "
                       "\"Preferences > Screen Recording\".");
        }
        return {};
    }

    std::array<char, 64> buffer{};
    const time_t secs = time(nullptr);
    const tm* t = localtime(&secs);
    snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02d_%02d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    std::string container = settings.getScreenRecordingContainer();
    if (container.empty()) {
        container = "mov";
    }

    return folder / (std::string(buffer.data()) + "." + container);
}
