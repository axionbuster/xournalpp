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
#include <cstdint>             // for uint64_t
#include <functional>          // for function
#include <memory>              // for unique_ptr
#include <mutex>               // for mutex
#include <string>              // for string
#include <thread>              // for thread
#include <vector>              // for vector

#include <cairo.h>
#include <glib.h>  // for GPid, guint

#include "gui/CanvasFrame.h"  // for FrameCache

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
 * What happens to the microphone between the capture and the encoder.
 *
 * The three things a streaming setup always puts in front of a microphone, in the order OBS runs
 * them, with OBS's names and OBS's units -- dB for levels, milliseconds for times -- so that a
 * number copied from one to the other means the same thing. ffmpeg does the work, as part of the
 * same process that is already encoding, which costs nothing measurable next to the video.
 *
 * Only the recording is processed. The separate .ogg is written by the audio recorder from the
 * unprocessed capture, so a stroke played back years from now sounds like the room did.
 */
struct AudioFilterConfig {
    bool compressor = true;
    double compressorThreshold = -18.0;  ///< dB
    double compressorRatio = 20.0;       ///< n:1
    double compressorAttack = 6.0;       ///< ms
    double compressorRelease = 60.0;     ///< ms
    double compressorOutputGain = 6.0;   ///< dB, applied after compressing

    bool equalizer = true;
    double eqLow = 0.0;    ///< dB below LOW_CROSSOVER
    double eqMid = -0.6;   ///< dB between the crossovers
    double eqHigh = 3.6;   ///< dB above HIGH_CROSSOVER

    /// "off", "rnnoise" or "fft". Anything else is read as "off".
    std::string noiseSuppression = "rnnoise";

    /// An .rnnn model for RNNoise; empty means the one shipped with the application.
    std::string rnnoiseModel;

    /**
     * Where the equalizer's bands meet, in Hz. Fixed rather than configurable, because they are
     * fixed in the three-band equalizer this mirrors, and because a band gain means nothing to
     * anyone unless the band it applies to is the one they are used to.
     */
    static constexpr double LOW_CROSSOVER = 880.0;
    static constexpr double HIGH_CROSSOVER = 5000.0;

    static AudioFilterConfig fromSettings(const Settings& settings);

    /**
     * The chain as one ffmpeg -af argument, or empty when nothing is switched on.
     *
     * Bands at 0 dB and a compressor at 1:1 are left out rather than written as no-ops, so the
     * command shown in the preferences says what is actually being done to the sound.
     */
    std::string buildFilterChain() const;

    /**
     * The .rnnn file RNNoise would load: the configured one if it names a readable file, otherwise
     * the one in the application's resources. Empty when neither exists, which is the one case
     * where the chain quietly falls back to the FFT denoiser.
     */
    fs::path resolveRnnoiseModel() const;
};

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

    /// Bitrates in kbit/s. The video one is used only when videoQuality is 0.
    int videoBitrate = 6000;
    int audioBitrate = 160;

    /**
     * Constant quality, 1 (worst) to 100 (best), or 0 to encode to videoBitrate instead.
     *
     * The scale is VideoToolbox's -q:v. Software encoders are given the -crf that corresponds to
     * it, so the number means the same picture whichever encoder is in use.
     */
    int videoQuality = 80;

    /// Seconds between keyframes. Also the most a truncated recording can lose off its tail.
    int keyframeInterval = 2;

    std::string videoCodec = "libx264";
    std::string audioCodec = "aac";
    std::string container = "mov";
    std::string extraArguments;

    /// Sound is recorded when true; the device and sample rate come from the audio settings.
    bool withAudio = true;
    int audioSampleRate = 44100;
    int audioChannels = 2;

    /// What happens to the microphone on its way into the file.
    AudioFilterConfig audioFilters;

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

    /// Drop the settled-page picture after selected elements enter or leave the document model.
    void invalidateFrameCache();

    /**
     * A finished stroke was just drawn into the main view's buffer; draw it into the kept frame
     * picture too, so it never flickers out of the recording. See FrameCache::drawSettled.
     */
    void onToolViewSettled(const PageRef& page, const xoj::view::ToolView* v);

    // ---------------------------------------------------------------------------------------
    // Health, for the frame rate indicator
    // ---------------------------------------------------------------------------------------

    /**
     * How often frames are actually being drawn, right now, in hertz. 0 when not recording.
     *
     * This is the number worth watching. Frames are drawn on the user interface thread, so a rate
     * below the configured one means that thread is too busy to keep up -- with the pen, as much as
     * with the recording -- and the encoder is being handed the same picture twice.
     */
    double getRenderRate() const;

    /**
     * How often frames are reaching the encoder, right now, in hertz. 0 when not recording.
     *
     * Steady at the configured rate by design: the writer emits on wall-clock time whether or not a
     * fresh picture arrived, which is what keeps the picture level with the sound. It falling below
     * means the pipe itself is not draining -- a busy disk, an encoder that cannot keep up.
     */
    double getOutputRate() const;

    /// The frame rate the recording in progress is aiming for. 0 when not recording.
    int getTargetRate() const;

    // ---------------------------------------------------------------------------------------
    // Configuration, exposed for the preferences dialog
    // ---------------------------------------------------------------------------------------

    /**
     * The ffmpeg binary to use: the explicitly configured one if there is one, otherwise the first
     * "ffmpeg" on PATH or in one of the usual package prefixes. Empty when nothing was found.
     */
    static fs::path resolveFfmpeg(const Settings& settings);

    /**
     * The encoder to use when the preference says "auto": the best one that actually encodes here.
     *
     * Candidates are tried hardware first, and each is tried by encoding two frames with the
     * arguments a real recording would use. Listing an encoder is not evidence it works -- a Mac
     * with no media engine lists both VideoToolbox encoders, and a machine with no NVIDIA card
     * lists both NVENC ones -- and the alternative to finding out here is finding out when
     * somebody presses record. The answer is remembered per ffmpeg binary.
     */
    static std::string detectVideoCodec(const fs::path& ffmpeg, int quality);

    /// @p configured unless it is empty or "auto", in which case detectVideoCodec() decides.
    static std::string resolveVideoCodec(const fs::path& ffmpeg, const std::string& configured, int quality);

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

    /// The page as it was last drawn, so an unchanged page is not re-rendered sixty times a second.
    xoj::canvas::FrameCache frameCache;

    /**
     * What the last published frame contained. When the picture's generation, the page and the
     * absence of overlays all match, the new frame is pixel-identical to the last one -- so it is
     * not packed or handed over at all, and the writer re-sends the previous frame on its own
     * clock. Most of a lecture is a still page; this is what makes a still page cost nothing.
     */
    PageRef lastFramePage;
    std::uint64_t lastFrameGeneration = 0;
    bool lastFrameHadOverlays = false;

    /**
     * Handoff between the UI thread and the writer. `pending` is a fully packed frame waiting to be
     * collected; `spare` is a buffer neither side needs any more. The frame being written lives on
     * the writer's own stack, so it can be sent down the pipe without the lock held -- a pipe write
     * blocks whenever ffmpeg is briefly busy, and blocking the UI thread on that would be felt.
     *
     * Buffers are swapped rather than allocated per frame. A 1080p frame is eight megabytes, and
     * asking for eight fresh megabytes sixty times a second costs the UI thread more in page faults
     * alone than drawing the frame does -- on the very thread that is meant to be following the pen.
     */
    std::mutex frameMutex;
    std::condition_variable frameReady;
    std::vector<unsigned char> pending;
    std::vector<unsigned char> spare;
    bool hasPending = false;

    std::atomic<bool> stopping{false};

    /// How long drawing frames has cost, for the line printed when a recording finishes.
    gint64 renderTimeTotal = 0;
    long long renderCount = 0;

    /// What the frame rate indicator reads. Ticked on the UI thread and on the writer thread.
    xoj::canvas::FrameRateMeter renderMeter;
    xoj::canvas::FrameRateMeter outputMeter;

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
