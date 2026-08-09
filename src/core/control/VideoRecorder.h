/*
 * Xournal++
 *
 * Records the canvas -- and only the canvas -- to a video file.
 *
 * Nothing is grabbed off the screen. Frames are drawn from the document model, exactly as the
 * projector window draws them, and handed to an ffmpeg child process over a pipe. That choice
 * decides most of this class's shape, and it is worth saying why:
 *
 *   - What lands in the file is the page, letterboxed on a plain background. Not the toolbars, not
 *     the desktop, not whatever notification happened to arrive mid-lecture, and not the second
 *     monitor.
 *   - The picture is rendered at the output resolution, so it is sharp at 1080p regardless of the
 *     window size, the zoom level, or whether the display is HiDPI.
 *   - No screen-recording permission is involved, because nothing is being recorded off the screen.
 *     The microphone is the only thing the operating system has to be asked about.
 *
 * Sound comes from the same PortAudio path the audio recorder has always used, piped to ffmpeg on
 * a second descriptor so that one process interleaves and muxes both streams. Video frames carry
 * no timestamps of their own -- rawvideo has none -- so the frame pump emits exactly as many
 * frames as wall-clock time says it should, which is what keeps the picture level with the sound
 * over a long recording.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <atomic>              // for atomic
#include <condition_variable>  // for condition_variable
#include <functional>          // for function
#include <memory>              // for unique_ptr
#include <mutex>               // for mutex
#include <string>              // for string
#include <thread>              // for thread
#include <vector>              // for vector

#include <cairo.h>
#include <glib.h>  // for GPid, guint

#include "config-features.h"  // for ENABLE_AUDIO
#include "filesystem.h"       // for path

class Control;
class Settings;

#ifdef ENABLE_AUDIO
namespace xoj::audio {
class PipedAudioSource;
}
#endif

/**
 * Everything the ffmpeg command line is built from, lifted out of Settings so that the preferences
 * dialog can also build one out of unsaved widget values and show the exact command a recording
 * would run.
 */
struct VideoRecorderConfig {
    fs::path ffmpeg;

    int width = 1920;
    int height = 1080;
    int fps = 60;

    /// Bitrates in kbit/s.
    int videoBitrate = 6000;
    int audioBitrate = 160;

    std::string videoCodec = "libx264";
    std::string audioCodec = "aac";
    std::string container = "mov";
    std::string extraArguments;

    /// Sound is recorded when true; the device and sample rate come from the audio settings.
    bool withAudio = true;
    int audioSampleRate = 44100;
    int audioChannels = 2;

    static VideoRecorderConfig fromSettings(const Settings& settings);

    /**
     * The argument vector a recording to @p file would run.
     *
     * @param audioFd  Descriptor ffmpeg will read raw audio from, or -1 for a silent recording.
     *                 Only its number goes into the command line; nothing is read here.
     */
    std::vector<std::string> buildCommandLine(const fs::path& file, int audioFd) const;

    /// buildCommandLine() rendered as a single shell-quoted line, for display.
    std::string describeCommandLine(const fs::path& file) const;
};

class VideoRecorder final {
public:
    explicit VideoRecorder(Control& control);
    ~VideoRecorder();

    VideoRecorder(const VideoRecorder&) = delete;
    auto operator=(const VideoRecorder&) -> VideoRecorder& = delete;

    // ---------------------------------------------------------------------------------------
    // Recording
    // ---------------------------------------------------------------------------------------

    /**
     * Spawn ffmpeg, open the microphone and begin writing to @p file.
     *
     * @param error  Filled with a message suitable for showing to the user when this returns
     *               false. Untouched on success.
     */
    bool start(const fs::path& file, std::string* error);

    /**
     * Finish the recording and wait for ffmpeg to write the container out.
     *
     * This blocks, briefly and deliberately: the file is not playable until ffmpeg has written its
     * index, and returning before then would leave the user looking at a corrupt recording.
     */
    void stop();

    bool isRecording() const;

    /// Destination of the recording in progress; empty when not recording.
    const fs::path& getFilename() const;

    /**
     * Called on the UI thread when ffmpeg exits on its own -- a bad codec name, a full disk. The
     * recording is over by then; the callback exists so the toolbar toggle can come back up and
     * the user can be told why.
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
     * Timestamped destination inside the configured video folder, carrying the configured
     * container's extension. Empty when the folder is unset or missing.
     */
    fs::path buildOutputPath(std::string* error) const;

private:
    /// UI thread: redraw the canvas into a frame buffer and hand it to the writer. */
    static gboolean onRenderTick(gpointer data);
    void renderFrame();

    /// Writer thread: emit frames to ffmpeg at a wall-clock-accurate rate.
    void writerLoop();

    void teardown();

    static void onChildExited(GPid pid, gint status, gpointer data);
    static gboolean onStderrReadable(GIOChannel* source, GIOCondition condition, gpointer data);
    void drainStderr();

    Control& control;

    GPid pid = 0;
    bool running = false;

    int videoFd = -1;
    GIOChannel* stderrChannel = nullptr;
    guint stderrWatch = 0;
    guint childWatch = 0;
    guint renderTimer = 0;

    fs::path filename;
    VideoRecorderConfig config;

    /// The surface the UI thread draws into, at the output resolution.
    cairo_surface_t* surface = nullptr;

    /**
     * Handoff between the UI thread and the writer. `pending` is a fully packed frame waiting to
     * be written; `last` is the one most recently written, re-sent when nothing has changed.
     */
    std::mutex frameMutex;
    std::condition_variable frameReady;
    std::vector<unsigned char> pending;
    std::vector<unsigned char> last;
    bool hasPending = false;

    std::atomic<bool> stopping{false};

    /// How long drawing frames has cost, for the line printed when a recording finishes.
    gint64 renderTimeTotal = 0;
    long long renderCount = 0;
    std::thread writerThread;

#ifdef ENABLE_AUDIO
    std::unique_ptr<xoj::audio::PipedAudioSource> audio;
#endif

    /**
     * The tail of ffmpeg's stderr. ffmpeg says why it failed on the last line before exiting, and
     * by the time the exit is noticed the pipe is closed, so the reason has to be kept as it
     * arrives.
     */
    std::vector<std::string> recentErrors;

    std::function<void(const std::string&)> unexpectedExitCallback;
};
