#include "RepaintHandler.h"

#include <gtk/gtk.h>  // for gtk_widget_queue_draw

#include "control/Control.h"           // for Control
#include "gui/ProjectorWindow.h"       // for ProjectorWindow
#include "gui/widgets/XournalWidget.h"  // for gtk_xournal_repaint_area

#include "PageView.h"     // for XojPageView
#include "XournalView.h"  // for XournalView

RepaintHandler::RepaintHandler(XournalView* xournal): xournal(xournal) {}

RepaintHandler::~RepaintHandler() { this->xournal = nullptr; }

/**
 * Every redraw of page content in the main view passes through this class, which makes it the one
 * place that can keep the projector in step without each tool having to know the projector exists.
 * The projector is asked Control-side rather than registered here, so that it survives the
 * XournalView being rebuilt and there is no stale pointer to clear.
 */
void RepaintHandler::notifyProjector(const XojPageView* view) const {
    if (this->xournal == nullptr) {
        return;
    }
    Control* control = this->xournal->getControl();
    if (control == nullptr) {
        return;
    }
    // peek, not get: getProjectorWindow() creates one on demand, and every repaint of every page
    // would then bring a projector into existence for a user who has never opened it.
    if (ProjectorWindow* projector = control->peekProjectorWindow(); projector != nullptr) {
        projector->notifyRepaint(view != nullptr ? view->getPage() : PageRef{});
    }
}

void RepaintHandler::repaintPage(const XojPageView* view) {
    auto p = view->getPixelPosition();
    int x2 = p.x + view->getDisplayWidth();
    int y2 = p.y + view->getDisplayHeight();
    gtk_xournal_repaint_area(this->xournal->getWidget(), p.x, p.y, x2, y2);
    notifyProjector(view);
}

void RepaintHandler::repaintPageArea(const XojPageView* view, int x1, int y1, int x2, int y2) {
    auto p = view->getPixelPosition();
    gtk_xournal_repaint_area(this->xournal->getWidget(), p.x + x1, p.y + y1, p.x + x2, p.y + y2);
    notifyProjector(view);
}

void RepaintHandler::repaintPageBorder(const XojPageView* view) {
    gtk_widget_queue_draw(this->xournal->getWidget());
    notifyProjector(view);
}
