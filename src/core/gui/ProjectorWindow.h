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

#include <gtk/gtk.h>  // for GtkWidget, GtkWindow

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
    /// Put the window back where it was, if the monitor it was on is still connected.
    void restoreGeometry();

    /// Constrain resizing to the recording's aspect ratio, or release the constraint.
    void applyAspectRatioHint();

    /**
     * Set the native window's stacking level, where the platform needs it said directly.
     * A no-op everywhere gtk_window_set_keep_above is sufficient on its own.
     */
    void applyNativeWindowLevel(bool keepAbove);

    void queueRedraw();


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

    Control* control;

    GtkWidget* window = nullptr;
    GtkWidget* drawingArea = nullptr;

    /// Tracks GTK's own idea of visibility, so hide() can save geometry exactly once.
    bool visible = false;

    /**
     * The page the last frame drew. Kept so that repaint notifications -- which arrive while the
     * document lock may be held by the caller -- can be filtered without touching the document.
     */
    PageRef shownPage;
};
