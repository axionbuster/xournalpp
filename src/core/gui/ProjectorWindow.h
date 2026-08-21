/*
 * Xournal++
 *
 * A clean, chrome-free mirror of the current page in its own always-on-top window.
 *
 * This is the same idea as OBS's windowed projector: a second view of exactly the content that
 * matters, with none of the toolbars, scrollbars or page shadows around it, which can be parked on
 * a second display for an audience or pointed at by a screen capture. It is deliberately
 * independent of recording -- it can be opened and closed at any time, during a recording or not,
 * and doing so has no effect on what is being recorded.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>  // for size_t
#include <string>   // for string

#include <gtk/gtk.h>  // for GtkWidget, GtkWindow

#include "gui/CanvasFrame.h"         // for FrameCache
#include "model/DocumentListener.h"  // for DocumentListener
#include "model/PageRef.h"           // for PageRef

class Control;

class ProjectorWindow final: public DocumentListener {
public:
    explicit ProjectorWindow(Control* control);
    ~ProjectorWindow() override;

    ProjectorWindow(const ProjectorWindow&) = delete;
    auto operator=(const ProjectorWindow&) -> ProjectorWindow& = delete;

    /**
     * Show the window, putting it back on the monitor and at the position it was last closed on.
     * Showing an already-visible projector just raises it.
     */
    void show();

    /**
     * Hide the window, remembering where it was. The window itself is kept alive rather than
     * destroyed, so reopening is instant and never loses the remembered geometry.
     */
    void hide();

    bool isVisible() const;

    /**
     * Re-read the preferences that affect appearance and window behaviour. Called when the
     * settings dialog is applied, so changes take effect without reopening the projector.
     */
    void applySettings();

    /**
     * Tell the projector that a page has been repainted in the main view. Cheap and called often:
     * it queues a redraw only when the page in question is the one on show.
     */
    void notifyRepaint(const PageRef& page);

    /**
     * The pen moved, so the marker the frame draws at it has to move too.
     *
     * The clock below repaints a page nobody is touching four times a second, which is enough for
     * a background change nobody reported and far too slow for something following a hand. Called
     * once per motion event and does nothing but set a flag; the clock still decides when the
     * repaint happens, so the rate stays capped whatever the tablet's report rate is.
     */
    void notifyPointerMoved();

    /**
     * A finished stroke was just drawn into the main view's buffer; draw it into the kept page
     * picture too, so it never flickers out of the projector. See FrameCache::drawSettled.
     */
    void onToolViewSettled(const PageRef& page, const xoj::view::ToolView* v);

    /**
     * Remember the current monitor, position and size. Called on hide and at shutdown, while the
     * window still exists.
     */
    void saveGeometry();

    // DocumentListener
    void documentChanged(DocumentChangeType type) override;
    void pageSizeChanged(size_t page) override;
    void pageChanged(size_t page) override;
    void pageInserted(size_t page) override;
    void pageDeleted(size_t page) override;
    void pageSelected(size_t page) override;

private:
    /**
     * Forbid macOS from merging this window into another window's tab bar. Does nothing anywhere
     * else. Must run before the window is first shown.
     */
    void applyNativeWindowTabbing();

    /// Put the window back where it was, if the monitor it was on is still connected.
    void restoreGeometry();

    /// Move the window so that gtk_window_get_position afterwards reports exactly (@p x, @p y).
    void moveTo(int x, int y);

    /// Constrain resizing to the recording's aspect ratio, or release the constraint.
    void applyAspectRatioHint();

    /**
     * Set the native window's stacking level, where the platform needs it said directly.
     * A no-op everywhere gtk_window_set_keep_above is sufficient on its own.
     */
    void applyNativeWindowLevel(bool keepAbove);

    void queueRedraw();

    /// Start and stop the clock that turns "something changed" into an actual repaint.
    void startRedrawClock();
    void stopRedrawClock();

    static gboolean onRedrawTick(gpointer data);

    static gboolean onDraw(GtkWidget* widget, cairo_t* cr, gpointer data);
    static gboolean onDeleteEvent(GtkWidget* widget, GdkEvent* event, gpointer data);

    /// Paint one page, letterboxed and centred, into a widget-sized area.
    void drawPage(cairo_t* cr, int width, int height);

    /**
     * Mark the strip along the bottom of the page that burnt-in captions will later cover, so
     * nothing worth reading gets written into it. A preview aid only -- the recording is made from
     * the screen grabber's frames, which never see anything this class draws.
     *
     * @param x,y,areaWidth,areaHeight  The page's rectangle inside the window, letterbox excluded.
     */
    void drawSafeArea(cairo_t* cr, double x, double y, double areaWidth, double areaHeight);

    /**
     * Put the frame rate in the corner of the window, the way OBS keeps it in its status bar.
     *
     * A preview aid, and only that: it is painted over the finished picture, after the page has been
     * drawn, and the encoder is fed its own frames by VideoRecorder, which never calls anything in
     * this file. What the presenter sees and what the file contains differ by this readout alone.
     */
    void drawFrameRate(cairo_t* cr, int width, int height);

    /// The reading as it should currently be shown: "REC 59.8 fps" while recording, "29.9 fps" if not.
    std::string buildFrameRateText() const;

    Control* control;

    GtkWidget* window = nullptr;
    GtkWidget* drawingArea = nullptr;

    /// Tracks GTK's own idea of visibility, so hide() can save geometry exactly once.
    bool visible = false;

    /**
     * The redraw clock, running only while the window is on screen.
     *
     * Repainting straight from every notification means repainting once per motion event, which on
     * a 120 Hz tablet is twice as often as anyone can see and each time costs a full page. The
     * clock collects those notifications and repaints at a rate a viewer can actually perceive.
     */
    guint redrawTimer = 0;

    /// Something has changed since the last repaint was asked for.
    bool needsRedraw = true;

    /// When a repaint was last asked for, so one happens now and then regardless.
    gint64 lastRedrawQueued = 0;

    /// The page as it was last drawn. See xoj::canvas::FrameCache.
    xoj::canvas::FrameCache frameCache;

    /**
     * How fast the redraw clock is really ticking, which is what the indicator shows when nothing is
     * being recorded.
     *
     * It is the clock rather than the paints that is measured, and deliberately: the projector only
     * repaints when the page has changed, so counting paints would read a few frames a second on a
     * page nobody is writing on and look like a fault. The clock ticks at a fixed rate whatever the
     * page is doing, and falls behind exactly when the user interface is too busy to keep up -- the
     * one thing the number is there to warn about.
     */
    xoj::canvas::FrameRateMeter previewMeter;

    /**
     * The indicator's text, recomputed a few times a second. Kept rather than built while painting,
     * so that a reading which has not changed does not cost a repaint of the whole window.
     */
    std::string frameRateText;
    gint64 frameRateTextAt = 0;

    /**
     * The page the last frame drew. Kept so that repaint notifications -- which arrive while the
     * document lock may be held by the caller -- can be filtered without touching the document.
     */
    PageRef shownPage;
};
