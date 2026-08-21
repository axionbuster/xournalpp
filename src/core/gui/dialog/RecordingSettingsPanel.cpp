// Table of contents
//   1. Widget construction helpers .. frame / grid / row builders
//   2. The page ..................... RecordingSettingsPanel()
//   3. The command preview .......... readConfig / updatePreview
//   4. Load and save ................ load / save

#include "RecordingSettingsPanel.h"

#include <algorithm>  // for max
#include <string>     // for string, to_string

#include "control/settings/Settings.h"       // for Settings
#include "control/settings/SettingsEnums.h"  // for PointerMarkerShape
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

auto makeDecimalSpin(double min, double max, double step, int digits) -> GtkWidget* {
    GtkWidget* spin = gtk_spin_button_new_with_range(min, max, step);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), digits);
    gtk_widget_set_halign(spin, GTK_ALIGN_START);
    return spin;
}

/// A spin button followed by its unit, so the number in the box is never ambiguous.
auto withUnit(GtkWidget* spin, const char* unit) -> GtkWidget* {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(unit), FALSE, FALSE, 0);
    return box;
}

/**
 * Tie a group of settings to the checkbox that switches them on, so the ones that currently do
 * nothing look like it.
 */
void bindSensitivity(GtkWidget* toggle, GtkWidget* dependent) {
    gtk_widget_set_sensitive(dependent, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle)));
    g_signal_connect(toggle, "toggled", G_CALLBACK(+[](GtkToggleButton* button, gpointer dependent) {
                         gtk_widget_set_sensitive(GTK_WIDGET(dependent), gtk_toggle_button_get_active(button));
                     }),
                     dependent);
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

        gtk_box_pack_start(
                GTK_BOX(content),
                makeHint(_("Recording writes a video of the page you are drawing on -- the canvas alone, drawn fresh "
                           "at the size below. Nothing is taken off the screen, so no toolbar, no other window and no "
                           "second display can appear in it, and the picture is sharp whatever the zoom level is. The "
                           "encoding is done by ffmpeg, which must be installed separately.")),
                FALSE, TRUE, 0);

        this->cbEnabled = gtk_check_button_new_with_label(_("Record a video when the record button is pressed"));
        gtk_box_pack_start(GTK_BOX(content), this->cbEnabled, FALSE, TRUE, 0);

        this->cbWithAudio = gtk_check_button_new_with_label(_("Record the microphone into the video"));
        gtk_widget_set_tooltip_text(this->cbWithAudio,
                                    _("The input device, sample rate and gain are the ones under "
                                      "\"Preferences > Audio Recording\"."));
        gtk_box_pack_start(GTK_BOX(content), this->cbWithAudio, FALSE, TRUE, 0);

        this->cbKeepAudioFile =
                gtk_check_button_new_with_label(_("Also write a separate audio file, so strokes can be played back"));
        gtk_widget_set_tooltip_text(this->cbKeepAudioFile,
                                    _("Stroke playback needs an audio file of its own to point at. Off by default: it "
                                      "is a second file beside every recording, for a feature you may not be using."));
        gtk_box_pack_start(GTK_BOX(content), this->cbKeepAudioFile, FALSE, TRUE, 0);

        this->cbShowPointer = gtk_check_button_new_with_label(_("Show where the pen is pointing"));
        gtk_widget_set_tooltip_text(
                this->cbShowPointer,
                _("The picture is drawn from the document, so the cursor on your desktop is not in it: without this, "
                  "a viewer sees ink appear with no idea where the pen was in between, and pointing at something "
                  "already written shows nothing at all. Drawn as a translucent dot in the current pen's color, so "
                  "the writing underneath still shows through it. The projector window shows it too."));
        gtk_box_pack_start(GTK_BOX(content), this->cbShowPointer, FALSE, TRUE, 0);

        // No bound worth defending at either end: a marker two pixels across is reasonable on a 4K
        // recording, one that fills half the page is reasonable when the point is emphasis, and
        // zero is the natural way to say "not this time" without hunting for the checkbox.
        this->spPointerSize = makeDecimalSpin(0.0, 100000.0, 1.0, 1);
        gtk_widget_set_tooltip_text(this->spPointerSize,
                                    _("The marker's diameter, measured on the finished video: 24 px stays 24 px "
                                      "whatever resolution is recorded and whatever size the projector window is. "
                                      "Any value at all, fractions included; 0 draws nothing."));
        this->boxPointerSize = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_start(this->boxPointerSize, 22);
        gtk_box_pack_start(GTK_BOX(this->boxPointerSize), gtk_label_new(_("Diameter:")), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(this->boxPointerSize), this->spPointerSize, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(this->boxPointerSize), gtk_label_new(_("px of the recorded frame")), FALSE, FALSE,
                           0);

        this->cbPointerShape = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbPointerShape), "disk", _("Disk (translucent)"));
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbPointerShape), "ring", _("Ring (outline only)"));
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbPointerShape), "dot", _("Dot (solid)"));
        gtk_widget_set_tooltip_text(this->cbPointerShape,
                                    _("All three are the same size and all three mark the exact tip. What they trade "
                                      "is how much of the page underneath survives: a disk keeps the writing readable "
                                      "through it, a ring hides nothing at all but is easier to lose against busy "
                                      "ink, and a dot is the most visible and the most opaque."));
        gtk_box_pack_start(GTK_BOX(this->boxPointerSize), gtk_label_new(_("Shape:")), FALSE, FALSE, 6);
        gtk_box_pack_start(GTK_BOX(this->boxPointerSize), this->cbPointerShape, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(content), this->boxPointerSize, FALSE, TRUE, 0);
        bindSensitivity(this->cbShowPointer, this->boxPointerSize);

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

        gtk_box_pack_start(GTK_BOX(column), frame, FALSE, TRUE, 0);
    }

    // --- what happens to the microphone --------------------------------------------------
    {
        GtkWidget* content = nullptr;
        GtkWidget* frame = makeFrame(_("Microphone processing"), &content);

        gtk_box_pack_start(
                GTK_BOX(content),
                makeHint(_("What happens to the microphone on its way into the video: the same three stages, in the "
                           "same order and to the same scales, that a streaming setup puts in front of one. Only the "
                           "video is processed -- the separate audio file, if you write one, keeps the untouched "
                           "capture.")),
                FALSE, TRUE, 0);

        this->cbCompressor = gtk_check_button_new_with_label(_("Compressor"));
        gtk_widget_set_tooltip_text(this->cbCompressor,
                                    _("Holds a voice at a steady level: anything louder than the threshold is turned "
                                      "down by the ratio, and the output gain makes up the difference."));
        gtk_box_pack_start(GTK_BOX(content), this->cbCompressor, FALSE, TRUE, 0);

        this->gridCompressor = makeGrid();
        gtk_widget_set_margin_start(this->gridCompressor, 22);
        int row = 0;

        this->spCompressorRatio = makeDecimalSpin(1.0, 32.0, 0.5, 1);
        addRow(this->gridCompressor, row++, _("Ratio:"), withUnit(this->spCompressorRatio, _("to 1")));

        this->spCompressorThreshold = makeDecimalSpin(-60.0, 0.0, 0.5, 1);
        addRow(this->gridCompressor, row++, _("Threshold:"), withUnit(this->spCompressorThreshold, _("dB")));

        this->spCompressorAttack = makeDecimalSpin(0.0, 500.0, 1.0, 0);
        addRow(this->gridCompressor, row++, _("Attack:"), withUnit(this->spCompressorAttack, _("ms")));

        this->spCompressorRelease = makeDecimalSpin(0.0, 2000.0, 5.0, 0);
        addRow(this->gridCompressor, row++, _("Release:"), withUnit(this->spCompressorRelease, _("ms")));

        this->spCompressorOutputGain = makeDecimalSpin(-32.0, 32.0, 0.5, 1);
        addRow(this->gridCompressor, row++, _("Output gain:"), withUnit(this->spCompressorOutputGain, _("dB")));

        gtk_box_pack_start(GTK_BOX(content), this->gridCompressor, FALSE, TRUE, 0);
        bindSensitivity(this->cbCompressor, this->gridCompressor);

        this->cbEqualizer = gtk_check_button_new_with_label(_("Equalizer"));
        gtk_widget_set_tooltip_text(this->cbEqualizer,
                                    _("Three bands, meeting at 880 Hz and 5 kHz. Cutting the middle a little and "
                                      "lifting the top is what makes speech easier to follow rather than just louder."));
        gtk_box_pack_start(GTK_BOX(content), this->cbEqualizer, FALSE, TRUE, 0);

        this->gridEqualizer = makeGrid();
        gtk_widget_set_margin_start(this->gridEqualizer, 22);
        row = 0;

        this->spEqualizerLow = makeDecimalSpin(-20.0, 20.0, 0.1, 1);
        addRow(this->gridEqualizer, row++, _("Low (below 880 Hz):"), withUnit(this->spEqualizerLow, _("dB")));

        this->spEqualizerMid = makeDecimalSpin(-20.0, 20.0, 0.1, 1);
        addRow(this->gridEqualizer, row++, _("Mid (880 Hz - 5 kHz):"), withUnit(this->spEqualizerMid, _("dB")));

        this->spEqualizerHigh = makeDecimalSpin(-20.0, 20.0, 0.1, 1);
        addRow(this->gridEqualizer, row++, _("High (above 5 kHz):"), withUnit(this->spEqualizerHigh, _("dB")));

        gtk_box_pack_start(GTK_BOX(content), this->gridEqualizer, FALSE, TRUE, 0);
        bindSensitivity(this->cbEqualizer, this->gridEqualizer);

        GtkWidget* grid = makeGrid();
        this->cbNoiseSuppression = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbNoiseSuppression), "off", _("Off"));
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbNoiseSuppression), "rnnoise",
                                  _("RNNoise (recurrent network)"));
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(this->cbNoiseSuppression), "fft", _("Spectral (FFT)"));
        gtk_widget_set_tooltip_text(this->cbNoiseSuppression,
                                    _("RNNoise was trained to tell speech from everything else, and takes out fan "
                                      "noise, hum and keyboard clatter without the underwater sound that spectral "
                                      "subtraction leaves behind."));
        addRow(grid, 0, _("Noise suppression:"), this->cbNoiseSuppression);
        gtk_box_pack_start(GTK_BOX(content), grid, FALSE, TRUE, 0);

        this->lbNoiseSuppressionStatus = makeHint("");
        gtk_box_pack_start(GTK_BOX(content), this->lbNoiseSuppressionStatus, FALSE, TRUE, 0);

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
                                    _("Shades the strip along the bottom of the page that burnt-in captions will "
                                      "cover, so nothing worth reading gets written into it. The shading belongs to "
                                      "the projector window and is never encoded into the recording -- though a "
                                      "capture wide enough to include the projector window would of course show it."));
        gtk_box_pack_start(GTK_BOX(content), this->cbProjectorShowSafeArea, FALSE, TRUE, 0);

        this->cbShowFrameRate = gtk_check_button_new_with_label(_("Show the frame rate"));
        gtk_widget_set_tooltip_text(
                this->cbShowFrameRate,
                _("Puts the frame rate in the corner of the projector window and beside the record button, the way "
                  "OBS keeps it in its status bar. While a video is being recorded it is the recording's own rate: a "
                  "figure below the one set above means the machine is not keeping up and the file is repeating "
                  "frames. Like the safe area, it is drawn on the projector's picture only and is never encoded into "
                  "the recording."));
        gtk_box_pack_start(GTK_BOX(content), this->cbShowFrameRate, FALSE, TRUE, 0);

        GtkWidget* grid = makeGrid();
        int row = 0;

        this->spProjectorSafeAreaHeight = makeSpin(0, 4320, 10);
        gtk_widget_set_tooltip_text(this->spProjectorSafeAreaHeight,
                                    _("Measured in lines of the finished video, which is how a subtitling "
                                      "requirement is usually written down. 150 against a 1080-line recording marks "
                                      "the bottom 150 lines, whatever size the projector window is."));
        GtkWidget* safeAreaBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(safeAreaBox), this->spProjectorSafeAreaHeight, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(safeAreaBox), gtk_label_new(_("px of the recorded frame")), FALSE, FALSE, 0);
        addRow(grid, row++, _("Safe area height:"), safeAreaBox);

        this->btProjectorBackground = gtk_color_button_new();
        gtk_widget_set_halign(this->btProjectorBackground, GTK_ALIGN_START);
        addRow(grid, row++, _("Background:"), this->btProjectorBackground);
        gtk_box_pack_start(GTK_BOX(content), grid, FALSE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(column), frame, FALSE, TRUE, 0);
    }

    // Keep the command preview honest: it is only useful if it tracks what is currently typed,
    // not what was saved. Rebuilding it reads widgets and formats a string -- no process is
    // spawned -- so doing it on every keystroke is cheap enough.
    auto onChanged = G_CALLBACK(+[](GtkWidget*, RecordingSettingsPanel* self) { self->updatePreview(); });
    for (GtkWidget* widget: {this->spWidth, this->spHeight, this->spFps, this->spVideoBitrate, this->spAudioBitrate,
                             this->spCompressorThreshold, this->spCompressorRatio, this->spCompressorAttack,
                             this->spCompressorRelease, this->spCompressorOutputGain, this->spEqualizerLow,
                             this->spEqualizerMid, this->spEqualizerHigh}) {
        g_signal_connect(widget, "value-changed", onChanged, this);
    }
    for (GtkWidget* widget: {this->enVideoCodec, this->enAudioCodec, this->enExtraArguments, this->enFfmpegPath}) {
        g_signal_connect(widget, "changed", onChanged, this);
    }
    for (GtkWidget* widget: {this->cbContainer, this->cbNoiseSuppression}) {
        g_signal_connect(widget, "changed", onChanged, this);
    }
    for (GtkWidget* widget: {this->cbWithAudio, this->cbCompressor, this->cbEqualizer}) {
        g_signal_connect(widget, "toggled", onChanged, this);
    }

    gtk_widget_show_all(scrolled);
    this->panel = scrolled;
}

// ===========================================================================================
// 3. The command preview
// ===========================================================================================

auto RecordingSettingsPanel::readConfig() const -> VideoRecorderConfig {
    VideoRecorderConfig config;

    config.ffmpeg = VideoRecorder::resolveFfmpeg(std::string(gtk_entry_get_text(GTK_ENTRY(this->enFfmpegPath))));
    config.width = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spWidth));
    config.height = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spHeight));
    config.fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spFps));
    config.videoBitrate = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spVideoBitrate));
    config.audioBitrate = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spAudioBitrate));
    config.videoCodec = gtk_entry_get_text(GTK_ENTRY(this->enVideoCodec));
    config.audioCodec = gtk_entry_get_text(GTK_ENTRY(this->enAudioCodec));
    config.extraArguments = gtk_entry_get_text(GTK_ENTRY(this->enExtraArguments));
    config.withAudio = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbWithAudio));

    if (const gchar* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(this->cbContainer)); id != nullptr) {
        config.container = id;
    }

    AudioFilterConfig& filters = config.audioFilters;
    filters.compressor = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbCompressor));
    filters.compressorThreshold = gtk_spin_button_get_value(GTK_SPIN_BUTTON(this->spCompressorThreshold));
    filters.compressorRatio = gtk_spin_button_get_value(GTK_SPIN_BUTTON(this->spCompressorRatio));
    filters.compressorAttack = gtk_spin_button_get_value(GTK_SPIN_BUTTON(this->spCompressorAttack));
    filters.compressorRelease = gtk_spin_button_get_value(GTK_SPIN_BUTTON(this->spCompressorRelease));
    filters.compressorOutputGain = gtk_spin_button_get_value(GTK_SPIN_BUTTON(this->spCompressorOutputGain));
    filters.equalizer = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbEqualizer));
    filters.eqLow = gtk_spin_button_get_value(GTK_SPIN_BUTTON(this->spEqualizerLow));
    filters.eqMid = gtk_spin_button_get_value(GTK_SPIN_BUTTON(this->spEqualizerMid));
    filters.eqHigh = gtk_spin_button_get_value(GTK_SPIN_BUTTON(this->spEqualizerHigh));
    if (const gchar* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(this->cbNoiseSuppression)); id != nullptr) {
        filters.noiseSuppression = id;
    }
    filters.rnnoiseModel = this->rnnoiseModel;

    return config;
}

void RecordingSettingsPanel::updatePreview() {
    const fs::path ffmpeg =
            VideoRecorder::resolveFfmpeg(std::string(gtk_entry_get_text(GTK_ENTRY(this->enFfmpegPath))));
    if (ffmpeg.empty()) {
        gtk_label_set_markup(GTK_LABEL(this->lbFfmpegStatus),
                             _("<i>No ffmpeg binary was found. Install ffmpeg, or type its full path above.</i>"));
    } else {
        gchar* status = g_markup_printf_escaped("<i>Using %s</i>", ffmpeg.string().c_str());
        gtk_label_set_markup(GTK_LABEL(this->lbFfmpegStatus), status);
        g_free(status);
    }

    const VideoRecorderConfig config = readConfig();

    // Say which model RNNoise found, because it is the one setting on this page that can silently
    // turn into something else: with no model file there is no network to run, and the chain drops
    // to the spectral denoiser rather than recording with no suppression at all.
    if (config.audioFilters.noiseSuppression != "rnnoise") {
        gtk_label_set_text(GTK_LABEL(this->lbNoiseSuppressionStatus), "");
    } else if (const fs::path model = config.audioFilters.resolveRnnoiseModel(); model.empty()) {
        gtk_label_set_markup(GTK_LABEL(this->lbNoiseSuppressionStatus),
                             _("<i>No RNNoise model was found, so the spectral denoiser is being used instead.</i>"));
    } else {
        gchar* status = g_markup_printf_escaped("<i>Model: %s</i>", model.string().c_str());
        gtk_label_set_markup(GTK_LABEL(this->lbNoiseSuppressionStatus), status);
        g_free(status);
    }

    const std::string command = config.describeCommandLine("recording." + config.container);
    gchar* markup = g_markup_printf_escaped("<tt><small>%s</small></tt>", command.c_str());
    gtk_label_set_markup(GTK_LABEL(this->lbPreview), markup);
    g_free(markup);
}

// ===========================================================================================
// 4. Load and save
// ===========================================================================================

void RecordingSettingsPanel::load(const Settings& settings) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbEnabled), settings.isVideoRecordingEnabled());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbWithAudio), settings.isVideoRecordingWithAudio());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbKeepAudioFile), settings.isVideoRecordingKeepAudioFile());

    // An unset video folder means "put videos beside the sound files", so show that folder rather
    // than an empty chooser the user cannot interpret.
    const fs::path folder =
            settings.getVideoFolder().empty() ? settings.getAudioFolder() : settings.getVideoFolder();
    if (!folder.empty()) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(this->fcVideoFolder),
                                            Util::toGFilename(folder).c_str());
    }

    gtk_entry_set_text(GTK_ENTRY(this->enFfmpegPath), settings.getVideoRecordingFfmpegPath().c_str());

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbShowPointer), settings.isVideoRecordingShowPointer());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spPointerSize), settings.getVideoRecordingPointerSize());
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(this->cbPointerShape),
                                     pointerMarkerShapeToString(settings.getVideoRecordingPointerShape()))) {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(this->cbPointerShape), "disk");
    }

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spWidth), settings.getVideoRecordingWidth());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spHeight), settings.getVideoRecordingHeight());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spFps), settings.getVideoRecordingFps());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spVideoBitrate), settings.getVideoRecordingVideoBitrate());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spAudioBitrate), settings.getVideoRecordingAudioBitrate());

    gtk_entry_set_text(GTK_ENTRY(this->enVideoCodec), settings.getVideoRecordingVideoCodec().c_str());
    gtk_entry_set_text(GTK_ENTRY(this->enAudioCodec), settings.getVideoRecordingAudioCodec().c_str());
    gtk_entry_set_text(GTK_ENTRY(this->enExtraArguments), settings.getVideoRecordingExtraArguments().c_str());

    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(this->cbContainer), settings.getVideoRecordingContainer().c_str())) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(this->cbContainer), 0);
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbCompressor), settings.isMicCompressorEnabled());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spCompressorThreshold), settings.getMicCompressorThreshold());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spCompressorRatio), settings.getMicCompressorRatio());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spCompressorAttack), settings.getMicCompressorAttack());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spCompressorRelease), settings.getMicCompressorRelease());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spCompressorOutputGain), settings.getMicCompressorOutputGain());

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbEqualizer), settings.isMicEqualizerEnabled());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spEqualizerLow), settings.getMicEqualizerLow());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spEqualizerMid), settings.getMicEqualizerMid());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spEqualizerHigh), settings.getMicEqualizerHigh());

    this->rnnoiseModel = settings.getMicRnnoiseModel();
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(this->cbNoiseSuppression),
                                     settings.getMicNoiseSuppression().c_str())) {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(this->cbNoiseSuppression), "off");
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbProjectorKeepAbove), settings.isProjectorKeepAbove());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbProjectorOpenAtStartup),
                                 settings.isProjectorOpenAtStartup());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbProjectorLockAspect), settings.isProjectorLockAspectRatio());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbProjectorShowSafeArea), settings.isProjectorShowSafeArea());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(this->spProjectorSafeAreaHeight),
                              settings.getProjectorSafeAreaHeight());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->cbShowFrameRate), settings.isShowFrameRate());

    GdkRGBA background = Util::rgb_to_GdkRGBA(settings.getProjectorBackgroundColor());
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(this->btProjectorBackground), &background);

    updatePreview();
}

void RecordingSettingsPanel::save(Settings& settings) {
    settings.setVideoRecordingEnabled(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbEnabled)));
    settings.setVideoRecordingWithAudio(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbWithAudio)));
    settings.setVideoRecordingKeepAudioFile(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbKeepAudioFile)));
    settings.setVideoRecordingShowPointer(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbShowPointer)));
    settings.setVideoRecordingPointerSize(gtk_spin_button_get_value(GTK_SPIN_BUTTON(this->spPointerSize)));
    if (const gchar* shape = gtk_combo_box_get_active_id(GTK_COMBO_BOX(this->cbPointerShape)); shape != nullptr) {
        settings.setVideoRecordingPointerShape(pointerMarkerShapeFromString(shape));
    }

    if (gchar* folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(this->fcVideoFolder)); folder != nullptr) {
        settings.setVideoFolder(Util::fromGFilename(folder));
        g_free(folder);
    }

    settings.setVideoRecordingFfmpegPath(gtk_entry_get_text(GTK_ENTRY(this->enFfmpegPath)));

    const VideoRecorderConfig config = readConfig();
    settings.setVideoRecordingSize(config.width, config.height);
    settings.setVideoRecordingFps(config.fps);
    settings.setVideoRecordingVideoBitrate(config.videoBitrate);
    settings.setVideoRecordingAudioBitrate(config.audioBitrate);
    settings.setVideoRecordingVideoCodec(config.videoCodec);
    settings.setVideoRecordingAudioCodec(config.audioCodec);
    settings.setVideoRecordingContainer(config.container);
    settings.setVideoRecordingExtraArguments(config.extraArguments);

    const AudioFilterConfig& filters = config.audioFilters;
    settings.setMicCompressorEnabled(filters.compressor);
    settings.setMicCompressorThreshold(filters.compressorThreshold);
    settings.setMicCompressorRatio(filters.compressorRatio);
    settings.setMicCompressorAttack(filters.compressorAttack);
    settings.setMicCompressorRelease(filters.compressorRelease);
    settings.setMicCompressorOutputGain(filters.compressorOutputGain);
    settings.setMicEqualizerEnabled(filters.equalizer);
    settings.setMicEqualizerLow(filters.eqLow);
    settings.setMicEqualizerMid(filters.eqMid);
    settings.setMicEqualizerHigh(filters.eqHigh);
    settings.setMicNoiseSuppression(filters.noiseSuppression);
    settings.setMicRnnoiseModel(filters.rnnoiseModel);

    settings.setProjectorKeepAbove(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbProjectorKeepAbove)));
    settings.setProjectorOpenAtStartup(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbProjectorOpenAtStartup)));
    settings.setProjectorLockAspectRatio(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbProjectorLockAspect)));
    settings.setProjectorShowSafeArea(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbProjectorShowSafeArea)));
    settings.setProjectorSafeAreaHeight(
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(this->spProjectorSafeAreaHeight)));
    settings.setShowFrameRate(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(this->cbShowFrameRate)));

    GdkRGBA background{};
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(this->btProjectorBackground), &background);
    settings.setProjectorBackgroundColor(Util::GdkRGBA_to_rgb(background));
}
