/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "control/VideoRecorder.h"

namespace {

/// A config that does not consult Settings, so the defaults under test are this file's own.
auto baseConfig() -> VideoRecorderConfig {
    VideoRecorderConfig config;
    config.ffmpeg = "ffmpeg";
    config.width = 1920;
    config.height = 1080;
    config.fps = 60;
    config.container = "mov";
    config.withAudio = false;
    return config;
}

auto argvOf(const VideoRecorderConfig& config) -> std::vector<std::string> {
    return config.buildCommandLine("out.mov", -1);
}

auto has(const std::vector<std::string>& argv, const std::string& value) -> bool {
    return std::find(argv.begin(), argv.end(), value) != argv.end();
}

/// The argument following @p flag, or "" when the flag is absent or last.
auto valueOf(const std::vector<std::string>& argv, const std::string& flag) -> std::string {
    const auto it = std::find(argv.begin(), argv.end(), flag);
    if (it == argv.end() || std::next(it) == argv.end()) {
        return "";
    }
    return *std::next(it);
}

/**
 * A fake `ffmpeg -encoders` listing naming every encoder the recorder knows how to drive.
 *
 * What the real listing contains, and what the real probe then answers about each entry, depends
 * on the machine the tests happen to be running on -- a build with NVENC compiled in lists it on
 * a box with no card in the slot. The detection walk is therefore tested through its seam,
 * pickVideoCodec(), with this listing and a works-predicate the test controls.
 */
const std::string ALL_ENCODERS_LISTED =
        " V....D hevc_videotoolbox    VideoToolbox H.265 Encoder (codec hevc)\n"
        " V....D h264_videotoolbox    VideoToolbox H.264 Encoder (codec h264)\n"
        " V....D hevc_nvenc           NVIDIA NVENC hevc encoder (codec hevc)\n"
        " V....D h264_nvenc           NVIDIA NVENC H.264 encoder (codec h264)\n"
        " V....D libx265              libx265 H.265 / HEVC (codec hevc)\n"
        " V....D libx264              libx264 H.264 / AVC (codec h264)\n";

/// The configuration a default recording runs with, apart from the codec the probe would choose.
auto defaultRecordingConfig(std::string codec) -> VideoRecorderConfig {
    VideoRecorderConfig config;
    config.ffmpeg = "ffmpeg";
    config.videoCodec = std::move(codec);
    config.audioCodec = "aac_at";
    config.videoQuality = 80;
    config.keyframeInterval = 2;
    config.container = "mov";
    config.withAudio = true;
    config.audioSampleRate = 48000;
    config.audioChannels = 1;
    return config;
}

}  // namespace

/**
 * ffmpeg's own default group size is twelve frames, which at 60 fps is five whole 1080p keyframes
 * every second and was most of the size of a recording of a page nobody was writing on.
 */
TEST(VideoRecorderCommandLineTest, keyframeIntervalIsGivenInFramesNotSeconds) {
    VideoRecorderConfig config = baseConfig();
    config.keyframeInterval = 2;

    EXPECT_EQ(valueOf(argvOf(config), "-g"), "120");

    config.fps = 30;
    EXPECT_EQ(valueOf(argvOf(config), "-g"), "60");

    config.keyframeInterval = 5;
    EXPECT_EQ(valueOf(argvOf(config), "-g"), "150");
}

TEST(VideoRecorderCommandLineTest, hardwareHevcTakesBgraWithNoSoftwareConversion) {
    VideoRecorderConfig config = baseConfig();
    config.videoCodec = "hevc_videotoolbox";

    const auto argv = argvOf(config);
    EXPECT_FALSE(has(argv, "-vf")) << "hevc_videotoolbox accepts bgra; no filter should be inserted";
    EXPECT_FALSE(has(argv, "-init_hw_device"));
}

/// h264_videotoolbox does not list bgra, so the frames are uploaded rather than converted on the CPU.
TEST(VideoRecorderCommandLineTest, hardwareH264UploadsFramesInsteadOfConvertingThem) {
    VideoRecorderConfig config = baseConfig();
    config.videoCodec = "h264_videotoolbox";

    const auto argv = argvOf(config);
    EXPECT_EQ(valueOf(argv, "-vf"), "hwupload");
    EXPECT_EQ(valueOf(argv, "-init_hw_device"), "videotoolbox=vt");
    EXPECT_EQ(valueOf(argv, "-filter_hw_device"), "vt");
}

TEST(VideoRecorderCommandLineTest, softwareEncodersStillGetTheSoftwareConversion) {
    VideoRecorderConfig config = baseConfig();
    config.videoCodec = "libx264";

    const auto argv = argvOf(config);
    EXPECT_EQ(valueOf(argv, "-vf"), "format=yuv420p");
    EXPECT_FALSE(has(argv, "-init_hw_device"));
}

/// QuickTime paints nothing for HEVC under the "hev1" sample entry ffmpeg writes by default.
TEST(VideoRecorderCommandLineTest, hevcIsTaggedHvc1InMp4AndMov) {
    VideoRecorderConfig config = baseConfig();
    config.videoCodec = "hevc_videotoolbox";
    EXPECT_EQ(valueOf(argvOf(config), "-tag:v"), "hvc1");

    config.container = "mp4";
    EXPECT_EQ(valueOf(argvOf(config), "-tag:v"), "hvc1");

    config.videoCodec = "libx265";
    EXPECT_EQ(valueOf(argvOf(config), "-tag:v"), "hvc1");

    config.videoCodec = "h264_videotoolbox";
    EXPECT_FALSE(has(argvOf(config), "-tag:v")) << "H.264's avc1 is already what ffmpeg writes";
}

TEST(VideoRecorderCommandLineTest, qualityReplacesTheBitrateAndZeroBringsItBack) {
    VideoRecorderConfig config = baseConfig();
    config.videoCodec = "hevc_videotoolbox";
    config.videoQuality = 80;
    config.videoBitrate = 6000;

    auto argv = argvOf(config);
    EXPECT_EQ(valueOf(argv, "-q:v"), "80");
    EXPECT_FALSE(has(argv, "-b:v")) << "a bitrate alongside a quality is a contradiction";

    config.videoQuality = 0;
    argv = argvOf(config);
    EXPECT_EQ(valueOf(argv, "-b:v"), "6000k");
    EXPECT_FALSE(has(argv, "-q:v"));
}

/**
 * -q:v counts upwards over 0-100 and -crf counts downwards over 51-0. One number in the
 * preferences has to mean one picture, so the software encoders are handed the equivalent crf.
 */
TEST(VideoRecorderCommandLineTest, softwareEncodersGetACrfRatherThanAVideoToolboxQuality) {
    VideoRecorderConfig config = baseConfig();
    config.videoCodec = "libx264";
    config.videoQuality = 80;

    const auto argv = argvOf(config);
    EXPECT_FALSE(has(argv, "-q:v"));

    const int crf = std::stoi(valueOf(argv, "-crf"));
    EXPECT_GE(crf, 14);
    EXPECT_LE(crf, 40);

    // Better quality must not ask for a larger crf.
    config.videoQuality = 100;
    const int best = std::stoi(valueOf(argvOf(config), "-crf"));
    config.videoQuality = 20;
    const int worst = std::stoi(valueOf(argvOf(config), "-crf"));
    EXPECT_LT(best, crf);
    EXPECT_GT(worst, crf);
}

/// Hand-written arguments are appended last so that they override everything derived from settings.
TEST(VideoRecorderCommandLineTest, extraArgumentsComeAfterTheDerivedOnes) {
    VideoRecorderConfig config = baseConfig();
    config.videoCodec = "hevc_videotoolbox";
    config.extraArguments = "-q:v 42";

    const auto argv = argvOf(config);
    const auto last = std::find(argv.rbegin(), argv.rend(), "-q:v");
    ASSERT_NE(last, argv.rend());
    EXPECT_EQ(*std::prev(last), "42");
}

/**
 * The encoder chosen for the machine the tests are running on.
 *
 * This one really does spawn ffmpeg, because the thing under test is whether an encoder that
 * ffmpeg lists actually encodes -- which is not answerable without trying it. It is skipped
 * where there is no ffmpeg to ask.
 */
TEST(VideoRecorderCommandLineTest, autoDetectionPrefersHardwareAndPicksSomethingThatRuns) {
    const fs::path ffmpeg = VideoRecorder::resolveFfmpeg(std::string{});
    if (ffmpeg.empty()) {
        GTEST_SKIP() << "no ffmpeg on this machine";
    }

    const std::string chosen = VideoRecorder::detectVideoCodec(ffmpeg, 80);
    std::cout << "detected encoder: " << chosen << std::endl;
    EXPECT_FALSE(chosen.empty());

    // Whatever was chosen has to survive the same trial that chose it.
    VideoRecorderConfig config = baseConfig();
    config.videoCodec = chosen;
    EXPECT_FALSE(argvOf(config).empty());

    // "auto" resolves; anything else is passed through untouched.
    EXPECT_EQ(VideoRecorder::resolveVideoCodec(ffmpeg, "auto", 80), chosen);
    EXPECT_EQ(VideoRecorder::resolveVideoCodec(ffmpeg, "", 80), chosen);
    EXPECT_EQ(VideoRecorder::resolveVideoCodec(ffmpeg, "libx264", 80), "libx264");

#ifdef __APPLE__
    EXPECT_NE(chosen.find("videotoolbox"), std::string::npos)
            << "a Mac should be encoding on the media engine, not the processor";
#endif
}

/**
 * The walk that decides the "auto" encoder, pinned down with a fake probe.
 *
 * The real probe's verdicts depend on what is in the machine running the tests, so this goes
 * through the seam instead: a listing and a works-predicate the test controls. The ordering under
 * test is the settled policy -- hardware before software, HEVC before H.264.
 */
TEST(VideoRecorderCommandLineTest, autoDetectionWalksHardwareFirstAndSkipsWhatDoesNotEncode) {
    const auto everythingWorks = [](const std::string&) { return true; };

    // Everything listed and everything encoding: the best hardware encoder wins.
    EXPECT_EQ(VideoRecorder::pickVideoCodec(ALL_ENCODERS_LISTED, everythingWorks), "hevc_videotoolbox");

    // Listed is not working: a Mac with no media engine still lists VideoToolbox, and a machine
    // with no NVIDIA card still lists NVENC. Candidates that fail their trial are walked past.
    EXPECT_EQ(VideoRecorder::pickVideoCodec(
                      ALL_ENCODERS_LISTED,
                      [](const std::string& candidate) { return candidate.find("videotoolbox") == std::string::npos; }),
              "hevc_nvenc");
    EXPECT_EQ(VideoRecorder::pickVideoCodec(
                      ALL_ENCODERS_LISTED,
                      [](const std::string& candidate) { return candidate.rfind("lib", 0) == 0; }),
              "libx265");

    // An encoder ffmpeg was not built with is never probed at all.
    EXPECT_EQ(VideoRecorder::pickVideoCodec(" V....D libx264   libx264 H.264 / AVC (codec h264)\n",
                                            [](const std::string& candidate) {
                                                EXPECT_EQ(candidate, "libx264")
                                                        << "an unlisted encoder must not be probed";
                                                return true;
                                            }),
              "libx264");

    // Nothing listed, or nothing encoding: still libx264, because a recording that runs slowly
    // beats one that refuses to start.
    EXPECT_EQ(VideoRecorder::pickVideoCodec("", everythingWorks), "libx264");
    EXPECT_EQ(VideoRecorder::pickVideoCodec(ALL_ENCODERS_LISTED, [](const std::string&) { return false; }), "libx264");
}

/**
 * The whole command a default recording runs when the probe blessed the hardware HEVC encoder,
 * end to end. The probe's verdict is faked -- the real one answers for the machine the tests
 * happen to be running on -- but everything downstream of that verdict is the real path.
 */
TEST(VideoRecorderCommandLineTest, theDefaultCommandIsHardwareEncodedAndQualityTargeted) {
    const std::string codec =
            VideoRecorder::pickVideoCodec(ALL_ENCODERS_LISTED, [](const std::string&) { return true; });
    ASSERT_EQ(codec, "hevc_videotoolbox");

    const std::string command = defaultRecordingConfig(codec).describeCommandLine("recording.mov");

    // The frame pump's own format, unconverted, all the way to the argument list.
    EXPECT_NE(command.find("-pixel_format bgra"), std::string::npos);
    // A picture asked for rather than a budget handed out.
    EXPECT_NE(command.find("-q:v 80"), std::string::npos);
    EXPECT_EQ(command.find("-b:v"), std::string::npos);
    // Two seconds of keyframes at 60 fps, not ffmpeg's twelve frames.
    EXPECT_NE(command.find("-g 120"), std::string::npos);
    // Playable by QuickTime, and recoverable if the machine dies mid-lecture.
    EXPECT_NE(command.find("+frag_keyframe"), std::string::npos);
    EXPECT_NE(command.find("-tag:v hvc1"), std::string::npos);
}

/**
 * The same command when the probe turned every hardware encoder away. Still HEVC, still constant
 * quality -- the same 80 spelled in the units the software encoder reads -- and still tagged and
 * fragmented for QuickTime. The only thing the fallback changes is who does the arithmetic.
 */
TEST(VideoRecorderCommandLineTest, theDefaultCommandFallsBackToSoftwareHevcAtConstantQuality) {
    const std::string codec = VideoRecorder::pickVideoCodec(
            ALL_ENCODERS_LISTED, [](const std::string& candidate) { return candidate.rfind("lib", 0) == 0; });
    ASSERT_EQ(codec, "libx265");

    const std::string command = defaultRecordingConfig(codec).describeCommandLine("recording.mov");

    EXPECT_NE(command.find("-pixel_format bgra"), std::string::npos);
    EXPECT_EQ(command.find("-q:v"), std::string::npos) << "-q:v is VideoToolbox's scale, not x265's";
    EXPECT_NE(command.find("-crf 19"), std::string::npos) << "quality 80 corresponds to crf 19";
    EXPECT_EQ(command.find("-b:v"), std::string::npos);
    EXPECT_NE(command.find("-g 120"), std::string::npos);
    EXPECT_NE(command.find("+frag_keyframe"), std::string::npos);
    EXPECT_NE(command.find("-tag:v hvc1"), std::string::npos);
}
