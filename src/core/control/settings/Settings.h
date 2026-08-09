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
constexpr unsigned int MAX_SPACES_FOR_TAB = 8U;

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
    // Screen recording
    //
    // Deliberately outside the ENABLE_AUDIO guard. The screen capture is run by an external
    // ffmpeg process and shares nothing with the PortAudio pipeline, so a build without audio
    // support can still record video.
    // ---------------------------------------------------------------------------------------

    /// Capture the screen alongside the sound when the record button is pressed.
    bool isScreenRecordingEnabled() const;
    void setScreenRecordingEnabled(bool enabled);

    /**
     * Also write the separate sound file that strokes are timestamped against.
     *
     * With this off, pressing record produces a video and nothing else -- which is what most
     * people want from a screen recording. It is on by default because turning it off silently
     * disables the stroke playback feature: without that file there is no audio for a stroke to
     * point at.
     */
    bool isScreenRecordingKeepAudioFile() const;
    void setScreenRecordingKeepAudioFile(bool keep);

    /// Where finished videos are written. Empty falls back to the audio folder.
    fs::path const& getVideoFolder() const;
    void setVideoFolder(fs::path videoFolder);

    /// Explicit ffmpeg binary; empty means "look on PATH and in the usual package prefixes".
    std::string const& getScreenRecordingFfmpegPath() const;
    void setScreenRecordingFfmpegPath(std::string path);

    int getScreenRecordingWidth() const;
    int getScreenRecordingHeight() const;
    void setScreenRecordingSize(int width, int height);

    int getScreenRecordingFps() const;
    void setScreenRecordingFps(int fps);

    /// Video bitrate in kbit/s.
    int getScreenRecordingVideoBitrate() const;
    void setScreenRecordingVideoBitrate(int kbits);

    /// Audio bitrate in kbit/s.
    int getScreenRecordingAudioBitrate() const;
    void setScreenRecordingAudioBitrate(int kbits);

    int getScreenRecordingAudioSampleRate() const;
    void setScreenRecordingAudioSampleRate(int sampleRate);

    /// An ffmpeg encoder name, e.g. "h264_videotoolbox" or "libx264".
    std::string const& getScreenRecordingVideoCodec() const;
    void setScreenRecordingVideoCodec(std::string codec);

    std::string const& getScreenRecordingAudioCodec() const;
    void setScreenRecordingAudioCodec(std::string codec);

    /// Container extension without the dot: "mov", "mp4" or "mkv".
    std::string const& getScreenRecordingContainer() const;
    void setScreenRecordingContainer(std::string container);

    bool isScreenRecordingCaptureCursor() const;
    void setScreenRecordingCaptureCursor(bool capture);

    /**
     * Index of the screen to capture, in the platform grabber's own numbering.
     *
     * The default is SCREEN_RECORDING_FIRST_SCREEN rather than 0, because a grabber lists cameras
     * and screens in one numbering and the cameras come first -- device 0 is usually the webcam.
     */
    static constexpr int SCREEN_RECORDING_FIRST_SCREEN = -1;
    int getScreenRecordingVideoDevice() const;
    void setScreenRecordingVideoDevice(int index);

    /// Index of the microphone to record, in the grabber's separate audio numbering; -1 for none.
    static constexpr int SCREEN_RECORDING_NO_AUDIO = -1;
    int getScreenRecordingAudioDevice() const;
    void setScreenRecordingAudioDevice(int index);

    /// Audio device by name, for the backends that address devices that way (dshow, pulse).
    std::string const& getScreenRecordingAudioDeviceName() const;
    void setScreenRecordingAudioDeviceName(std::string name);

    /**
     * Region of the captured screen to keep, in captured pixels -- which on a HiDPI panel are not
     * the same as the logical pixels the window manager reports. A zero width or height means the
     * whole screen.
     */
    int getScreenRecordingRegionX() const;
    int getScreenRecordingRegionY() const;
    int getScreenRecordingRegionWidth() const;
    int getScreenRecordingRegionHeight() const;
    void setScreenRecordingRegion(int x, int y, int width, int height);

    /// Extra ffmpeg arguments, appended last so they override everything derived from settings.
    std::string const& getScreenRecordingExtraArguments() const;
    void setScreenRecordingExtraArguments(std::string arguments);

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
     * Screen recording. Names match the settings.xml keys one-for-one, and the defaults are the
     * ones a 1080p60 lecture capture wants: see Settings::loadDefault.
     */
    bool screenRecordingEnabled{};
    bool screenRecordingKeepAudioFile{};
    fs::path videoFolder;
    std::string screenRecordingFfmpegPath;
    int screenRecordingWidth{};
    int screenRecordingHeight{};
    int screenRecordingFps{};
    int screenRecordingVideoBitrate{};
    int screenRecordingAudioBitrate{};
    int screenRecordingAudioSampleRate{};
    std::string screenRecordingVideoCodec;
    std::string screenRecordingAudioCodec;
    std::string screenRecordingContainer;
    bool screenRecordingCaptureCursor{};
    int screenRecordingVideoDevice{};
    int screenRecordingAudioDevice{};
    std::string screenRecordingAudioDeviceName;
    int screenRecordingRegionX{};
    int screenRecordingRegionY{};
    int screenRecordingRegionWidth{};
    int screenRecordingRegionHeight{};
    std::string screenRecordingExtraArguments;

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
