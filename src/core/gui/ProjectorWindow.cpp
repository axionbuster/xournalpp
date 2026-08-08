// Table of contents
//   1. Construction ............. window, drawing area, signal wiring
//   2. Show / hide .............. and the geometry that survives both
//   3. Geometry memory .......... saveGeometry / restoreGeometry / aspect ratio hint
//   4. Painting ................. onDraw / drawPage
//   5. Change notifications ..... DocumentListener and the repaint hook

#include "ProjectorWindow.h"

#include <algorithm>     // for clamp, max, min
#include <shared_mutex>  // for shared_lock
#include <string>        // for string

#include "control/Control.h"                     // for Control
#include "control/actions/ActionDatabase.h"  // for ActionDatabase
#include "control/settings/Settings.h"  // for Settings
#include "gui/MainWindow.h"             // for MainWindow
#include "gui/PageView.h"               // for XojPageView
#include "gui/XournalView.h"            // for XournalView
#include "model/Document.h"                      // for Document
#include "model/XojPage.h"                       // for XojPage
#include "util/Color.h"                 // for cairo_set_source_rgbi
#include "util/i18n.h"                  // for _
#include "view/DocumentView.h"          // for DocumentView

// ===========================================================================================
// 1. Construction
// ===========================================================================================

ProjectorWindow::ProjectorWindow(Control* control): control(control) {
    this->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(this->window), _("Projector"));
    gtk_window_set_default_size(GTK_WINDOW(this->window), 960, 540);

    // Not transient for the main window on purpose. A transient window is tied to its parent's
    // stacking and workspace, which is exactly wrong for something meant to sit on a second
    // display in front of whatever else is there.
    gtk_window_set_destroy_with_parent(GTK_WINDOW(this->window), FALSE);

    this->drawingArea = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(this->window), this->drawingArea);
    gtk_widget_show(this->drawingArea);

    g_signal_connect(this->drawingArea, "draw", G_CALLBACK(&ProjectorWindow::onDraw), this);
    g_signal_connect(this->window, "delete-event", G_CALLBACK(&ProjectorWindow::onDeleteEvent), this);

    this->registerListener(control);
    applySettings();
}

ProjectorWindow::~ProjectorWindow() {
    this->unregisterListener();

    if (this->window != nullptr) {
        if (this->visible) {
            saveGeometry();
        }
        // Drop our handlers first: destroying the window emits "delete-event" on some backends,
        // and the callback would run against a half-destroyed ProjectorWindow.
        g_signal_handlers_disconnect_by_data(this->window, this);
        g_signal_handlers_disconnect_by_data(this->drawingArea, this);
        gtk_widget_destroy(this->window);
        this->window = nullptr;
        this->drawingArea = nullptr;
    }
}

void ProjectorWindow::applySettings() {
    Settings* settings = control->getSettings();

    gtk_window_set_keep_above(GTK_WINDOW(this->window), settings->isProjectorKeepAbove() ? TRUE : FALSE);
    applyAspectRatioHint();
    queueRedraw();
}

// ===========================================================================================
// 2. Show / hide
// ===========================================================================================

void ProjectorWindow::show() {
    if (this->visible) {
        gtk_window_present(GTK_WINDOW(this->window));
        return;
    }

    applySettings();
    gtk_widget_show_all(this->window);
    this->visible = true;

    // After the window is on screen: gtk_window_move on an unrealized window is advisory at best,
    // and the size has to be known before the position can be clamped onto the monitor.
    restoreGeometry();
    gtk_window_present(GTK_WINDOW(this->window));
}

void ProjectorWindow::hide() {
    if (!this->visible) {
        return;
    }

    saveGeometry();
    gtk_widget_hide(this->window);
    this->visible = false;
}

auto ProjectorWindow::isVisible() const -> bool { return this->visible; }

auto ProjectorWindow::onDeleteEvent(GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
    auto* self = static_cast<ProjectorWindow*>(data);

    // Closing the projector is a view change and nothing else. Any recording in progress is run by
    // a separate process capturing the screen and never learns this happened.
    self->hide();
    self->control->getActionDatabase()->setActionState(Action::PROJECTOR_WINDOW, false);

    return TRUE;  // handled: do not let GTK destroy the window
}

// ===========================================================================================
// 3. Geometry memory
// ===========================================================================================

void ProjectorWindow::saveGeometry() {
    if (this->window == nullptr || !gtk_widget_get_realized(this->window)) {
        return;
    }

    GdkWindow* gdkWindow = gtk_widget_get_window(this->window);
    if (gdkWindow == nullptr) {
        return;
    }

    GdkDisplay* display = gtk_widget_get_display(this->window);
    GdkMonitor* monitor = gdk_display_get_monitor_at_window(display, gdkWindow);
    if (monitor == nullptr) {
        return;
    }

    GdkRectangle monitorGeometry{};
    gdk_monitor_get_geometry(monitor, &monitorGeometry);

    gint x = 0;
    gint y = 0;
    gint width = 0;
    gint height = 0;
    gtk_window_get_position(GTK_WINDOW(this->window), &x, &y);
    gtk_window_get_size(GTK_WINDOW(this->window), &width, &height);

    // Stored relative to the monitor's origin, and with the monitor identified by description
    // rather than index, for the same reason the main window is: indices are reassigned whenever a
    // display is plugged in, so an index restores onto the wrong panel exactly when it matters.
    control->getSettings()->setProjectorGeometry(x - monitorGeometry.x, y - monitorGeometry.y, width, height,
                                                 Settings::describeMonitor(monitor));
}

void ProjectorWindow::restoreGeometry() {
    Settings* settings = control->getSettings();

    const int width = settings->getProjectorWidth();
    const int height = settings->getProjectorHeight();
    if (width > 0 && height > 0) {
        gtk_window_resize(GTK_WINDOW(this->window), width, height);
    }

    const std::string& wanted = settings->getProjectorMonitor();
    if (wanted.empty()) {
        return;
    }

    GdkDisplay* display = gtk_widget_get_display(this->window);
    GdkMonitor* match = nullptr;
    const int monitorCount = gdk_display_get_n_monitors(display);
    for (int i = 0; i < monitorCount; i++) {
        GdkMonitor* candidate = gdk_display_get_monitor(display, i);
        if (Settings::describeMonitor(candidate) == wanted) {
            match = candidate;
            break;
        }
    }

    if (match == nullptr) {
        g_message("Projector: monitor \"%s\" is not connected, using default placement", wanted.c_str());
        return;
    }

    GdkRectangle workarea{};
    gdk_monitor_get_workarea(match, &workarea);

    gint currentWidth = 0;
    gint currentHeight = 0;
    gtk_window_get_size(GTK_WINDOW(this->window), &currentWidth, &currentHeight);

    // Clamp onto the monitor, so a projector remembered from a larger display still comes back
    // fully on screen and can be reached by its title bar.
    const int maxX = workarea.x + std::max(0, workarea.width - currentWidth);
    const int maxY = workarea.y + std::max(0, workarea.height - currentHeight);
    const int x = std::clamp(workarea.x + settings->getProjectorPosX(), workarea.x, maxX);
    const int y = std::clamp(workarea.y + settings->getProjectorPosY(), workarea.y, maxY);

    gtk_window_move(GTK_WINDOW(this->window), x, y);
}

void ProjectorWindow::applyAspectRatioHint() {
    Settings* settings = control->getSettings();

    GdkGeometry geometry{};
    if (settings->isProjectorLockAspectRatio()) {
        const int width = std::max(1, settings->getScreenRecordingWidth());
        const int height = std::max(1, settings->getScreenRecordingHeight());
        geometry.min_aspect = static_cast<gdouble>(width) / height;
        geometry.max_aspect = geometry.min_aspect;
        gtk_window_set_geometry_hints(GTK_WINDOW(this->window), nullptr, &geometry, GDK_HINT_ASPECT);
    } else {
        gtk_window_set_geometry_hints(GTK_WINDOW(this->window), nullptr, &geometry, static_cast<GdkWindowHints>(0));
    }
}

// ===========================================================================================
// 4. Painting
// ===========================================================================================

auto ProjectorWindow::onDraw(GtkWidget* widget, cairo_t* cr, gpointer data) -> gboolean {
    auto* self = static_cast<ProjectorWindow*>(data);

    GtkAllocation allocation{};
    gtk_widget_get_allocation(widget, &allocation);
    self->drawPage(cr, allocation.width, allocation.height);

    return TRUE;
}

void ProjectorWindow::drawPage(cairo_t* cr, int width, int height) {
    Settings* settings = control->getSettings();

    Util::cairo_set_source_rgbi(cr, settings->getProjectorBackgroundColor());
    cairo_paint(cr);

    Document* doc = control->getDocument();
    if (doc == nullptr) {
        return;
    }

    const size_t pageNo = control->getCurrentPageNo();
    XournalView* xournal = control->getWindow() != nullptr ? control->getWindow()->getXournal() : nullptr;

    {
        // A SHARED lock, taken exactly once for the whole render, as RenderJob does. Document's
        // lock is a shared_mutex and is not reentrant, so taking it twice on this thread -- once to
        // find the page and again to draw it -- would deadlock the UI.
        std::shared_lock<Document> lock(*doc);

        if (pageNo == npos || pageNo >= doc->getPageCount()) {
            return;
        }
        const PageRef page = doc->getPage(pageNo);
        if (!page) {
            return;
        }
        this->shownPage = page;

        const double pageWidth = page->getWidth();
        const double pageHeight = page->getHeight();
        if (pageWidth <= 0 || pageHeight <= 0) {
            return;
        }

        const double scale = std::min(width / pageWidth, height / pageHeight);
        cairo_save(cr);
        cairo_translate(cr, (width - pageWidth * scale) / 2.0, (height - pageHeight * scale) / 2.0);
        cairo_scale(cr, scale, scale);
        cairo_rectangle(cr, 0, 0, pageWidth, pageHeight);
        cairo_clip(cr);

        // Rendered from the model rather than copied from the main view's buffer, so the projector
        // is sharp at its own size instead of an upscale of whatever zoom the main window happens
        // to be at. The cost is a full page render per frame; it is only paid while the projector
        // is open and something is actually changing, since nothing queues a redraw otherwise.
        DocumentView documentView;
        documentView.setMarkAudioStroke(false);
        if (xournal != nullptr) {
            // Without this a PDF background renders as blank white. The cache is shared with the
            // main view rather than duplicated: it guards itself with its own mutex, and a second
            // copy would double the memory a large PDF costs.
            documentView.setPdfCache(xournal->getCache());
        }
        documentView.drawPage(page, cr, false);

        // The stroke currently under the pen does not exist in the model yet -- it lives in the
        // main view's overlays. Drawing them too is what makes the projector show ink as it is
        // written rather than a beat behind, and it brings the selection, laser pointer and
        // geometry tools across as well.
        if (XojPageView* pageView = xournal != nullptr ? xournal->getViewFor(pageNo) : nullptr;
            pageView != nullptr && pageView->getPage() == page) {
            pageView->drawOverlays(cr);
        }

        cairo_restore(cr);
    }

    if (settings->isProjectorShowSafeArea()) {
        // A band across the bottom marking where burnt-in captions would sit, so nothing important
        // gets written underneath them. Translucent, and never on by default: the projector is
        // usually within the recorded area, and a guide that ends up in the recording is worse than
        // no guide at all.
        const double bandHeight = height * 0.12;
        cairo_set_source_rgba(cr, 0.65, 0.13, 0.11, 0.55);
        cairo_rectangle(cr, 0, height - bandHeight, width, bandHeight);
        cairo_fill(cr);
    }
}

void ProjectorWindow::queueRedraw() {
    if (this->visible && this->drawingArea != nullptr) {
        gtk_widget_queue_draw(this->drawingArea);
    }
}

// ===========================================================================================
// 5. Change notifications
// ===========================================================================================

void ProjectorWindow::notifyRepaint(const PageRef& page) {
    if (!this->visible) {
        return;
    }
    // Compared against the page the last frame actually drew, rather than asking the document what
    // the current page is. This runs once per motion event while a stroke is being drawn, and the
    // document lock may well be held by the caller -- so it must not touch the document at all.
    if (page && this->shownPage && page != this->shownPage) {
        return;
    }
    queueRedraw();
}

void ProjectorWindow::documentChanged(DocumentChangeType) { queueRedraw(); }
void ProjectorWindow::pageSizeChanged(size_t) { queueRedraw(); }
void ProjectorWindow::pageChanged(size_t) { queueRedraw(); }
void ProjectorWindow::pageInserted(size_t) { queueRedraw(); }
void ProjectorWindow::pageDeleted(size_t) { queueRedraw(); }
void ProjectorWindow::pageSelected(size_t) { queueRedraw(); }
