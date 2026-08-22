/*
 * Xournal++
 *
 * Xournal Settings
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>     // for array
#include <cstddef>   // for size_t
#include <map>       // for map
#include <memory>    // for make_shared, shared_ptr
#include <optional>  // for optional
#include <string>    // for string, basic_string
#include <utility>   // for pair
#include <vector>    // for vector

#include <gdk/gdk.h>      // for GdkInputSource, GdkD...
#include <glib.h>         // for gchar, gboolean, gint
#include <libxml/tree.h>  // for xmlNodePtr, xmlDocPtr

#include "control/tools/StrokeStabilizerEnum.h"  // for AveragingMethod, Pre...
#include "model/Font.h"                          // for XojFont
#include "util/Color.h"                          // for Color

#include "LatexSettings.h"         // for LatexSettings
#include "PageTemplateSettings.h"  // for PageTemplateSettings
#include "RecolorParameters.h"     // for RecolorParameters
#include "SettingsEnums.h"         // for InputDeviceTypeOption
#include "ViewModes.h"             // for ViewModes
#include "config-features.h"       // for ENABLE_AUDIO
#include "filesystem.h"            // for path

#ifdef ENABLE_AUDIO
#include <portaudiocpp/PortAudioCpp.hxx>  // for PaDeviceIndex
#endif

struct Palette;

constexpr auto DEFAULT_GRID_SIZE = 14.17;
/// Overshoot of the ray/infinite line drawing types, in page units (30/72 inch, about 10.6 mm)
constexpr auto DEFAULT_EXTENDED_LINE_OVERSHOOT = 30.0;
constexpr unsigned int MAX_SPACES_FOR_TAB = 8U;
/// Number of font preset slots (see Settings::getFontPreset)
constexpr size_t FONT_PRESET_COUNT = 4;

/**
 * One font preset slot: the font and the text color that applying the preset installs.
 */
struct FontPreset {
    XojFont font;
    Color color;
};

class ButtonConfig;
class InputDevice;
class PageTemplateSettings;

class SAttribute {
public:
    SAttribute();
    SAttribute(const SAttribute& attrib);
    virtual ~SAttribute();

public:
    std::string sValue;
    int iValue{};
    double dValue{};

    AttributeType type;

    std::string comment;
};


class SElement final {
    struct SElementData {
    private:
        std::map<std::string, SAttribute> attributes;
        std::map<std::string, SElement> children;
        friend class SElement;
    };

public:
    SElement() = default;

    void clear();

    SElement& child(const std::string& name);

    void setIntHex(const std::string& name, const int value);
    void setInt(const std::string& name, const int value);
    void setDouble(const std::string& name, const double value);
    void setBool(const std::string& name, const bool value);
    void setString(const std::string& name, const std::string& value);

    [[maybe_unused]] void setComment(const std::string& name, const std::string& comment);

    bool getInt(const std::string& name, int& value);
    [[maybe_unused]] bool getDouble(const std::string& name, double& value);
    bool getBool(const std::string& name, bool& value);
    bool getString(const std::string& name, std::string& value);

    std::map<std::string, SAttribute>& attributes();
    std::map<std::string, SElement>& children();

private:
    std::shared_ptr<SElementData> element = std::make_shared<SElementData>();
};

class Settings {
public:
    /*[[implicit]]*/ Settings(fs::path filepath);
    Settings(const Settings& settings) = delete;
    void operator=(const Settings& settings) = delete;
    virtual ~Settings();

public:
    bool load();
    void parseData(xmlNodePtr cur, SElement& elem);

    void save();

private:
    void loadDefault();
    void parseItem(xmlDocPtr doc, xmlNodePtr cur);

    static xmlNodePtr savePropertyDouble(const gchar* key, double value, xmlNodePtr parent);
    static xmlNodePtr saveProperty(const gchar* key, int value, xmlNodePtr parent);
    static xmlNodePtr savePropertyUnsigned(const gchar* key, unsigned int value, xmlNodePtr parent);
    static xmlNodePtr saveProperty(const gchar* key, const gchar* value, xmlNodePtr parent);

    void saveData(xmlNodePtr root, const std::string& name, SElement& elem);

    void saveButtonConfig();
    void loadButtonConfig();

public:
    // View Mode
    bool loadViewMode(ViewModeId mode);

    // Getter- / Setter
    const std::vector<ViewMode>& getViewModes() const;
    ViewModeId getActiveViewMode() const;

    bool isPressureSensitivity() const;
    void setPressureSensitivity(gboolean presureSensitivity);

    /**
     * Input device pressure options
     */
    double getMinimumPressure() const;
    void setMinimumPressure(double minimumPressure);

    double getPressureMultiplier() const;
    void setPressureMultiplier(double multiplier);

    /**
     * Getter, enable/disable
     */
    bool isZoomGesturesEnabled() const;
    void setZoomGesturesEnabled(bool enable);

    /**
     * The last used font
     */
    XojFont& getFont();
    void setFont(const XojFont& font);

    /**
     * Font presets. `index` is 0-based and must be smaller than FONT_PRESET_COUNT.
     * An empty slot means "never saved"; the caller then falls back to the current
     * font and the text tool's color.
     */
    const std::optional<FontPreset>& getFontPreset(size_t index) const;
    void setFontPreset(size_t index, const FontPreset& preset);

    /**
     * The selected Toolbar
     */
    void setSelectedToolbar(const std::string& name);
    std::string const& getSelectedToolbar() const;

    void setEdgePanSpeed(double speed);
    double getEdgePanSpeed() const;

    void setEdgePanMaxMult(double mult);
    double getEdgePanMaxMult() const;

    /**
     * Set the Zoomstep for one step in percent
     */
    void setZoomStep(double zoomStep);
    double getZoomStep() const;

    /**
     * Set the Zoomstep for Ctrl + Scroll in percent
     */
    void setZoomStepScroll(double zoomStepScroll);
    double getZoomStepScroll() const;

    /**
     * Whether to force zoom to fit on loading document
     */
    void setForceZoomToFitOnLoad(bool force);
    bool getForceZoomToFitOnLoad() const;


    /**
     * Sets the screen resolution in DPI
     */
    void setDisplayDpi(int dpi);
    int getDisplayDpi() const;

    void setAreStockIconsUsed(bool use);
    bool areStockIconsUsed() const;

    /**
     * The last saved path
     */
    void setLastSavePath(fs::path p);
    fs::path const& getLastSavePath() const;

    /**
     * The last open path
     */
    void setLastOpenPath(fs::path p);
    fs::path const& getLastOpenPath() const;

    void setLastImagePath(const fs::path& p);
    fs::path const& getLastImagePath() const;

    void setMainWndSize(int width, int height);
    void setMainWndMaximized(bool max);
    int getMainWndWidth() const;
    int getMainWndHeight() const;
    bool isMainWndMaximized() const;

    /**
     * Position of the main window relative to the origin of the monitor it was on, together with
     * that monitor's description.
     *
     * The monitor is what is really being remembered here, and it is remembered by description
     * rather than by index because indices are reassigned when displays are plugged in or
     * rearranged. The position is only honoured again when a monitor matching that description is
     * actually connected; otherwise placement is left to GTK. See MainWindow::restoreWindowPosition.
     */
    void setMainWndPos(int x, int y, const std::string& monitor);

    /**
     * Stable, human-readable description of a monitor -- manufacturer and model where the backend
     * exposes them, falling back to geometry. Used as the identity that survives a reconnect.
     */
    static std::string describeMonitor(GdkMonitor* monitor);
    int getMainWndPosX() const;
    int getMainWndPosY() const;
    const std::string& getMainWndMonitor() const;

    bool isFullscreen() const;

    bool isSidebarVisible() const;
    void setSidebarVisible(bool visible);

    bool isToolbarVisible() const;
    void setToolbarVisible(bool visible);

    int getSidebarWidth() const;
    void setSidebarWidth(int width);

    bool isSidebarOnRight() const;
    void setSidebarOnRight(bool right);

    bool isScrollbarOnLeft() const;
    void setScrollbarOnLeft(bool right);

    bool isMenubarVisible() const;
    void setMenubarVisible(bool visible);

    const bool isFilepathInTitlebarShown() const;
    void setFilepathInTitlebarShown(const bool shown);

    const bool isPageNumberInTitlebarShown() const;
    void setPageNumberInTitlebarShown(const bool shown);

    void setShowPairedPages(bool showPairedPages);
    bool isShowPairedPages() const;

    void setShowPageShadow(bool showPageShadow);
    bool isShowPageShadow() const;

    void setPresentationMode(bool presentationMode);
    bool isPresentationMode() const;

    void setPairsOffset(int numOffset);
    int getPairsOffset() const;

    void setEmptyLastPageAppend(EmptyLastPageAppendType emptyLastPageAppend);
    EmptyLastPageAppendType getEmptyLastPageAppend() const;

    void setViewColumns(int numColumns);
    int getViewColumns() const;

    void setViewRows(int numRows);
    int getViewRows() const;

    void setViewFixedRows(bool viewFixedRows);
    bool isViewFixedRows() const;

    void setViewLayoutVert(bool vert);
    bool getViewLayoutVert() const;

    void setViewLayoutR2L(bool r2l);
    bool getViewLayoutR2L() const;

    void setViewLayoutB2T(bool b2t);
    bool getViewLayoutB2T() const;

    bool isAutoloadMostRecent() const;
    void setAutoloadMostRecent(bool load);

    bool isAutoloadPdfXoj() const;
    void setAutoloadPdfXoj(bool load);

    int getAutosaveTimeout() const;
    void setAutosaveTimeout(int autosave);
    bool isAutosaveEnabled() const;
    void setAutosaveEnabled(bool autosave);

    bool getAddVerticalSpace() const;
    void setAddVerticalSpace(bool space);
    int getAddVerticalSpaceAmountAbove() const;
    void setAddVerticalSpaceAmountAbove(int pixels);
    int getAddVerticalSpaceAmountBelow() const;
    void setAddVerticalSpaceAmountBelow(int pixels);

    bool getAddHorizontalSpace() const;
    void setAddHorizontalSpace(bool space);
    int getAddHorizontalSpaceAmountRight() const;
    void setAddHorizontalSpaceAmountRight(int pixels);
    int getAddHorizontalSpaceAmountLeft() const;
    void setAddHorizontalSpaceAmountLeft(int pixels);

    bool getUnlimitedScrolling() const;
    void setUnlimitedScrolling(bool enable);

    bool getDrawDirModsEnabled() const;
    void setDrawDirModsEnabled(bool enable);
    int getDrawDirModsRadius() const;
    void setDrawDirModsRadius(int pixels);

    bool getTouchDrawingEnabled() const;
    void setTouchDrawingEnabled(bool b);

    bool getGtkTouchInertialScrollingEnabled() const;
    void setGtkTouchInertialScrollingEnabled(bool b);

    bool isPressureGuessingEnabled() const;
    void setPressureGuessingEnabled(bool b);

    bool isSnapRotation() const;
    void setSnapRotation(bool b);
    double getSnapRotationTolerance() const;
    void setSnapRotationTolerance(double tolerance);

    bool isSnapGrid() const;
    void setSnapGrid(bool b);
    double getSnapGridTolerance() const;
    void setSnapGridTolerance(double tolerance);
    double getSnapGridSize() const;
    void setSnapGridSize(double gridSize);

    double getStrokeRecognizerMinSize() const;
    void setStrokeRecognizerMinSize(double value);

    double getExtendedLineOvershoot() const;
    void setExtendedLineOvershoot(double value);

    StylusCursorType getStylusCursorType() const;
    void setStylusCursorType(StylusCursorType stylusCursorType);

    EraserVisibility getEraserVisibility() const;
    void setEraserVisibility(EraserVisibility eraserVisibility);

    IconTheme getIconTheme() const;
    void setIconTheme(IconTheme iconTheme);

    void setThemeVariant(ThemeVariant theme);
    ThemeVariant getThemeVariant() const;

    SidebarNumberingStyle getSidebarNumberingStyle() const;
    void setSidebarNumberingStyle(SidebarNumberingStyle numberingStyle);

    bool isHighlightPosition() const;
    void setHighlightPosition(bool highlight);

    Color getCursorHighlightColor() const;
    void setCursorHighlightColor(Color color);

    double getCursorHighlightRadius() const;
    void setCursorHighlightRadius(double radius);

    Color getCursorHighlightBorderColor() const;
    void setCursorHighlightBorderColor(Color color);

    double getCursorHighlightBorderWidth() const;
    void setCursorHighlightBorderWidth(double width);

    ScrollbarHideType getScrollbarHideType() const;
    void setScrollbarHideType(ScrollbarHideType type);

    bool isScrollbarFadeoutDisabled() const;
    void setScrollbarFadeoutDisabled(bool disable);

    bool isAudioDisabled() const;
    void setAudioDisabled(bool disable);

    std::u8string const& getDefaultSaveName() const;
    void setDefaultSaveName(const std::u8string& name);

    std::u8string const& getDefaultPdfExportName() const;
    void setDefaultPdfExportName(const std::u8string& name);

    ButtonConfig* getButtonConfig(unsigned int id);

    void setViewMode(ViewModeId mode, ViewMode ViewMode);

    Color getBorderColor() const;
    void setBorderColor(Color color);

    Color getSelectionColor() const;
    void setSelectionColor(Color color);

    Color getBackgroundColor() const;
    void setBackgroundColor(Color color);

    Color getActiveSelectionColor() const;
    void setActiveSelectionColor(Color color);

    const RecolorParameters& getRecolorParameters() const;
    void setRecolorParameters(RecolorParameters&& recolorParameters);

    // Re-render pages if document zoom differs from the last render zoom by the given threshold.
    double getPDFPageRerenderThreshold() const;
    void setPDFPageRerenderThreshold(double threshold);

    double getTouchZoomStartThreshold() const;
    void setTouchZoomStartThreshold(double threshold);

    int getPdfPageCacheSize() const;
    [[maybe_unused]] void setPdfPageCacheSize(int size);

    unsigned int getPreloadPagesBefore() const;
    void setPreloadPagesBefore(unsigned int n);

    unsigned int getPreloadPagesAfter() const;
    void setPreloadPagesAfter(unsigned int n);

    bool isEagerPageCleanup() const;
    void setEagerPageCleanup(bool b);

    PageTemplateSettings const& getPageTemplateSettings() const;
    void setPageTemplateSettings(const PageTemplateSettings& pageTemplateSettings);

    /**
     * Folder holding the sound files strokes are timestamped into. Outside the audio guard because
     * the screen recorder falls back to it when no separate video folder is set.
     */
    fs::path const& getAudioFolder() const;
    void setAudioFolder(fs::path audioFolder);

#ifdef ENABLE_AUDIO
    static constexpr PaDeviceIndex AUDIO_INPUT_SYSTEM_DEFAULT = -1;
    PaDeviceIndex getAudioInputDevice() const;
    void setAudioInputDevice(PaDeviceIndex deviceIndex);

    static constexpr PaDeviceIndex AUDIO_OUTPUT_SYSTEM_DEFAULT = -1;
    PaDeviceIndex getAudioOutputDevice() const;
    void setAudioOutputDevice(PaDeviceIndex deviceIndex);

    double getAudioSampleRate() const;
    void setAudioSampleRate(double sampleRate);

    double getAudioGain() const;
    void setAudioGain(double gain);

    unsigned int getDefaultSeekTime() const;
    void setDefaultSeekTime(unsigned int t);
#endif

    // ---------------------------------------------------------------------------------------
    // Video recording
    //
    // What is recorded is the canvas, drawn from the document model at the output resolution --
    // never the screen. The encoder is an external ffmpeg process; the microphone is the existing
    // PortAudio pipeline, so those settings live under Audio Recording and are not repeated here.
    // ---------------------------------------------------------------------------------------

    /// Record a video of the canvas when the record button is pressed.
    bool isVideoRecordingEnabled() const;
    void setVideoRecordingEnabled(bool enabled);

    /// Record the microphone into that video.
    bool isVideoRecordingWithAudio() const;
    void setVideoRecordingWithAudio(bool withAudio);

    /**
     * Also write the separate sound file that strokes are timestamped against.
     *
     * Off by default: it is a second file for a feature -- replaying the audio that was being
     * recorded while a given stroke was drawn -- that has nothing to do with wanting a video, and
     * an unasked-for .ogg turning up beside every recording is a surprise. Turning it on costs
     * nothing extra at the microphone; the same capture feeds both.
     */
    bool isVideoRecordingKeepAudioFile() const;
    void setVideoRecordingKeepAudioFile(bool keep);

    /// Where finished videos are written. Empty falls back to the audio folder.
    fs::path const& getVideoFolder() const;
    void setVideoFolder(fs::path videoFolder);

    /// Explicit ffmpeg binary; empty means "look on PATH and in the usual package prefixes".
    std::string const& getVideoRecordingFfmpegPath() const;
    void setVideoRecordingFfmpegPath(std::string path);

    int getVideoRecordingWidth() const;
    int getVideoRecordingHeight() const;
    void setVideoRecordingSize(int width, int height);

    int getVideoRecordingFps() const;
    void setVideoRecordingFps(int fps);

    /// Video bitrate in kbit/s.
    int getVideoRecordingVideoBitrate() const;
    void setVideoRecordingVideoBitrate(int kbits);

    /// Constant quality, 1 (worst) to 100 (best); 0 encodes to the bitrate instead.
    int getVideoRecordingQuality() const;
    void setVideoRecordingQuality(int quality);

    /// Seconds between keyframes, and the most a truncated recording can lose off its tail.
    int getVideoRecordingKeyframeInterval() const;
    void setVideoRecordingKeyframeInterval(int seconds);

    /// Audio bitrate in kbit/s.
    int getVideoRecordingAudioBitrate() const;
    void setVideoRecordingAudioBitrate(int kbits);

    /// An ffmpeg encoder name, e.g. "h264_videotoolbox" or "libx264".
    std::string const& getVideoRecordingVideoCodec() const;
    void setVideoRecordingVideoCodec(std::string codec);

    std::string const& getVideoRecordingAudioCodec() const;
    void setVideoRecordingAudioCodec(std::string codec);

    /// Container extension without the dot: "mov", "mp4" or "mkv".
    std::string const& getVideoRecordingContainer() const;
    void setVideoRecordingContainer(std::string container);

    /// Extra ffmpeg arguments, appended last so they override everything derived from settings.
    std::string const& getVideoRecordingExtraArguments() const;
    void setVideoRecordingExtraArguments(std::string arguments);

    /**
     * Draw a marker at the pen's position on the live canvas and into the recorded picture.
     *
     * The frame is rendered from the document, so nothing that lives on the desktop -- the system
     * pointer, the pen cursor Xournal++ hands to GTK -- can appear in it. Without this, a viewer
     * sees ink arrive with no idea where the pen was in between, and pointing at something already
     * written is invisible. On by default: a recording of a lecture is the case this feature is
     * for, and a lecture is half pointing.
     *
     * Drawn by one shared marker renderer on the live canvas and wherever the shared canvas frame
     * is drawn -- which means the projector window and recording show the same shape, color, tip
     * and proportional size. The live canvas replaces ordinary native drawing cursors with the
     * marker so platform custom-cursor limits cannot clip it or turn it back into an arrow. The
     * eraser retains its rectangular live size outline when allowed by Eraser Visibility; frames
     * represent it with a neutral marker.
     */
    bool isVideoRecordingShowPointer() const;
    void setVideoRecordingShowPointer(bool show);

    /**
     * Diameter of that marker, in pixels of the recorded frame.
     *
     * Given in output pixels rather than document units for the same reason the caption safe area
     * is: what matters is whether it reads on the finished video. Held to a fraction of the page
     * so it means the same thing in a projector window of any size.
     *
     * Not an integer, and deliberately unbounded at both ends: a marker two pixels across is a
     * legitimate thing to want on a 4K recording, and so is one that takes up half the page.
     * Nothing here knows better than the person watching the result which of those is right.
     *
     * Zero draws nothing, which is a second way to switch the marker off and the convenient one
     * while a recording is being set up -- the number is already under the cursor, and putting the
     * old value back is easier than remembering which checkbox was turned off.
     */
    double getVideoRecordingPointerSize() const;
    void setVideoRecordingPointerSize(double pixels);

    /**
     * Which shape that marker takes.
     *
     * All three are drawn at the same size and all three put a mark on the exact tip; what they
     * trade is how much of the page underneath survives. A disk is what a screen recorder normally
     * uses and keeps the writing readable through it; a ring hides nothing at all but is easier to
     * lose against busy ink; a dot is the most visible and the most opaque.
     */
    PointerMarkerShape getVideoRecordingPointerShape() const;
    void setVideoRecordingPointerShape(PointerMarkerShape shape);

    // ---------------------------------------------------------------------------------------
    // Microphone processing
    //
    // The chain a streaming setup puts between a microphone and a recording: compressor,
    // equalizer, noise suppression. Named and scaled the way OBS names and scales them, so a
    // setting copied from one to the other means the same thing. Applied by ffmpeg on its way
    // into the video, in that order, and never to the separate .ogg -- see AudioFilterConfig.
    // ---------------------------------------------------------------------------------------

    bool isMicCompressorEnabled() const;
    void setMicCompressorEnabled(bool enabled);

    /// Level above which the compressor starts working, in dB.
    double getMicCompressorThreshold() const;
    void setMicCompressorThreshold(double dB);

    /// How much quieter than the input a signal above the threshold gets, as "n:1".
    double getMicCompressorRatio() const;
    void setMicCompressorRatio(double ratio);

    /// Attack and release, in milliseconds.
    double getMicCompressorAttack() const;
    void setMicCompressorAttack(double ms);
    double getMicCompressorRelease() const;
    void setMicCompressorRelease(double ms);

    /// Gain applied after compressing, in dB, to make up for what the compressor took away.
    double getMicCompressorOutputGain() const;
    void setMicCompressorOutputGain(double dB);

    bool isMicEqualizerEnabled() const;
    void setMicEqualizerEnabled(bool enabled);

    /// Gains of the three bands, in dB. The crossovers are fixed -- see AudioFilterConfig.
    double getMicEqualizerLow() const;
    void setMicEqualizerLow(double dB);
    double getMicEqualizerMid() const;
    void setMicEqualizerMid(double dB);
    double getMicEqualizerHigh() const;
    void setMicEqualizerHigh(double dB);

    /// One of "off", "rnnoise" or "fft"; anything else is read as "off".
    std::string const& getMicNoiseSuppression() const;
    void setMicNoiseSuppression(std::string method);

    /// An .rnnn model for the RNNoise method. Empty means the one shipped with the application.
    std::string const& getMicRnnoiseModel() const;
    void setMicRnnoiseModel(std::string path);

    // ---------------------------------------------------------------------------------------
    // Projector window
    // ---------------------------------------------------------------------------------------

    /**
     * Where the projector was when it was last closed: position relative to the origin of
     * getProjectorMonitor(), plus its size. Remembered by monitor description for the same reason
     * the main window is -- see setMainWndPos.
     */
    void setProjectorGeometry(int x, int y, int width, int height, const std::string& monitor);
    int getProjectorPosX() const;
    int getProjectorPosY() const;
    int getProjectorWidth() const;
    int getProjectorHeight() const;
    std::string const& getProjectorMonitor() const;

    bool isProjectorKeepAbove() const;
    void setProjectorKeepAbove(bool keepAbove);

    /// Open the projector automatically at startup, on the display it was last closed on.
    bool isProjectorOpenAtStartup() const;
    void setProjectorOpenAtStartup(bool open);

    /// Constrain the projector's shape to the recording's aspect ratio while it is resized.
    bool isProjectorLockAspectRatio() const;
    void setProjectorLockAspectRatio(bool lock);

    /// Draw a translucent band where burnt-in captions would sit.
    bool isProjectorShowSafeArea() const;
    void setProjectorShowSafeArea(bool show);

    /**
     * Height of that band, in pixels of the recording's own frame -- so 150 against a 1080-line
     * recording marks the bottom 150 lines of the finished video, which is how a subtitling
     * requirement is normally written down. The projector scales it to whatever size it is at.
     */
    int getProjectorSafeAreaHeight() const;
    void setProjectorSafeAreaHeight(int pixels);

    Color getProjectorBackgroundColor() const;
    void setProjectorBackgroundColor(Color color);

    /**
     * Show the frame rate in the projector window and next to the record button.
     *
     * Never in the recording itself -- it is drawn over the projector's own picture, after the
     * frame the encoder is given has already been made, so what is on screen for the presenter
     * differs from the file by exactly this one readout.
     */
    bool isShowFrameRate() const;
    void setShowFrameRate(bool show);

    std::string const& getPluginEnabled() const;
    void setPluginEnabled(const std::string& pluginEnabled);

    std::string const& getPluginDisabled() const;
    void setPluginDisabled(const std::string& pluginDisabled);

    /**
     * Sets #numIgnoredStylusEvents. If given a negative value writes 0 instead.
     */
    void setIgnoredStylusEvents(int numEvents);
    /**
     * Returns #numIgnoredStylusEvents.
     */
    int getIgnoredStylusEvents() const;

    bool getInputSystemTPCButtonEnabled() const;
    void setInputSystemTPCButtonEnabled(bool tpcButtonEnabled);

    bool getInputSystemDrawOutsideWindowEnabled() const;
    void setInputSystemDrawOutsideWindowEnabled(bool drawOutsideWindowEnabled);

    void loadDeviceClasses();
    void saveDeviceClasses();
    void setDeviceClassForDevice(GdkDevice* device, InputDeviceTypeOption deviceClass);
    void setDeviceClassForDevice(const std::string& deviceName, GdkInputSource deviceSource,
                                 InputDeviceTypeOption deviceClass);
    InputDeviceTypeOption getDeviceClassForDevice(GdkDevice* device) const;
    InputDeviceTypeOption getDeviceClassForDevice(const std::string& deviceName, GdkInputSource deviceSource) const;
    std::vector<InputDevice> getKnownInputDevices() const;

    /**
     * Get name, e.g. "cm"
     */
    std::string const& getSizeUnit() const;

    /**
     * Get size index in XOJ_UNITS
     */
    int getSizeUnitIndex() const;

    /**
     * Set Unit, e.g. "cm"
     */
    void setSizeUnit(const std::string& sizeUnit);

    /**
     * Set size index in XOJ_UNITS
     */
    void setSizeUnitIndex(int sizeUnitId);

    /**
     * Set StrokeFilter enabled
     */
    void setStrokeFilterEnabled(bool enabled);

    /**
     * Get StrokeFilter enabled
     */
    bool getStrokeFilterEnabled() const;

    /**
     * get strokeFilter settings
     */
    void getStrokeFilter(int* strokeFilterIgnoreTime, double* strokeFilterIgnoreLength,
                         int* strokeFilterSuccessiveTime) const;

    /**
     * configure stroke filter
     */
    void setStrokeFilter(int strokeFilterIgnoreTime, double strokeFilterIgnoreLength, int strokeFilterSuccessiveTime);

    /**
     * Set DoActionOnStrokeFilter enabled
     */
    void setDoActionOnStrokeFiltered(bool enabled);

    /**
     * Get DoActionOnStrokeFilter enabled
     */
    bool getDoActionOnStrokeFiltered() const;

    /**
     * Set TrySelectOnStrokeFilter enabled
     */
    void setTrySelectOnStrokeFiltered(bool enabled);

    /**
     * Get TrySelectOnStrokeFilter enabled
     */
    bool getTrySelectOnStrokeFiltered() const;

    /**
     * Set snap recognized shapes enabled
     */
    void setSnapRecognizedShapesEnabled(bool enabled);

    /**
     * Get snap recognized shapes enabled
     */
    bool getSnapRecognizedShapesEnabled() const;

    /**
     * Set line width restoring for resized edit selctions enabled
     */
    void setRestoreLineWidthEnabled(bool enabled);

    /**
     * Get line width restoring enabled
     */
    bool getRestoreLineWidthEnabled() const;

    /**
     * Set the preferred locale
     */
    void setPreferredLocale(std::string const& locale);

    /**
     * Get the preferred locale
     */
    std::string getPreferredLocale() const;

    /**
     * Stabilizer related getters and setters
     */
    bool getStabilizerCuspDetection() const;
    bool getStabilizerFinalizeStroke() const;
    size_t getStabilizerBuffersize() const;
    double getStabilizerDeadzoneRadius() const;
    double getStabilizerDrag() const;
    double getStabilizerMass() const;
    double getStabilizerSigma() const;
    StrokeStabilizer::AveragingMethod getStabilizerAveragingMethod() const;
    StrokeStabilizer::Preprocessor getStabilizerPreprocessor() const;

    void setStabilizerCuspDetection(bool cuspDetection);
    void setStabilizerFinalizeStroke(bool finalizeStroke);
    void setStabilizerBuffersize(size_t buffersize);
    void setStabilizerDeadzoneRadius(double deadzoneRadius);
    void setStabilizerDrag(double drag);
    void setStabilizerMass(double mass);
    void setStabilizerSigma(double sigma);
    void setStabilizerAveragingMethod(StrokeStabilizer::AveragingMethod averagingMethod);
    void setStabilizerPreprocessor(StrokeStabilizer::Preprocessor preprocessor);

    fs::path const& getColorPaletteSetting();
    void setColorPaletteSetting(fs::path palettePath);

    void setNumberOfSpacesForTab(unsigned int numberSpaces);
    unsigned int getNumberOfSpacesForTab() const;

    void setUseSpacesAsTab(bool useSpaces);
    bool getUseSpacesAsTab() const;

    void setLaserPointerFadeOutTime(unsigned int timeInMs);
    unsigned int getLaserPointerFadeOutTime() const;

public:
    // Custom settings
    SElement& getCustomElement(const std::string& name);

    /**
     * Call this after you have done all custom settings changes
     */
    void customSettingsChanged();

    /**
     * Do not save settings until transactionEnd() is called
     */
    void transactionStart();

    /**
     * Stop transaction and save settings
     */
    void transactionEnd();

    LatexSettings latexSettings{};

    inline const fs::path& getSettingsFile() const { return filepath; }

private:
    /**
     *  The config filepath
     */
    fs::path filepath;

private:
    /**
     * The settings tree
     */
    std::map<std::string, SElement> data;

    /**
     *  Use pen pressure to control stroke width?
     */
    bool pressureSensitivity{};

    /**
     * Adjust input pressure?
     */
    double minimumPressure{};
    double pressureMultiplier{};

    /**
     * If the touch zoom gestures are enabled
     */
    bool zoomGesturesEnabled{};

    /**
     *  If fullscreen is active
     */
    bool fullscreenActive{};

    /**
     *  If the sidebar is visible
     */
    bool showSidebar{};

    /**
     *  If the sidebar is visible
     */
    bool showToolbar{};

    /**
     *  The Width of the Sidebar
     */
    int sidebarWidth{};

    /**
     *  If the sidebar is on the right
     */
    bool sidebarOnRight{};

    /**
     *  Type of cursor icon to use with a stylus
     */
    StylusCursorType stylusCursorType;

    /**
     * Visibility of eraser cursor
     */
    EraserVisibility eraserVisibility;

    /**
     * Icon Theme
     */
    IconTheme iconTheme;

    /**
     * Follow system's default theme variant or force one or the other
     */
    ThemeVariant themeVariant;

    /**
     * Sidebar page number style
     */
    SidebarNumberingStyle sidebarNumberingStyle;

    /**
     * Show a colored circle around the cursor
     */
    bool highlightPosition{};

    /**
     * Cursor highlight color (ARGB format)
     */
    Color cursorHighlightColor{};

    /**
     * Radius of cursor highlight circle. Note that this is limited by the size
     * of the cursor in the display server (default is probably 30 pixels).
     */
    double cursorHighlightRadius{};

    /**
     * Cursor highlight border color (ARGB format)
     */
    Color cursorHighlightBorderColor{};

    /**
     * Width of cursor highlight border, in pixels.
     */
    double cursorHighlightBorderWidth{};

    /**
     * If stock icons are used instead of Xournal++ icons when available
     */
    bool useStockIcons{};

    /**
     * If the menu bar is visible on startup
     */
    bool menubarVisible{};

    /**
     * If the filepath is shown in titlebar
     */
    bool filepathShownInTitlebar{};

    /**
     * If the page number is shown in titlebar
     */
    bool pageNumberShownInTitlebar{};

    /**
     *  Hide the scrollbar
     */
    ScrollbarHideType scrollbarHideType;

    /**
     * Disable scrollbar fade out (overlay scrolling)
     */
    bool disableScrollbarFadeout{};

    /**
     * Disable the audio system
     */
    bool disableAudio{};

    /**
     *  The selected Toolbar name
     */
    std::string selectedToolbar;

    /**
     *  The last saved folder
     */
    fs::path lastSavePath;

    /**
     *  The last opened folder
     */
    fs::path lastOpenPath;

    /**
     *  The last "insert image" folder
     */
    fs::path lastImagePath;

    /**
     * The last used font
     */
    XojFont font;

    /**
     * The font presets, stored as `fontPreset1` .. `fontPreset4` properties.
     * An unset slot has never been saved by the user.
     */
    std::array<std::optional<FontPreset>, FONT_PRESET_COUNT> fontPresets;

    /**
     * Base speed (as a percentage of visible canvas) of edge pan per
     * second
     */
    double edgePanSpeed{};

    /**
     * Maximum multiplier of edge pan speed due to proportion of selection out
     * of view
     */
    double edgePanMaxMult{};

    /**
     * Zoomstep for one step
     */
    double zoomStep{};

    /**
     * Zoomstep for Ctrl + Scroll zooming
     */
    double zoomStepScroll{};

    /**
     * Whether to force zoom to fit on loading document
     */
    bool forceZoomToFitOnLoad{};

    /**
     * The display resolution, in pixels per inch. -1 for automatic detection
     */
    int displayDpi{};

    /**
     *  If the window is maximized
     */
    bool maximized{};

    /**
     * Width of the main window
     */
    int mainWndWidth{};

    /**
     * Height of the main window
     */
    int mainWndHeight{};

    /**
     * Position of the main window RELATIVE TO THE ORIGIN OF mainWndMonitor, not in root
     * coordinates. The monitor is the anchor: root coordinates only keep their meaning while the
     * display arrangement is unchanged, so a window remembered at x=1440 lands on the built-in
     * display as soon as the external one is plugged in on the other side.
     */
    int mainWndPosX{};
    int mainWndPosY{};

    /**
     * Description of the monitor the main window was last on, as built by
     * Settings::describeMonitor. Empty when unknown.
     */
    std::string mainWndMonitor{};

    /**
     * Show the scrollbar on the left side
     */
    bool scrollbarOnLeft{};

    /**
     *  Pairs pages
     */
    bool showPairedPages{};

    /**
     *  Show shadow behind pages
     */
    bool showPageShadow{};

    /**
     *  Sets presentation mode
     */
    bool presentationMode{};

    /**
     *  Offsets first page ( to align pairing )
     */
    int numPairsOffset{};

    /**
     * Preference for appending an empty last page to the document
     */
    EmptyLastPageAppendType emptyLastPageAppend{};

    /**
     *  Use when fixed number of columns
     */
    int numColumns{};

    /**
     *  Use when fixed number of rows
     */
    int numRows{};

    /**
     *  USE  fixed rows, otherwise fixed columns
     */
    bool viewFixedRows{};

    /**
     *  Layout Vertical then Horizontal
     */
    bool layoutVertical{};

    /**
     *  Layout pages right to left
     */
    bool layoutRightToLeft{};

    /**
     *  Layout Bottom to Top
     */
    bool layoutBottomToTop{};


    /**
     * Automatically load filename.pdf.xoj / .pdf.xopp instead of filename.pdf (true/false)
     */
    bool autoloadPdfXoj{};

    /**
     * Automatically load most recent document on application startup (true/false)
     */
    bool autoloadMostRecent{};

    /**
     * Automatically save documents for crash recovery each x minutes
     */
    int autosaveTimeout{};

    /**
     *  Enable automatic save
     */
    bool autosaveEnabled{};

    /**
     * Allow scroll outside the page display area (horizontal)
     */
    bool addHorizontalSpace{};

    /**
     * How much allowance to scroll outside the page display area on the right
     */
    int addHorizontalSpaceAmountRight{};

    /**
     * How much allowance to scroll outside the page display area on the left
     */
    int addHorizontalSpaceAmountLeft{};

    /**
     * Allow scroll outside the page display area (vertical)
     */
    bool addVerticalSpace{};

    /**
     * How much allowance to scroll outside the page display area above
     */
    int addVerticalSpaceAmountAbove{};

    /**
     * How much allowance to scroll outside the page display area below
     */
    int addVerticalSpaceAmountBelow{};

    /**
     * Enables unlimited scrolling, which automatically adds maximum space to scroll outside the page
     */
    bool unlimitedScrolling{};

    /**
     * Emulate modifier keys based on initial direction of drawing tool ( for Rectangle, Ellipse etc. )
     */
    bool drawDirModsEnabled{};

    /**
     * Radius at which emulated modifiers are locked on for the rest of drawing operation
     */
    int drawDirModsRadius{};

    /**
     * Rotation snapping enabled by default
     */
    bool snapRotation{};

    /**
     * grid snapping enabled by default
     */
    bool snapGrid{};

    /**
     * Default name if you save a new document
     */
    std::u8string defaultSaveName;  // should be string - don't change to path

    std::u8string defaultPdfExportName;

    /**
     * The button config
     */
    std::array<std::unique_ptr<ButtonConfig>, BUTTON_COUNT> buttonConfig;

    /**
     * View-modes. Predefined: 0=default, 1=fullscreen, 2=presentation
     */
    ViewModeId activeViewMode;
    std::vector<ViewMode> viewModes;

    /**
     *  The count of pages which will be cached
     */
    int pdfPageCacheSize{};

    /**
     *  Percentage by which the page's zoom must change
     * for PDF pages to re-render while zooming.
     */
    double pageRerenderThreshold{};

    /**
     * Don't start zooming with touch until the difference in distances between the
     * current touch points and the original is greater than this percentage of the
     * original distance.
     */
    double touchZoomStartThreshold{};

    /**
     * The color to draw borders on selected elements
     * (Page, insert image selection etc.)
     */
    Color selectionBorderColor{};

    /**
     * Color for Text selection, Stroke selection etc.
     */
    Color selectionMarkerColor{};

    /**
     * Color for active selection.
     */
    Color activeSelectionColor{};

    /**
     * The color for Xournal page background
     */
    Color backgroundColor{};

    /**
     * The parameters for the recoloring logic
     * Also contains fields whether it is active at all
     */
    RecolorParameters recolorParameters{};

    /**
     * Page template (format, background, color...)
     */
    PageTemplateSettings pageTemplateSettings;

    /**
     * Unit, see XOJ_UNITS
     */
    std::string sizeUnit;

    /**
     * Audio folder for audio recording
     */
    fs::path audioFolder;

    /**
     * Snap tolerance for the graph/dotted grid
     */
    double snapGridTolerance{};

    /**
     * Rotation epsilon for rotation snapping feature
     */
    double snapRotationTolerance{};

    /// Grid size for Snapping
    double snapGridSize{};

    /**
     * Minimum size of stroke to detect shape
     */
    double strokeRecognizerMinSize{};

    /**
     * How far (in page units) the ray and infinite line drawing types overshoot the dragged
     * endpoints before their arrow head. There is no preferences dialog entry for this: edit
     * the extendedLineOvershoot property in settings.xml to change it.
     */
    double extendedLineOvershoot{};

    /// Touchscreens act like multi-touch-aware pens.
    bool touchDrawing{};

    /// True iff we use GTK's built-in kinetic/inertial scrolling
    /// for touchscreen devices. If false, we use our own.
    bool gtkTouchInertialScrolling{};

    /**
     * Infer pressure from speed when device pressure
     * is unavailable (e.g. drawing with a mouse).
     */
    bool pressureGuessing{};

#ifdef ENABLE_AUDIO
    /**
     * The index of the audio device used for recording
     */
    PaDeviceIndex audioInputDevice{};

    /**
     * The index of the audio device used for playback
     */
    PaDeviceIndex audioOutputDevice{};

    /**
     * The sample rate used for recording
     */
    double audioSampleRate{};

    /**
     * The gain by which to amplify the recorded audio samples
     */
    double audioGain{};

    /**
     * The default time by which the playback will seek backwards and forwards
     */
    unsigned int defaultSeekTime{};
#endif

    /**
     * Video recording. Names match the settings.xml keys one-for-one, and the defaults are the
     * ones a 1080p60 lecture capture wants: see Settings::loadDefault.
     */
    /// Where finished videos are written; empty falls back to the audio folder.
    fs::path videoFolder;

    bool videoRecordingEnabled{};
    bool videoRecordingWithAudio{};
    bool videoRecordingKeepAudioFile{};

    std::string videoRecordingFfmpegPath;
    int videoRecordingWidth{};
    int videoRecordingHeight{};
    int videoRecordingFps{};
    int videoRecordingVideoBitrate{};
    int videoRecordingQuality{};
    int videoRecordingKeyframeInterval{};
    int videoRecordingAudioBitrate{};
    std::string videoRecordingVideoCodec;
    std::string videoRecordingAudioCodec;
    std::string videoRecordingContainer;
    std::string videoRecordingExtraArguments;
    bool videoRecordingShowPointer{};
    double videoRecordingPointerSize{};
    PointerMarkerShape videoRecordingPointerShape{};

    /**
     * Microphone processing. Units are OBS's: dB for levels and gains, milliseconds for times,
     * a plain number for the compression ratio.
     */
    bool micCompressorEnabled{};
    double micCompressorThreshold{};
    double micCompressorRatio{};
    double micCompressorAttack{};
    double micCompressorRelease{};
    double micCompressorOutputGain{};

    bool micEqualizerEnabled{};
    double micEqualizerLow{};
    double micEqualizerMid{};
    double micEqualizerHigh{};

    std::string micNoiseSuppression;
    std::string micRnnoiseModel;

    /**
     * Projector window placement, stored the same way as the main window's: an offset from the
     * origin of a monitor identified by description, not a root coordinate.
     */
    int projectorPosX{};
    int projectorPosY{};
    int projectorWidth{};
    int projectorHeight{};
    std::string projectorMonitor;
    bool projectorKeepAbove{};
    bool projectorOpenAtStartup{};
    bool projectorLockAspectRatio{};
    bool projectorShowSafeArea{};
    int projectorSafeAreaHeight{};
    Color projectorBackgroundColor{};

    /// See isShowFrameRate().
    bool showFrameRate{};

    /**
     * List of enabled plugins (only the one which are not enabled by default)
     */
    std::string pluginEnabled;

    /**
     * List of disabled plugins (only the one which are not disabled by default)
     */
    std::string pluginDisabled;


    /**
     * Used to filter strokes of short time and length unless successive in order to do something else ( i.e. select
     * object, float Toolbox menu ). strokeFilterIgnoreLength			this many mm ( double ) strokeFilterIgnoreTime
     * within this time (ms)  will be ignored.. strokeFilterSuccessiveTime		...unless successive within this time.
     */
    int strokeFilterIgnoreTime{};
    double strokeFilterIgnoreLength{};
    int strokeFilterSuccessiveTime{};
    bool strokeFilterEnabled{};
    bool doActionOnStrokeFiltered{};
    bool trySelectOnStrokeFiltered{};

    /**
     * Whether snapping for recognized shapes is enabled
     */
    bool snapRecognizedShapesEnabled{};

    /**
     * Whether the line width should be preserved in a resizing operation
     */
    bool restoreLineWidthEnabled{};

    /**
     * How many stylus events since hitting the screen should be ignored before actually starting the action. If set to
     * 0, no event will be ignored. Should not be negative.
     */
    int numIgnoredStylusEvents{};

    /**
     * Whether Wacom parameter TabletPCButton is enabled
     */
    bool inputSystemTPCButton{};

    bool inputSystemDrawOutsideWindow{};

    std::map<std::string, std::pair<InputDeviceTypeOption, GdkInputSource>> inputDeviceClasses = {};

    /**
     * "Transaction" running, do not save until the end is reached
     */
    bool inTransaction{};

    /** The preferred locale as its language code
     * e.g. "en_US"
     */
    std::string preferredLocale;

    /**
     * The number of pages to pre-load before the current page.
     */
    unsigned int preloadPagesBefore{};

    /**
     * The number of pages to pre-load after the current page.
     */
    unsigned int preloadPagesAfter{};

    /**
     * Whether to evict from the page buffer cache when scrolling.
     */
    bool eagerPageCleanup{};

    /**
     * Stabilizer related settings
     */
    bool stabilizerCuspDetection{};
    bool stabilizerFinalizeStroke{};

    size_t stabilizerBuffersize{};
    double stabilizerDeadzoneRadius{};
    double stabilizerDrag{};
    double stabilizerMass{};
    double stabilizerSigma{};
    StrokeStabilizer::AveragingMethod stabilizerAveragingMethod{};
    StrokeStabilizer::Preprocessor stabilizerPreprocessor{};

    fs::path colorPaletteSetting;

    /**
     * Tab control settings
     */
    bool useSpacesForTab{};
    unsigned int numberOfSpacesForTab{};

    unsigned int laserPointerFadeOutTime{};  ///< Time in ms before the laser pointer strokes start fading out
};
