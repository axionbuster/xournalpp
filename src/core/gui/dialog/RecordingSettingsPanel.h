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

    GtkWidget* cbProjectorKeepAbove = nullptr;
    GtkWidget* cbProjectorOpenAtStartup = nullptr;
    GtkWidget* cbProjectorLockAspect = nullptr;
    GtkWidget* cbProjectorShowSafeArea = nullptr;
    GtkWidget* spProjectorSafeAreaHeight = nullptr;
    GtkWidget* btProjectorBackground = nullptr;
};
