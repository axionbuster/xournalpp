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

/// The whole command a default recording on this machine would run, end to end.
TEST(VideoRecorderCommandLineTest, theDefaultCommandIsHardwareEncodedAndQualityTargeted) {
    const fs::path ffmpeg = VideoRecorder::resolveFfmpeg(std::string{});
    if (ffmpeg.empty()) {
        GTEST_SKIP() << "no ffmpeg on this machine";
    }

    VideoRecorderConfig config;
    config.ffmpeg = ffmpeg;
    config.videoCodec = VideoRecorder::resolveVideoCodec(ffmpeg, "auto", 80);
    config.audioCodec = "aac_at";
    config.videoQuality = 80;
    config.keyframeInterval = 2;
    config.container = "mov";
    config.withAudio = true;
    config.audioSampleRate = 48000;
    config.audioChannels = 1;

    const std::string command = config.describeCommandLine("recording.mov");
    std::cout << command << std::endl;

    // The frame pump's own format, unconverted, all the way to the argument list.
    EXPECT_NE(command.find("-pixel_format bgra"), std::string::npos);
    // A picture asked for rather than a budget handed out.
    EXPECT_NE(command.find("-q:v 80"), std::string::npos);
    EXPECT_EQ(command.find("-b:v"), std::string::npos);
    // Two seconds of keyframes at 60 fps, not ffmpeg's twelve frames.
    EXPECT_NE(command.find("-g 120"), std::string::npos);
    // Playable by QuickTime, and recoverable if the machine dies mid-lecture.
    EXPECT_NE(command.find("+frag_keyframe"), std::string::npos);
    if (config.videoCodec.find("hevc") != std::string::npos) {
        EXPECT_NE(command.find("-tag:v hvc1"), std::string::npos);
    }
}
