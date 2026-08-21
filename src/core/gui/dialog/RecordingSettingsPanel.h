/*
 * Xournal++
 *
 * Preferences page for video recording and the projector window.
 *
 * Built in code rather than from a .glade file, unlike its neighbours, because the command preview
 * has to be rebuilt from whatever is currently typed into the page rather than from what was last
 * saved.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string

#include <gtk/gtk.h>  // for GtkWidget

#include "control/VideoRecorder.h"  // for VideoRecorderConfig

class Settings;

class RecordingSettingsPanel final {
public:
    RecordingSettingsPanel();

    RecordingSettingsPanel(const RecordingSettingsPanel&) = delete;
    auto operator=(const RecordingSettingsPanel&) -> RecordingSettingsPanel& = delete;

    void load(const Settings& settings);
    void save(Settings& settings);

    GtkWidget* getPanel() const { return this->panel; }

private:
    /// Rebuild the command preview from the widgets as they currently stand.
    void updatePreview();

    /// Read the widgets into a config, so the preview matches what a recording would actually run.
    VideoRecorderConfig readConfig() const;

    GtkWidget* panel = nullptr;

    GtkWidget* cbEnabled = nullptr;
    GtkWidget* cbWithAudio = nullptr;
    GtkWidget* cbKeepAudioFile = nullptr;
    GtkWidget* cbShowPointer = nullptr;
    GtkWidget* boxPointerSize = nullptr;
    GtkWidget* spPointerSize = nullptr;
    GtkWidget* cbPointerShape = nullptr;
    GtkWidget* fcVideoFolder = nullptr;
    GtkWidget* enFfmpegPath = nullptr;
    GtkWidget* lbFfmpegStatus = nullptr;

    GtkWidget* spWidth = nullptr;
    GtkWidget* spHeight = nullptr;
    GtkWidget* spFps = nullptr;
    GtkWidget* spVideoBitrate = nullptr;
    GtkWidget* spAudioBitrate = nullptr;
    GtkWidget* enVideoCodec = nullptr;
    GtkWidget* enAudioCodec = nullptr;
    GtkWidget* cbContainer = nullptr;

    GtkWidget* enExtraArguments = nullptr;
    GtkWidget* lbPreview = nullptr;

    GtkWidget* cbCompressor = nullptr;
    GtkWidget* gridCompressor = nullptr;
    GtkWidget* spCompressorThreshold = nullptr;
    GtkWidget* spCompressorRatio = nullptr;
    GtkWidget* spCompressorAttack = nullptr;
    GtkWidget* spCompressorRelease = nullptr;
    GtkWidget* spCompressorOutputGain = nullptr;

    GtkWidget* cbEqualizer = nullptr;
    GtkWidget* gridEqualizer = nullptr;
    GtkWidget* spEqualizerLow = nullptr;
    GtkWidget* spEqualizerMid = nullptr;
    GtkWidget* spEqualizerHigh = nullptr;

    GtkWidget* cbNoiseSuppression = nullptr;
    GtkWidget* lbNoiseSuppressionStatus = nullptr;

    /**
     * The configured RNNoise model, carried from load to save. There is no widget for it: the
     * application ships a model and OBS offers no such choice either, so it stays a settings.xml
     * key for the rare case of wanting a differently trained one, and this holds it so that
     * saving the page does not silently drop it.
     */
    std::string rnnoiseModel;

    GtkWidget* cbProjectorKeepAbove = nullptr;
    GtkWidget* cbProjectorOpenAtStartup = nullptr;
    GtkWidget* cbProjectorLockAspect = nullptr;
    GtkWidget* cbProjectorShowSafeArea = nullptr;
    GtkWidget* spProjectorSafeAreaHeight = nullptr;
    GtkWidget* btProjectorBackground = nullptr;
    GtkWidget* cbShowFrameRate = nullptr;
};
