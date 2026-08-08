/*
 * Xournal++
 *
 * Preferences page for screen recording and the projector window.
 *
 * Built in code rather than from a .glade file, unlike its neighbours. Two things here cannot be
 * described statically: the capture-device lists come from asking ffmpeg what the machine has, and
 * the command preview has to be rebuilt from whatever is currently typed into the page.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <vector>  // for vector

#include <gtk/gtk.h>  // for GtkWidget

#include "control/ScreenRecorder.h"  // for CaptureDevice

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
    /// Ask ffmpeg what it can capture and refill the two device combo boxes.
    void reloadDevices();

    /// Rebuild the command preview from the widgets as they currently stand.
    void updatePreview();

    /// Read the widgets into a config, so the preview matches what a recording would actually run.
    ScreenRecorderConfig readConfig() const;

    GtkWidget* panel = nullptr;

    GtkWidget* cbEnabled = nullptr;
    GtkWidget* cbKeepAudioFile = nullptr;
    GtkWidget* fcVideoFolder = nullptr;
    GtkWidget* enFfmpegPath = nullptr;
    GtkWidget* lbFfmpegStatus = nullptr;

    GtkWidget* cbScreen = nullptr;
    GtkWidget* cbMicrophone = nullptr;
    GtkWidget* cbCaptureCursor = nullptr;

    GtkWidget* spWidth = nullptr;
    GtkWidget* spHeight = nullptr;
    GtkWidget* spFps = nullptr;
    GtkWidget* spVideoBitrate = nullptr;
    GtkWidget* spAudioBitrate = nullptr;
    GtkWidget* spAudioSampleRate = nullptr;
    GtkWidget* enVideoCodec = nullptr;
    GtkWidget* enAudioCodec = nullptr;
    GtkWidget* cbContainer = nullptr;

    GtkWidget* spRegionX = nullptr;
    GtkWidget* spRegionY = nullptr;
    GtkWidget* spRegionWidth = nullptr;
    GtkWidget* spRegionHeight = nullptr;

    GtkWidget* enExtraArguments = nullptr;
    GtkWidget* lbPreview = nullptr;

    GtkWidget* cbProjectorKeepAbove = nullptr;
    GtkWidget* cbProjectorOpenAtStartup = nullptr;
    GtkWidget* cbProjectorLockAspect = nullptr;
    GtkWidget* cbProjectorShowSafeArea = nullptr;
    GtkWidget* btProjectorBackground = nullptr;

    /**
     * The device lists behind the combo boxes, kept so a selection can be turned back into the
     * grabber's own index -- which is not the same as the row number, since row 0 is a synthetic
     * "first screen" / "no sound" entry.
     */
    std::vector<CaptureDevice> videoDevices;
    std::vector<CaptureDevice> audioDevices;
};
