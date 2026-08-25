/*
 * Xournal++
 *
 * The main Control
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <atomic>    // for atomic
#include <cstddef>   // for size_t
#include <cstdint>   // for uint64_t
#include <memory>    // for unique_ptr
#include <optional>  // for optional
#include <string>    // for string, allocator
#include <vector>    // for vector

#include <gdk-pixbuf/gdk-pixbuf.h>  // for GdkPixbuf
#include <gio/gio.h>                // for GApplication
#include <glib.h>                   // for guint
#include <gtk/gtk.h>                // for GtkLabel

#include "control/ToolEnums.h"                      // for ToolSize, ToolType
#include "control/jobs/ProgressListener.h"          // for ProgressListener
#include "control/settings/ViewModes.h"             // for ViewModeId
#include "control/tools/EditSelection.h"            // for OrderChange
#include "enums/Action.enum.h"                      // for Action
#include "gui/toolbarMenubar/model/ColorPalette.h"  // for ColorPalette
#include "model/DocumentHandler.h"                  // for DocumentHandler
#include "model/DocumentListener.h"                 // for DocumentListener
#include "model/GeometryTool.h"                     // for GeometryTool
#include "model/PageRef.h"                          // for PageRef
#include "undo/UndoRedoHandler.h"                   // for UndoRedoHandler (ptr only)

#include "ClipboardHandler.h"  // for ClipboardListener
#include "ToolHandler.h"       // for ToolListener
#include "filesystem.h"        // for path

class LoadHandler;
class GeometryToolController;
class AudioController;
class VideoRecorder;
class ProjectorWindow;
class FullscreenHandler;
class Sidebar;
class GladeSearchpath;
class MetadataManager;
class XournalppCursor;
class ToolbarDragDropHandler;
class MetadataEntry;
class MetadataCallbackData;
class PageBackgroundChangeController;
class PageTemplateSettings;
class PageTypeHandler;
class BaseExportJob;
class LayerController;
class PluginController;
class Document;
class EditSelection;
class Element;
class MainWindow;
class ObjectInputStream;
class ScrollHandler;
class SearchBar;
class Settings;
class TextEditor;
class XournalScheduler;
class ZoomControl;
class ToolMenuHandler;
class XojFont;
class XojPdfRectangle;
class Callback;
class ActionDatabase;

namespace xoj::view {
class ToolView;
}
class NavigationHistory;

class Control:
        public ToolListener,
        public DocumentHandler,
        public UndoRedoListener,
        public ClipboardListener,
        public ProgressListener {
public:
    Control(GApplication* gtkApp, GladeSearchpath* gladeSearchPath, bool disableAudio);
    Control(Control const&) = delete;
    Control(Control&&) = delete;
    auto operator=(Control const&) -> Control& = delete;
    auto operator=(Control&&) -> Control& = delete;
    ~Control() override;

    void initWindow(MainWindow* win);

public:
    /// Asymchronously closes the current document and replaces it by a new file
    void newFile(fs::path filepath = {});

    /// @brief Shows an open file dialog and opens the selected file after closing the previously opened file
    void askToOpenFile();
    /**
     * @brief Asynchronously opens the provided path, safely closes the current opened document and replaces it with the
     * newly parsed file. Calls callback afterwards, with boolean parameter true on success. Does nothing to this->doc
     * in case of failure at any point.
     */
    void openFile(
            fs::path filepath, std::function<void(bool)> callback = [](bool) {}, int scrollToPage = -1,
            bool forceOpen = false);
    /// Shows an open file dialog and opens the selected file
    void askToAnnotatePdf();

    /**
     * (Potentially asynchronously) Opens the given file without saving any previously opened Document. Calls callback
     * afterwards, with boolean parameter true if success. WARNING: may lead to data loss if the current Document has
     * not been saved yet.
     */
    void openFileWithoutSavingTheCurrentDocument(fs::path filepath, bool attachToDocument, int scrollToPage,
                                                 std::function<void(bool)> callback);

    void print();
    void exportAsPdf();
    void exportAs();
    void quit(bool allowCancel = true);

    /**
     * @brief Asynchronously saves the document and calls callback afterwards with boolean parameter true on success.
     * May ask the user for a place to save if necessary.
     */
    void save(std::function<void(bool)> callback = [](bool) {});
    /**
     * @brief Asks the user for a new location, asynchronously saves the document there and calls callback afterwards.
     */
    void saveAs(std::function<void(bool)> callback = [](bool) {});

    /**
     * Marks the current document as saved if it is currently marked as unsaved.
     */
    void resetSavedStatus();

    /**
     * Close the current document, prompting to save unsaved changes.
     *
     * @param callback Called after trying to close the document, with param true in case of success, false otherwise.
     * @param allowDestroy Whether clicking "Discard" should destroy the current document.
     * @param allowCancel Whether the user should be able to cancel closing the document.
     * @param forceClose Whether to skip the save dialog and unconditionally discard unsaved changes.
     * @return true if the user closed the document, otherwise false.
     */
    void close(std::function<void(bool)> callback, bool allowDestroy = false, bool allowCancel = true,
               bool forceClose = false);

    // Menu edit
    void showSettings();

    // Menu Help
    void showAbout();
    void showGtkDemo();

    /**
     * @brief Update the Cursor and the Toolbar based on the active color
     *
     */
    void toolColorChanged() override;
    /**
     * @brief Change the color of the current selection based on the active Tool
     *
     */
    void changeColorOfSelection() override;
    void toolChanged() override;
    void toolSizeChanged() override;
    void toolFillChanged() override;
    void toolLineStyleChanged() override;

    void selectTool(ToolType type);
    void selectDefaultTool();

    void fontChanged(const XojFont& font);      ///< Set the font after the user selected a font

    /**
     * Apply font preset `index` (0-based): its font goes through the same path as the font
     * dialog, its color is applied to text only — the text tool, the text being edited and
     * the selected text elements. The tool currently held (e.g. the pen) keeps its color.
     * A slot the user never saved is left alone: applying it does nothing at all.
     * Font and color change together, as a single undo step.
     */
    void applyFontPreset(size_t index);

    /**
     * Store into font preset `index` (0-based) the font and color the user currently sees:
     * those of the text element being edited, or else the default font and the text tool's color
     */
    void saveFontPreset(size_t index);

    /**
     * Apply `color` to text only: the text tool, the selected text elements and the text being
     * edited. Returns the undo action for the selected elements (null if none were recolored);
     * the caller decides whether to push it on its own or to group it.
     */
    UndoActionPtr applyTextColor(Color color);

    /**
     * The body of fontChanged(), returning the undo action for the selected elements instead of
     * pushing it (null if no text element was in the selection)
     */
    UndoActionPtr changeFont(const XojFont& font);

    void updatePageNumbers(size_t page, size_t pdfPage);

    /**
     * Save current state (selected tool etc.)
     */
    void saveSettings();

    void updateWindowTitle();
    void setViewPairedPages(bool enabled);
    void setViewFullscreenMode(bool enabled);
    void setViewPresentationMode(bool enabled);
    void setPairsOffset(int numOffset);
    void setViewColumns(int numColumns);
    void setViewRows(int numRows);
    void setViewLayoutVert(bool vert);
    void setViewLayoutR2L(bool r2l);
    void setViewLayoutB2T(bool b2t);

    void manageToolbars();
    void customizeToolbars();
    void setFullscreen(bool enabled);
    void setShowSidebar(bool enabled);
    void setShowToolbar(bool enabled);
    void setShowMenubar(bool enabled);

    void gotoPage();

    void setToolDrawingType(DrawingType type);

    void paperTemplate();
    void paperFormat();
    void changePageBackgroundColor();
    void updateBackgroundSizeButton();

    /**
     * Loads the view mode (hide/show menu-,tool-&sidebar)
     */
    bool loadViewMode(ViewModeId mode);

    /**
     * @brief Search text on the given page. The matches (if any) are stored in the XojPageView::SearchControl instance.
     * @param occurrences If not nullptr, the pointed variable will contain the number of matches on the page
     * @param matchRect If not nullptr, will contain the topleft point of the first match on the page
     *                          (Used for scrolling to the first match)
     * @return true if at least one match was found
     */
    bool searchTextOnPage(const std::string& text, size_t pageNumber, size_t index, size_t* occurrences,
                          XojPdfRectangle* matchRect);

    /**
     * Fire page selected, but first check if the page Number is valid
     *
     * @return the page ID or size_t_npos if the page is not found
     */
    size_t firePageSelected(const PageRef& page);
    void firePageSelected(size_t page);

    void addDefaultPage(const std::optional<PageTemplateSettings>& pageTemplate, Document* doc = nullptr);
    void duplicatePage();
    void insertNewPage(size_t position, bool automatedInsertion = false);
    void appendNewPdfPages();
    void insertPage(const PageRef& page, size_t position, bool shouldScrollToPage = true);
    void deletePage();
    void movePageTowardsBeginning();
    void movePageTowardsEnd();

    /**
     * Ask the user whether a page with the given id
     * should be added to the document.
     */
    void askInsertPdfPage(size_t pdfPage);

    /**
     * Disable / enable page action buttons
     */
    void updatePageActions();

    /**
     * Get the navigation history handler.
     */
    NavigationHistory* getNavigationHistory() const;

    // selection handling
    void clearSelection();

    void moveSelectionToLayer(size_t layerNo);

    void setCopyCutEnabled(bool enabled);

    void enableAutosave(bool enable);

    void clearSelectionEndText();

    void selectAllOnPage();

    void reorderSelection(EditSelection::OrderChange change);

    /**
     * Toggle the editor-only flag of the current selection: elements that are editor-only
     * stay visible (faded) on the canvas but are left out of recordings, the projector,
     * exports and prints. Does nothing without a selection.
     */
    void toggleSelectionEditorOnly();

    void setToolSize(ToolSize size);

    /**
     * Change the line style of the PEN if select, or of selected elements if any
     * Otherwise, select the PEN tool and set its linestyle
     */
    void setLineStyle(const std::string& style);

    /**
     * Change the eraser type. If the eraser is not selected, select it as well.
     */
    void setEraserType(EraserType type);

    void setFill(bool fill);

    TextEditor* getTextEditor();

    GladeSearchpath* getGladeSearchPath() const;

    void disableSidebarTmp(bool disabled);

    XournalScheduler* getScheduler() const;

    void block(const std::string& name);
    void unblock();

    void setLastAutosaveFile(fs::path newAutosaveFile);
    void deleteLastAutosaveFile();
    void setClipboardHandlerSelection(EditSelection* selection);

    void addChangedDocumentListener(DocumentListener* dl);
    void removeChangedDocumentListener(DocumentListener* dl);

    MetadataManager* getMetadataManager() const;
    Settings* getSettings() const;
    ToolHandler* getToolHandler() const;
    ZoomControl* getZoomControl() const;
    Document* getDocument() const;
    UndoRedoHandler* getUndoRedoHandler() const;
    MainWindow* getWindow() const;
    GtkWindow* getGtkWindow() const;
    ScrollHandler* getScrollHandler() const;
    PageRef getCurrentPage();
    size_t getCurrentPageNo() const;
    XournalppCursor* getCursor() const;
    Sidebar* getSidebar() const;
    SearchBar* getSearchBar() const;
    AudioController* getAudioController() const;
    VideoRecorder* getVideoRecorder() const;

    /**
     * The projector window, created lazily the first time it is asked for. Never null.
     *
     * Callers on hot paths (RepaintHandler) want peekProjectorWindow instead, which returns null
     * rather than bringing one into existence.
     */
    ProjectorWindow* getProjectorWindow();

    /// The projector window if one has been created, otherwise null. Cheap; never allocates.
    ProjectorWindow* peekProjectorWindow() const;

    /// Show or hide the projector. Has no effect on any recording in progress, in either direction.
    void setProjectorVisible(bool visible);

    /**
     * Start a recording: the sound file that strokes are timestamped against, the screen capture,
     * or both, depending on the preferences.
     *
     * @param error filled with a user-facing reason when this returns false.
     */
    bool startRecording(std::string* error);

    /// Stop whatever startRecording started. Returns true unless the recording could not be ended.
    bool stopRecording();

    bool isRecording() const;

    /**
     * When the recording in progress started, on g_get_monotonic_time()'s clock, or 0 when nothing
     * is being recorded. Kept here rather than in the toolbar button so that a button built partway
     * through a recording -- after the toolbars are reconfigured, say -- still shows the real
     * elapsed time.
     */
    gint64 getRecordingStartTime() const;

    /**
     * How fast the video recording is really going, in frames per second, and the rate it is aiming
     * for. Both 0 when no video is being recorded.
     *
     * New frames and the clock that asks for them are serviced on this thread, so the first number
     * falling below the second says the user interface is not keeping up -- with the pen as much as
     * with the recording. That is worth having in front of you while there is still time to close
     * something, which is why both the projector and the record button can show it. See
     * VideoRecorder::getRenderRate.
     */
    double getVideoFrameRate() const;
    int getVideoTargetFrameRate() const;

    /// How fast frames are reaching the encoder. See VideoRecorder::getOutputRate.
    double getVideoOutputFrameRate() const;

    /**
     * A counter that changes whenever the settled content of a page does -- a stroke finished, an
     * undo, a background swapped, a layer hidden -- and stays put while a stroke is merely being
     * drawn, since ink under the pen is an overlay and not yet part of any page.
     *
     * This is what lets the projector and the recorder draw a page once and keep the picture: both
     * of them redraw many times a second, and re-rendering every stroke of a full page each time is
     * what an hour-long recording cannot afford. See xoj::canvas::FrameCache.
     */
    std::uint64_t getCanvasRevision() const;

    /// Say that the settled content of a page has changed. Cheap; safe from any thread.
    void bumpCanvasRevision();

    /**
     * Changes whenever a live canvas frame may differ even though settled page content does not:
     * pointer motion, ink still under the pen, selections, laser/geometry overlays and repaints.
     * The video recorder coalesces multiple changes by sampling this counter on its next idle tick.
     */
    std::uint64_t getLiveFrameGeneration() const;

    /// Say that live canvas pixels may have changed. Cheap; safe from any thread.
    void bumpLiveFrameGeneration();

    /**
     * A selection has taken elements out of the document model, or returned them to it.
     *
     * Frame caches must not bridge that ownership hand-off: a cached settled page may otherwise
     * draw the selected elements underneath the live selection, or briefly omit them after the
     * selection is finalized. This invalidates both clean-frame consumers at the transition only;
     * moving an established selection remains an inexpensive overlay repaint.
     */
    void selectionStateChanged();

    /**
     * A tool view has just been drawn into the main view's page buffer -- a finished stroke,
     * mostly. Patches the projector's and the recorder's kept pictures the same way, so the
     * stroke never flickers out of them, then bumps the canvas revision so their background
     * reconcile still runs. Called by XojPageView before the overlay is deleted.
     */
    void toolViewSettled(const PageRef& page, const xoj::view::ToolView* v);


    PageTypeHandler* getPageTypes() const;
    PageBackgroundChangeController* getPageBackgroundChangeController() const;
    LayerController* getLayerController() const;
    PluginController* getPluginController() const;
    const Palette& getPalette() const;

    /**
     * Show floating toolbox at specified coordinates
     * @param x x coordinate relative to main window
     * @param y y coordinate relative to main window
     */
    void showFloatingToolbox(int x, int y);

    bool copy();
    bool cut();
    bool paste();

    void help();

    void selectAlpha(OpacityFeature feature);

    /**
     * @brief Initialize the all button tools based on the respective ButtonConfigs
     *
     */
    void initButtonTool();


public:
    // UndoRedoListener interface
    void undoRedoChanged() override;
    void undoRedoPageChanged(PageRef page) override;

public:
    // ProgressListener interface
    void setMaximumState(size_t max) override;
    void setCurrentState(size_t state) override;

public:
    // ClipboardListener interface
    void clipboardCutCopyEnabled(bool enabled) override;
    void clipboardPasteEnabled(bool enabled) override;
    void clipboardPasteText(std::string text) override;
    void clipboardPasteImage(GdkPixbuf* img) override;
    void clipboardPasteXournal(ObjectInputStream& in) override;
    void deleteSelection() override;

    void clipboardPaste(ElementPtr e);

public:
    void registerPluginToolButtons(ToolMenuHandler* toolMenuHandler);
    inline ActionDatabase* getActionDatabase() const { return actionDB.get(); }
    void loadPaletteFromSettings();

protected:
    void setRotationSnapping(bool enable);
    void setGridSnapping(bool enable);

    void showFontDialog();
    void showColorChooserDialog();

    void fileLoaded(int scrollToPage = -1);

    void eraserSizeChanged();
    void penSizeChanged();
    void highlighterSizeChanged();

    /// Note that a recording has begun or ended: the elapsed-time clock and the stop button.
    void recordingStateChanged(bool recording);

    static bool checkChangedDocument(Control* control);
    static bool autosaveCallback(Control* control);

    /**
     * Load metadata later, md will be deleted
     */
    void loadMetadata(MetadataEntry md);

    static bool loadMetadataCallback(MetadataCallbackData* data);

    void saveImpl(bool saveAs, std::function<void(bool)> callback);

private:
    /**
     * @brief Creates the specified geometric tool if it's not on the current page yet. Deletes it if it already exists.
     * @return true if a geometric tool was created
     */
    template <class ToolClass, class ViewClass, class ControllerClass, class InputHandlerClass,
              GeometryToolType toolType>
    bool toggleGeometryTool();
    void resetGeometryTool();

    /**
     * @brief Creates a compass if it's not on the current page yet. Deletes it if it already exists.
     * @return true if a compass was created
     */
    bool toggleCompass();
    /**
     * @brief Creates a setsquare if it's not on the current page yet. Deletes it if it already exists.
     * @return true if a setsquare was created
     */
    bool toggleSetsquare();

    struct MissingPdfData;
    /**
     * Prompt the user that the PDF background is missing and offer solution options
     */
    void promptMissingPdf(MissingPdfData& missingPdf, const fs::path& filepath);

    /**
     * Handle the response from the missing PDF dialog
     */
    void missingPdfDialogResponseHandler(fs::path proposedPdfFilepath, int responseId);

    /**
     * "Closes" the document, preparing the editor for a new document.
     */
    void closeDocument();

    /**
     * Forcibly replaces the opened document.
     * WARNING: Be sure the active document has been saved (or discarded) before calling replaceDocument()
     */
    void replaceDocument(std::unique_ptr<Document> doc, int scrollToPage);

    /**
     * Asynchronously opens the provided path and parse it as a .xopp or .xoj file. Then forcibly replaces the currently
     * opened document with the new one. Asks the user in case of doubts (e.g. wrong file version) and aborts if the
     * document was not correctly and entirely parsed. Calls callback afterwards, with boolean parameter true on
     * success. WARNING: Be sure the active document has been saved (or discarded) before calling openXoppFile()
     */
    void openXoppFile(fs::path filepath, int scrollToPage, std::function<void(bool)> callback);

    /**
     * Opens the provided path and parse it as a PDF file. Then forcibly replaces the currently opened document with a
     * new one based on the PDF. WARNING: Be sure the active document has been saved (or discarded) before calling
     * openPdfFile()
     *
     * @return true on success
     */
    bool openPdfFile(fs::path filepath, bool attachToDocument, int scrollToPage);

    /**
     * Opens the provided path and parse it as a PNG file. Then forcibly replaces the currently opened document with a
     * new one based on the PNG. WARNING: Be sure the active document has been saved (or discarded) before calling
     * openPngFile()
     *
     * @return true on success
     */
    bool openPngFile(fs::path filepath, bool attachToDocument, int scrollToPage);

    /**
     * Opens the provided path and parse it as a .xopt template  file. Then forcibly replaces the currently opened
     * document with a new one based on the template. WARNING: Be sure the active document has been saved (or discarded)
     * before calling openXoptFile()
     *
     * @return true on success
     */
    bool openXoptFile(fs::path filepath);

    /**
     * @brief Get the pen line style to select in the toolbar
     *
     * @return style to select, empty if no style should be selected (active
     * selection with differing line styles)
     */
    auto getLineStyleToSelect() -> std::optional<std::string> const;

    UndoRedoHandler* undoRedo = nullptr;
    ZoomControl* zoom = nullptr;

    Settings* settings = nullptr;
    std::unique_ptr<Palette> palette;
    MainWindow* win = nullptr;

    Document* doc = nullptr;

    Sidebar* sidebar = nullptr;
    SearchBar* searchBar = nullptr;

    ToolHandler* toolHandler;

    ScrollHandler* scrollHandler;

    ToolbarDragDropHandler* dragDropHandler = nullptr;

    GApplication* gtkApp = nullptr;

    /**
     * The cursor handler
     */
    XournalppCursor* cursor;

    /**
     * Timeout id: the timeout watches the changes and actualizes the previews from time to time
     */
    guint changeTimout;

    /**
     * The pages wihch has changed since the last update (for preview update)
     */
    std::vector<PageRef> changedPages;

    /**
     * DocumentListener instances that are to be updated by checkChangedDocument.
     */
    std::list<DocumentListener*> changedDocumentListeners;

    /**
     * Our clipboard abstraction
     */
    ClipboardHandler* clipboardHandler = nullptr;

    /**
     * The autosave handler ID
     */
    guint autosaveTimeout = 0;
    fs::path lastAutosaveFilename;

    XournalScheduler* scheduler;

    /**
     * State / Blocking attributes
     */
    GtkWidget* statusbar = nullptr;
    GtkLabel* lbState = nullptr;
    GtkProgressBar* pgState = nullptr;
    size_t maxState = 0;
    bool isBlocking;

    GladeSearchpath* gladeSearchPath;

    MetadataManager* metadata;

    PageTypeHandler* pageTypes;

    std::unique_ptr<PageBackgroundChangeController> pageBackgroundChangeController;

    LayerController* layerController;

    std::unique_ptr<GeometryTool> geometryTool;
    std::unique_ptr<GeometryToolController> geometryToolController;

    std::unique_ptr<NavigationHistory> navHistory;

    /**
     * Manage all Xournal++ plugins
     */
    PluginController* pluginController;

    std::unique_ptr<ActionDatabase> actionDB;
    template <Action a>
    friend struct ActionProperties;

    // Keep after the ActionDatabase so it is destroyed first: ~AudioController refers to the ActionDatabase
    std::unique_ptr<AudioController> audioController;

    /**
     * Screen capture. Always present, even in a build without audio support: it runs an external
     * ffmpeg process and shares nothing with the PortAudio pipeline.
     */
    std::unique_ptr<VideoRecorder> videoRecorder;

    /// g_get_monotonic_time() at the moment the current recording started; 0 when idle.
    gint64 recordingStartTime = 0;

    /// See getCanvasRevision(). Atomic because a render job may finish on a worker thread.
    std::atomic<std::uint64_t> canvasRevision{1};

    /// See getLiveFrameGeneration(). Repaint requests and pointer events advance it on the UI thread.
    std::atomic<std::uint64_t> liveFrameGeneration{1};

    /**
     * Created the first time the projector is opened and then kept, so that closing and reopening
     * it is instant and the remembered geometry is never lost mid-session. Destroyed before the
     * ActionDatabase, whose state it updates when the window is closed from its title bar.
     */
    std::unique_ptr<ProjectorWindow> projectorWindow;
};
