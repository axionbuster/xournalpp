#include "CanvasFrame.h"

#include <algorithm>     // for min
#include <shared_mutex>  // for shared_lock

#include "control/Control.h"    // for Control
#include "gui/MainWindow.h"     // for MainWindow
#include "gui/PageView.h"       // for XojPageView
#include "gui/XournalView.h"    // for XournalView
#include "model/Document.h"     // for Document
#include "model/XojPage.h"      // for XojPage
#include "util/Color.h"         // for cairo_set_source_rgbi
#include "view/DocumentView.h"  // for DocumentView

namespace xoj::canvas {

auto drawCurrentPage(Control* control, cairo_t* cr, double width, double height, Color background) -> FrameLayout {
    Util::cairo_set_source_rgbi(cr, background);
    cairo_paint(cr);

    FrameLayout layout;
    if (control == nullptr || width <= 0.0 || height <= 0.0) {
        return layout;
    }

    Document* doc = control->getDocument();
    if (doc == nullptr) {
        return layout;
    }

    const size_t pageNo = control->getCurrentPageNo();
    XournalView* xournal = control->getWindow() != nullptr ? control->getWindow()->getXournal() : nullptr;

    // A SHARED lock, taken exactly once for the whole render, as RenderJob does. Document's lock is
    // a shared_mutex and is not reentrant, so taking it twice on this thread -- once to find the
    // page and again to draw it -- would deadlock.
    std::shared_lock<Document> lock(*doc);

    if (pageNo == npos || pageNo >= doc->getPageCount()) {
        return layout;
    }
    const PageRef page = doc->getPage(pageNo);
    if (!page) {
        return layout;
    }

    layout.page = page;

    const double pageWidth = page->getWidth();
    const double pageHeight = page->getHeight();
    if (pageWidth <= 0 || pageHeight <= 0) {
        return layout;
    }

    const double scale = std::min(width / pageWidth, height / pageHeight);
    layout.width = pageWidth * scale;
    layout.height = pageHeight * scale;
    layout.x = (width - layout.width) / 2.0;
    layout.y = (height - layout.height) / 2.0;

    cairo_save(cr);
    cairo_translate(cr, layout.x, layout.y);
    cairo_scale(cr, scale, scale);
    cairo_rectangle(cr, 0, 0, pageWidth, pageHeight);
    cairo_clip(cr);

    DocumentView documentView;
    documentView.setMarkAudioStroke(false);
    if (xournal != nullptr) {
        // Without this a PDF background renders as blank white. The cache is shared with the main
        // view rather than duplicated: it guards itself with its own mutex, and a second copy would
        // double the memory a large PDF costs.
        documentView.setPdfCache(xournal->getCache());
    }
    documentView.drawPage(page, cr, false);

    // The stroke currently under the pen does not exist in the model yet -- it lives in the main
    // view's overlays. Drawing them too brings the selection, laser pointer and geometry tools
    // across as well.
    if (XojPageView* pageView = xournal != nullptr ? xournal->getViewFor(pageNo) : nullptr;
        pageView != nullptr && pageView->getPage() == page) {
        pageView->drawOverlays(cr);
    }

    cairo_restore(cr);
    return layout;
}

}  // namespace xoj::canvas
