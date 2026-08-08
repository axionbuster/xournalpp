/*
 * Xournal++
 *
 * Screen + audio capture to a video file, driven by an external ffmpeg process.
 *
 * Xournal++ has always been able to record audio alongside a lecture. This records the picture as
 * well, so a session produces a finished video rather than a sound file that only means something
 * next to the .xopp. The encoder is ffmpeg, spawned as a child process: it already knows every
 * platform's screen grabber and every codec, and driving it over a pipe keeps a large, fast-moving
 * dependency out of the build.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>  // for function
#include <string>      // for string
#include <vector>      // for vector

#include <glib.h>  // for GPid, guint

#include "filesystem.h"  // for path

class Settings;

/**
 * One capture device as the platform grabber sees it. `index` is what actually goes into the
 * ffmpeg input specifier; `name` is what a human recognises in the preferences dialog.
 */
struct CaptureDevice {
    int index = -1;
    std::string name;

    /// True for entries ffmpeg reports as screens rather than cameras.
    bool isScreen = false;
};

/**
 * Everything the command line is built from, lifted out of Settings so that it can also be built
 * out of unsaved dialog widgets. The preferences dialog shows the exact command a recording would
 * run, and it has to be able to do that for values the user has typed but not yet applied.
 */
struct ScreenRecorderConfig {
    fs::path ffmpeg;

    int width = 1920;
    int height = 1080;
    int fps = 60;

    /// Bitrates in kbit/s.
    int videoBitrate = 6000;
    int audioBitrate = 160;
    int audioSampleRate = 48000;

    std::string videoCodec = "libx264";
    std::string audioCodec = "aac";
    std::string container = "mov";
    std::string extraArguments;

    bool captureCursor = true;

    /// Already resolved: never the "pick the first screen" sentinel.
    int videoDevice = 0;

    /// Negative means record no sound at all.
    int audioDevice = 0;
    std::string audioDeviceName;

    /// Region of the captured screen to keep, in captured pixels. Zero size means the whole screen.
    int regionX = 0;
    int regionY = 0;
    int regionWidth = 0;
    int regionHeight = 0;

    /**
     * Read the configuration out of the saved settings, resolving the screen index if it is still
     * the "first screen" sentinel -- which costs an ffmpeg run, so do not call this per frame.
     */
    static ScreenRecorderConfig fromSettings(const Settings& settings);

    /// The argument vector a recording to @p file would run.
    std::vector<std::string> buildCommandLine(const fs::path& file) const;

    /// buildCommandLine() rendered as a single shell-quoted line, for display.
    std::string describeCommandLine(const fs::path& file) const;
};

class ScreenRecorder final {
public:
    explicit ScreenRecorder(Settings& settings);
    ~ScreenRecorder();

    ScreenRecorder(const ScreenRecorder&) = delete;
    auto operator=(const ScreenRecorder&) -> ScreenRecorder& = delete;

    // ---------------------------------------------------------------------------------------
    // Recording
    // ---------------------------------------------------------------------------------------

    /**
     * Spawn ffmpeg and begin writing to @p file.
     *
     * @param file    Destination. Its extension must match the configured container; use
     *                buildOutputPath() to get one that does.
     * @param error   Filled with a message suitable for showing to the user when this returns
     *                false. Untouched on success.
     */
    bool start(const fs::path& file, std::string* error);

    /**
     * Ask ffmpeg to finish and wait for it to write out the container.
     *
     * This blocks, briefly and deliberately: the file is not playable until ffmpeg has written its
     * index, and returning before then would leave the user looking at a corrupt recording. The
     * escalation is "q" on stdin, then SIGINT, then SIGKILL, so a wedged encoder cannot hang the
     * application indefinitely.
     */
    void stop();

    bool isRecording() const;

    /// Destination of the recording in progress; empty when not recording.
    const fs::path& getFilename() const;

    /**
     * Called on the UI thread when ffmpeg exits on its own -- disk full, a bad codec name, a
     * screen-recording permission that was never granted. The recording is already over by then;
     * the callback exists so the toolbar toggle can come back up and the user can be told why.
     */
    void setUnexpectedExitCallback(std::function<void(const std::string& message)> callback);

    // ---------------------------------------------------------------------------------------
    // Configuration, exposed for the preferences dialog
    // ---------------------------------------------------------------------------------------

    /**
     * The ffmpeg binary to use: the explicitly configured one if there is one, otherwise the first
     * "ffmpeg" on PATH or in one of the usual package prefixes. Empty when nothing was found.
     */
    static fs::path resolveFfmpeg(const Settings& settings);

    /// resolveFfmpeg for an explicit override string rather than the saved one.
    static fs::path resolveFfmpeg(const std::string& configuredPath);

    /**
     * Ask ffmpeg which capture devices exist. Spawns it synchronously, so call it from a dialog,
     * not from a draw handler.
     *
     * The two lists live in separate index spaces -- that is the platform grabbers' convention,
     * not ours -- so a screen and a microphone may both be device 0.
     */
    static std::vector<CaptureDevice> listVideoDevices(const fs::path& ffmpeg);
    static std::vector<CaptureDevice> listAudioDevices(const fs::path& ffmpeg);

    /**
     * Both lists from a single ffmpeg run. The two above each spawn ffmpeg, and on the platforms
     * that enumerate at all they parse the same output, so asking for both separately pays the
     * process-start cost twice for nothing.
     */
    static void listCaptureDevices(const fs::path& ffmpeg, std::vector<CaptureDevice>& video,
                                   std::vector<CaptureDevice>& audio);

    /**
     * Timestamped destination inside the configured video folder, carrying the configured
     * container's extension. Empty when the folder is unset or missing.
     */
    fs::path buildOutputPath(std::string* error) const;

private:
    /// Reap the child, tearing down the watch and the pipes. Safe to call when not running.
    void reap();

    static void onChildExited(GPid pid, gint status, gpointer data);
    static gboolean onStderrReadable(GIOChannel* source, GIOCondition condition, gpointer data);

    Settings& settings;

    GPid pid = 0;
    bool running = false;

    int stdinFd = -1;
    GIOChannel* stderrChannel = nullptr;
    guint stderrWatch = 0;
    guint childWatch = 0;

    fs::path filename;

    /**
     * The tail of ffmpeg's stderr. ffmpeg says why it failed on the last line before exiting, and
     * by the time the exit is noticed the pipe is closed, so the reason has to be kept as it
     * arrives.
     */
    std::vector<std::string> recentErrors;

    std::function<void(const std::string&)> unexpectedExitCallback;
};
