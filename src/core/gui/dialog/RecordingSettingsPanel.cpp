// Table of contents
//   1. Widget construction helpers .. frame / grid / row builders
//   2. The page ..................... RecordingSettingsPanel()
//   3. Devices and preview .......... reloadDevices / readConfig / updatePreview
//   4. Load and save ................ load / save

#include "RecordingSettingsPanel.h"

#include <algorithm>  // for max
#include <string>     // for string, to_string

#include "control/settings/Settings.h"  // for Settings
#include "util/Color.h"                 // for rgb_to_GdkRGBA, GdkRGBA_to_rgb
#include "util/PathUtil.h"              // for toGFilename, fromGFilename
#include "util/i18n.h"                  // for _

// ===========================================================================================
// 1. Widget construction helpers
// ===========================================================================================

namespace {

constexpr int ROW_SPACING = 6;
constexpr int COLUMN_SPACING = 12;

/// A titled frame holding a vertical box, in the same visual style as the .glade-built pages.
auto makeFrame(const char* title, GtkWidget** contentOut) -> GtkWidget* {
    GtkWidget* frame = gtk_frame_new(nullptr);

    GtkWidget* label = gtk_label_new(nullptr);
    gchar* markup = g_markup_printf_escaped("<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    gtk_frame_set_label_widget(GTK_FRAME(frame), label);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);

    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, ROW_SPACING);
    gtk_widget_set_margin_start(content, 12);
    gtk_widget_set_margin_end(content, 12);
    gtk_widget_set_margin_top(content, 6);
    gtk_widget_set_margin_bottom(content, 10);
    gtk_container_add(GTK_CONTAINER(frame), content);

    *contentOut = content;
    return frame;
}

auto makeGrid() -> GtkWidget* {
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), ROW_SPACING);
    gtk_grid_set_column_spacing(GTK_GRID(grid), COLUMN_SPACING);
    return grid;
}

/// Add "label:  widget" as the next row of a grid, and hand the widget back for convenience.
auto addRow(GtkWidget* grid, int row, const char* label, GtkWidget* widget) -> GtkWidget* {
    GtkWidget* text = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(text), 0.0F);
    gtk_grid_attach(GTK_GRID(grid), text, 0, row, 1, 1);
    gtk_widget_set_hexpand(widget, TRUE);
    gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
    return widget;
}

auto makeSpin(int min, int max, int step) -> GtkWidget* {
    GtkWidget* spin = gtk_spin_button_new_with_range(min, max, step);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
    gtk_widget_set_halign(spin, GTK_ALIGN_START);
    return spin;
}

/// A horizontal pair of spin buttons with a separator, for "width x height" style settings.
auto makePair(GtkWidget* first, const char* separator, GtkWidget* second) -> GtkWidget* {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), first, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(separator), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), second, FALSE, FALSE, 0);
    return box;
}

auto makeHint(const char* text) -> GtkWidget* {
    GtkWidget* label = gtk_label_new(nullptr);
    gchar* markup = g_markup_printf_escaped("<i>%s</i>", text);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 85);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    return label;
}

}  // namespace

// ===========================================================================================
// 2. The page
// ===========================================================================================

RecordingSettingsPanel::RecordingSettingsPanel() {
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scrolled), 500);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 450);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_vexpand(scrolled, TRUE);

    GtkWidget* column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(scrolled), column);

    // --- what a recording is -------------------------------------------------------------
    {
        GtkWidget* content = nullptr;
        GtkWidget* frame = makeFrame(_("Recording"), &content);

        gtk_box_pack_start(GTK_BOX(content),
                           makeHint(_("Recording captures the screen and the microphone into a video file. The "
                                      "encoding is done by ffmpeg, which must be installed separately.")),
                           FALSE, TRUE, 0);

        this->cbEnabled = gtk_check_button_new_with_label(_("Capture the screen when recording"));
        gtk_box_pack_start(GTK_BOX(content), this->cbEnabled, FALSE, TRUE, 0);

        this->cbKeepAudioFile =
                gtk_check_button_new_with_label(_("Also write a separate audio file, so strokes can be played back"));
        gtk_widget_set_tooltip_text(this->cbKeepAudioFile,
                                    _("Stroke playback needs an audio file of its own to point at. Turn this off to "
                                      "record only the video."));
        gtk_box_pack_start(GTK_BOX(content), this->cbKeepAudioFile, FALSE, TRUE, 0);

        GtkWidget* grid = makeGrid();
        int row = 0;

        this->fcVideoFolder = gtk_file_chooser_button_new(_("Video folder"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
        addRow(grid, row++, _("Video folder:"), this->fcVideoFolder);

        this->enFfmpegPath = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(this->enFfmpegPath), _("Found automatically"));
        addRow(grid, row++, _("ffmpeg binary:"), this->enFfmpegPath);

        gtk_box_pack_start(GTK_BOX(content), grid, FALSE, TRUE, 0);

        this->lbFfmpegStatus = makeHint("");
        gtk_box_pack_start(GTK_BOX(content), this->lbFfmpegStatus, FALSE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(column), frame, FALSE, TRUE, 0);
    }

    // --- what gets captured --------------------------------------------------------------
    {
        GtkWidget* content = nullptr;
        GtkWidget* frame = makeFrame(_("Capture"), &content);

        GtkWidget* grid = makeGrid();
        int row = 0;

        this->cbScreen = gtk_combo_box_text_new();
        addRow(grid, row++, _("Screen:"), this->cbScreen);

        this->cbMicrophone = gtk_combo_box_text_new();
        addRow(grid, row++, _("Microphone:"), this->cbMicrophone);

        this->spRegionX = makeSpin(0, 32000, 1);
        this->spRegionY = makeSpin(0, 32000, 1);
        addRow(grid, row++, _("Crop origin:"), makePair(this->spRegionX, ",", this->spRegionY));

        this->spRegionWidth = makeSpin(0, 32000, 1);
        this->spRegionHeight = makeSpin(0, 32000, 1);
        addRow(grid, row++, _("Crop size:"), makePair(this->spRegionWidth, "×", this->spRegionHeight));

        gtk_box_pack_start(GTK_BOX(content), grid, FALSE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(content),
                           makeHint(_("A crop size of 0 captures the whole screen. The crop is measured in captured "
                                      "pixels, which on a HiDPI display are more numerous than the pixels the desktop "
                                      "reports.")),
                           FALSE, TRUE, 0);

        this->cbCaptureCursor = gtk_check_button_new_with_label(_("Include the mouse pointer"));
        gtk_box_pack_start(GTK_BOX(content), this->cbCaptureCursor, FALSE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(column), frame, FALSE, TRUE, 0);
    }

    // --- how it is encoded ---------------------------------------------------------------
    {
        GtkWidget* content = nullptr;
        GtkWidget* frame = makeFrame(_("Output"), &content);

        GtkWidget* grid = makeGrid();
        int row = 0;

        this->spWidth = makeSpin(16, 7680, 2);
        this->spHeight = makeSpin(16, 4320, 2);
        addRow(grid, row++, _("Size:"), makePair(this->spWidth, "×", this->spHeight));

        this->spFps = makeSpin(1, 240, 1);
        addRow(grid, row++, _("Frame rate:"), this->spFps);

        this->spVideoBitrate = makeSpin(100, 200000, 500);
        addRow(grid, row++, _("Video bitrate (kbit/s):"), this->spVideoBitrate);

        this->spAudioBitrate = makeSpin(32, 512, 32);
        addRow(grid, row++, _("Audio bitrate (kbit/s):"), this->spAudioBitrate);

        this->spAudioSampleRate = makeSpin(8000, 192000, 1000);
        addRow(grid, row++, _("Audio sample rate (Hz):"), this->spAudioSampleRate);

        this->enVideoCodec = gtk_entry_new();
        gtk_widget_set_tooltip_text(this->enVideoCodec,
                                    _("An ffmpeg encoder name. h264_videotoolbox and h264_nvenc use the graphics "
                                      "hardware; libx264 uses the processor."));
        addRow(grid, row++, _("Video encoder:"), this->enVideoCodec);

        this->enAudioCodec = gtk_entry_new();
        addRow(grid, row++, _("Audio encoder:"), this->enAudioCodec);

        this->cbContainer = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbContainer), "mov", "QuickTime (.mov)");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbContainer), "mp4", "MPEG-4 (.mp4)");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbContainer), "mkv", "Matroska (.mkv)");
        addRow(grid, row++, _("Container:"), this->cbContainer);

        this->enExtraArguments = gtk_entry_new();
        gtk_widget_set_tooltip_text(this->enExtraArguments,
                                    _("Appended to the ffmpeg command last, so they override everything above."));
        addRow(grid, row++, _("Extra ffmpeg arguments:"), this->enExtraArguments);

        gtk_box_pack_start(GTK_BOX(content), grid, FALSE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(content), makeHint(_("The command a recording would run:")), FALSE, TRUE, 0);

        this->lbPreview = gtk_label_new("");
        gtk_label_set_selectable(GTK_LABEL(this->lbPreview), TRUE);
        gtk_label_set_line_wrap(GTK_LABEL(this->lbPreview), TRUE);
        gtk_label_set_xalign(GTK_LABEL(this->lbPreview), 0.0F);
        gtk_label_set_max_width_chars(GTK_LABEL(this->lbPreview), 70);
        gtk_widget_set_name(this->lbPreview, "recordingCommandPreview");
        gtk_box_pack_start(GTK_BOX(content), this->lbPreview, FALSE, TRUE, 0);

        GtkWidget* refresh = gtk_button_new_with_label(_("Refresh"));
        gtk_widget_set_halign(refresh, GTK_ALIGN_START);
        g_signal_connect_swapped(refresh, "clicked", G_CALLBACK(+[](RecordingSettingsPanel* self) {
                                     self->reloadDevices();
                                     self->updatePreview();
                                 }),
                                 this);
        gtk_box_pack_start(GTK_BOX(content), refresh, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(column), frame, FALSE, TRUE, 0);
    }

    // --- the projector -------------------------------------------------------------------
    {
        GtkWidget* content = nullptr;
        GtkWidget* frame = makeFrame(_("Projector window"), &content);

        gtk_box_pack_start(
                GTK_BOX(content),
                makeHint(_("The projector shows the current page on its own, with no toolbars around it, in a window "
                           "you can put on a second display or point a capture at. It remembers the display and "
                           "position it was last closed on, and opening or closing it never affects a recording.")),
                FALSE, TRUE, 0);

        this->cbProjectorKeepAbove = gtk_check_button_new_with_label(_("Keep the projector above other windows"));
        gtk_box_pack_start(GTK_BOX(content), this->cbProjectorKeepAbove, FALSE, TRUE, 0);

        this->cbProjectorOpenAtStartup = gtk_check_button_new_with_label(_("Open the projector at startup"));
        gtk_box_pack_start(GTK_BOX(content), this->cbProjectorOpenAtStartup, FALSE, TRUE, 0);

        this->cbProjectorLockAspect =
                gtk_check_button_new_with_label(_("Hold the projector to the recording's aspect ratio"));
        gtk_box_pack_start(GTK_BOX(content), this->cbProjectorLockAspect, FALSE, TRUE, 0);

        this->cbProjectorShowSafeArea = gtk_check_button_new_with_label(_("Show the caption safe area"));
        gtk_widget_set_tooltip_text(this->cbProjectorShowSafeArea,
                                    _("Marks where burnt-in captions would sit. Remember that this guide is part of "
                                      "the projector window, so it appears in a recording that includes it."));
        gtk_box_pack_start(GTK_BOX(content), this->cbProjectorShowSafeArea, FALSE, TRUE, 0);

        GtkWidget* grid = makeGrid();
        this->btProjectorBackground = gtk_color_button_new();
        gtk_widget_set_halign(this->btProjectorBackground, GTK_ALIGN_START);
        addRow(grid, 0, _("Background:"), this->btProjectorBackground);
        gtk_box_pack_start(GTK_BOX(content), grid, FALSE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(column), frame, FALSE, TRUE, 0);
    }

    // Keep the command preview honest: it is only useful if it tracks what is currently typed,
    // not what was saved. Rebuilding it reads widgets and formats a string -- no process is
    // spawned -- so doing it on every keystroke is cheap enough.
    auto onChanged = G_CALLBACK(+[](GtkWidget*, RecordingSettingsPanel* self) { self->updatePreview(); });
    for (GtkWidget* widget: {this->spWidth, this->spHeight, this->spFps, this->spVideoBitrate, this->spAudioBitrate,
                             this->spAudioSampleRate, this->spRegionX, this->spRegionY, this->spRegionWidth,
                             this->spRegionHeight}) {
        g_signal_connect(widget, "value-changed", onChanged, this);
    }
    for (GtkWidget* widget: {this->enVideoCodec, this->enAudioCodec, this->enExtraArguments}) {
        g_signal_connect(widget, "changed", onChanged, this);
    }
    for (GtkWidget* widget: {this->cbContainer, this->cbScreen, this->cbMicrophone}) {
        g_signal_connect(widget, "changed", onChanged, this);
    }
    g_signal_connect(this->cbCaptureCursor, "toggled", onChanged, this);

    gtk_widget_show_all(scrolled);
    this->panel = scrolled;
}

// ===========================================================================================
// 3. Devices and preview
// ===========================================================================================

void RecordingSettingsPanel::reloadDevices() {
    const fs::path ffmpeg =
            ScreenRecorder::resolveFfmpeg(std::string(gtk_entry_get_text(GTK_ENTRY(this->enFfmpegPath))));

    if (ffmpeg.empty()) {
        gtk_label_set_markup(GTK_LABEL(this->lbFfmpegStatus),
                             _("<i>No ffmpeg binary was found. Install ffmpeg, or type its full path above.</i>"));
    } else {
        gchar* markup = g_markup_printf_escaped("<i>Using %s</i>", ffmpeg.string().c_str());
        gtk_label_set_markup(GTK_LABEL(this->lbFfmpegStatus), markup);
        g_free(markup);
    }

    // Remember the selections across the refill, so pressing Refresh does not silently move the
    // capture to a different screen.
    const gint previousScreen = gtk_combo_box_get_active(GTK_COMBO_BOX(this->cbScreen));
    const gint previousMicrophone = gtk_combo_box_get_active(GTK_COMBO_BOX(this->cbMicrophone));

    ScreenRecorder::listCaptureDevices(ffmpeg, this->videoDevices, this->audioDevices);

    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(this->cbScreen));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbScreen), "", _("First screen found"));
    for (const CaptureDevice& device: this->videoDevices) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbScreen), "", device.name.c_str());
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(this->cbScreen), std::max(0, previousScreen));

    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(this->cbMicrophone));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbMicrophone), "", _("No sound"));
    for (const CaptureDevice& device: this->audioDevices) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbMicrophone), "", device.name.c_str());
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(this->cbMicrophone), std::max(0, previousMicrophone));
}

auto RecordingSettingsPanel::readConfig() const -> ScreenRecorderConfig {
    ScreenRecorderConfig config;

    config.ffmpeg = ScreenRecorder::resolveFfmpeg(std::string(gtk_entry_get_text(GTK_ENTRY(this->enFfmpegPath))));
    config.width = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spWidth));
    config.height = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spHeight));
    config.fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spFps));
    config.videoBitrate = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spVideoBitrate));
    config.audioBitrate = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spAudioBitrate));
    config.audioSampleRate = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spAudioSampleRate));
    config.videoCodec = gtk_entry_get_text(GTK_ENTRY(this->enVideoCodec));
    config.audioCodec = gtk_entry_get_text(GTK_ENTRY(this->enAudioCodec));
    config.extraArguments = gtk_entry_get_text(GTK_ENTRY(this->enExtraArguments));
    config.captureCursor = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbCaptureCursor));
    config.regionX = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spRegionX));
    config.regionY = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spRegionY));
    config.regionWidth = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spRegionWidth));
    config.regionHeight = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spRegionHeight));

    if (const gchar* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(this->cbContainer)); id != nullptr) {
        config.container = id;
    }

    // Row 0 of each combo is the synthetic entry, so a real device is at row - 1 of the list.
    const gint screenRow = gtk_combo_box_get_active(GTK_COMBO_BOX(this->cbScreen));
    if (screenRow > 0 && static_cast<size_t>(screenRow - 1) < this->videoDevices.size()) {
        config.videoDevice = this->videoDevices[static_cast<size_t>(screenRow - 1)].index;
    } else {
        const auto screen = std::find_if(this->videoDevices.begin(), this->videoDevices.end(),
                                         [](const CaptureDevice& d) { return d.isScreen; });
        config.videoDevice = screen != this->videoDevices.end() ? screen->index : 0;
    }

    const gint microphoneRow = gtk_combo_box_get_active(GTK_COMBO_BOX(this->cbMicrophone));
    if (microphoneRow > 0 && static_cast<size_t>(microphoneRow - 1) < this->audioDevices.size()) {
        const CaptureDevice& device = this->audioDevices[static_cast<size_t>(microphoneRow - 1)];
        config.audioDevice = device.index;
        config.audioDeviceName = device.name;
    } else {
        config.audioDevice = Settings::SCREEN_RECORDING_NO_AUDIO;
        config.audioDeviceName.clear();
    }

    return config;
}

void RecordingSettingsPanel::updatePreview() {
    const ScreenRecorderConfig config = readConfig();
    const std::string command = config.describeCommandLine("recording." + config.container);
    gchar* markup = g_markup_printf_escaped("<tt><small>%s</small></tt>", command.c_str());
    gtk_label_set_markup(GTK_LABEL(this->lbPreview), markup);
    g_free(markup);
}

// ===========================================================================================
// 4. Load and save
// ===========================================================================================

void RecordingSettingsPanel::load(const Settings& settings) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbEnabled), settings.isScreenRecordingEnabled());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbKeepAudioFile), settings.isScreenRecordingKeepAudioFile());

    // An unset video folder means "put videos beside the sound files", so show that folder rather
    // than an empty chooser the user cannot interpret.
    const fs::path folder =
            settings.getVideoFolder().empty() ? settings.getAudioFolder() : settings.getVideoFolder();
    if (!folder.empty()) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(this->fcVideoFolder),
                                            Util::toGFilename(folder).c_str());
    }

    gtk_entry_set_text(GTK_ENTRY(this->enFfmpegPath), settings.getScreenRecordingFfmpegPath().c_str());

    reloadDevices();

    // Select the remembered devices by the grabber's index, not by row: the lists can come back in
    // a different order, or shorter, when a display or a microphone has been unplugged.
    gtk_combo_box_set_active(GTK_COMBO_BOX(this->cbScreen), 0);
    if (const int wanted = settings.getScreenRecordingVideoDevice(); wanted >= 0) {
        for (size_t i = 0; i < this->videoDevices.size(); i++) {
            if (this->videoDevices[i].index == wanted) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(this->cbScreen), static_cast<gint>(i + 1));
                break;
            }
        }
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(this->cbMicrophone), 0);
    if (const int wanted = settings.getScreenRecordingAudioDevice(); wanted >= 0) {
        for (size_t i = 0; i < this->audioDevices.size(); i++) {
            if (this->audioDevices[i].index == wanted) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(this->cbMicrophone), static_cast<gint>(i + 1));
                break;
            }
        }
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbCaptureCursor), settings.isScreenRecordingCaptureCursor());

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spWidth), settings.getScreenRecordingWidth());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spHeight), settings.getScreenRecordingHeight());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spFps), settings.getScreenRecordingFps());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spVideoBitrate), settings.getScreenRecordingVideoBitrate());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spAudioBitrate), settings.getScreenRecordingAudioBitrate());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spAudioSampleRate), settings.getScreenRecordingAudioSampleRate());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spRegionX), settings.getScreenRecordingRegionX());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spRegionY), settings.getScreenRecordingRegionY());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spRegionWidth), settings.getScreenRecordingRegionWidth());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spRegionHeight), settings.getScreenRecordingRegionHeight());

    gtk_entry_set_text(GTK_ENTRY(this->enVideoCodec), settings.getScreenRecordingVideoCodec().c_str());
    gtk_entry_set_text(GTK_ENTRY(this->enAudioCodec), settings.getScreenRecordingAudioCodec().c_str());
    gtk_entry_set_text(GTK_ENTRY(this->enExtraArguments), settings.getScreenRecordingExtraArguments().c_str());

    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(this->cbContainer), settings.getScreenRecordingContainer().c_str())) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(this->cbContainer), 0);
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbProjectorKeepAbove), settings.isProjectorKeepAbove());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbProjectorOpenAtStartup),
                                 settings.isProjectorOpenAtStartup());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbProjectorLockAspect), settings.isProjectorLockAspectRatio());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbProjectorShowSafeArea), settings.isProjectorShowSafeArea());

    GdkRGBA background = Util::rgb_to_GdkRGBA(settings.getProjectorBackgroundColor());
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(this->btProjectorBackground), &background);

    updatePreview();
}

void RecordingSettingsPanel::save(Settings& settings) {
    settings.setScreenRecordingEnabled(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbEnabled)));
    settings.setScreenRecordingKeepAudioFile(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbKeepAudioFile)));

    if (gchar* folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(this->fcVideoFolder)); folder != nullptr) {
        settings.setVideoFolder(Util::fromGFilename(folder));
        g_free(folder);
    }

    settings.setScreenRecordingFfmpegPath(gtk_entry_get_text(GTK_ENTRY(this->enFfmpegPath)));

    const ScreenRecorderConfig config = readConfig();
    settings.setScreenRecordingVideoDevice(
            gtk_combo_box_get_active(GTK_COMBO_BOX(this->cbScreen)) > 0 ? config.videoDevice :
                                                                         Settings::SCREEN_RECORDING_FIRST_SCREEN);
    settings.setScreenRecordingAudioDevice(config.audioDevice);
    settings.setScreenRecordingAudioDeviceName(config.audioDeviceName);
    settings.setScreenRecordingCaptureCursor(config.captureCursor);

    settings.setScreenRecordingSize(config.width, config.height);
    settings.setScreenRecordingFps(config.fps);
    settings.setScreenRecordingVideoBitrate(config.videoBitrate);
    settings.setScreenRecordingAudioBitrate(config.audioBitrate);
    settings.setScreenRecordingAudioSampleRate(config.audioSampleRate);
    settings.setScreenRecordingVideoCodec(config.videoCodec);
    settings.setScreenRecordingAudioCodec(config.audioCodec);
    settings.setScreenRecordingContainer(config.container);
    settings.setScreenRecordingRegion(config.regionX, config.regionY, config.regionWidth, config.regionHeight);
    settings.setScreenRecordingExtraArguments(config.extraArguments);

    settings.setProjectorKeepAbove(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbProjectorKeepAbove)));
    settings.setProjectorOpenAtStartup(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbProjectorOpenAtStartup)));
    settings.setProjectorLockAspectRatio(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbProjectorLockAspect)));
    settings.setProjectorShowSafeArea(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbProjectorShowSafeArea)));

    GdkRGBA background{};
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(this->btProjectorBackground), &background);
    settings.setProjectorBackgroundColor(Util::GdkRGBA_to_rgb(background));
}
