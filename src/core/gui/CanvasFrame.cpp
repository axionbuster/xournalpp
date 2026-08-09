#include "CanvasFrame.h"

#include <algorithm>     // for max, min
#include <cmath>         // for ceil, floor
#include <shared_mutex>  // for shared_lock

#include "control/Control.h"    // for Control
#include "gui/MainWindow.h"     // for MainWindow
#include "gui/PageView.h"       // for XojPageView
#include "gui/XournalView.h"    // for XournalView
#include "model/Document.h"     // for Document
#include "model/XojPage.h"      // for XojPage
#include "util/Color.h"         // for cairo_set_source_rgbi
#include "util/safe_casts.h"    // for ceil_cast, floor_cast
#include "view/DocumentView.h"  // for DocumentView

namespace xoj::canvas {

namespace {

/**
 * How long a kept picture is used before the page is drawn again regardless.
 *
 * Every route a change is known to take reports itself, and this is the answer to the ones nobody
 * has thought of: a plugin, a tool added later, a corner of the model that repaints by some path of
 * its own. A recording that quietly stops following the page is the one failure worth spending a
 * few frames a second to rule out, and a quarter of a second is short enough that nothing is ever
 * seen to be stale.
 */
constexpr gint64 MAX_CACHE_AGE = 250 * 1000;  // microseconds

/// Draw the page's settled content -- everything except ink still under the pen -- at scale 1.
void renderPageContent(Control* control, const PageRef& page, cairo_t* cr) {
    XournalView* xournal = control->getWindow() != nullptr ? control->getWindow()->getXournal() : nullptr;

    DocumentView documentView;
    documentView.setMarkAudioStroke(false);
    if (xournal != nullptr) {
        // Without this a PDF background renders as blank white. The cache is shared with the main
        // view rather than duplicated: it guards itself with its own mutex, and a second copy would
        // double the memory a large PDF costs.
        documentView.setPdfCache(xournal->getCache());
    }
    documentView.drawPage(page, cr, false);
}

}  // namespace

void FrameCache::invalidate() {
    this->surface.reset();
    this->page = PageRef{};
}

auto drawCurrentPage(Control* control, cairo_t* cr, double width, double height, Color background, FrameCache* cache)
        -> FrameLayout {
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
    // Whole pixels, so that a kept picture is blitted rather than resampled onto a half-pixel
    // offset -- which costs both sharpness and time. The letterbox absorbs the half pixel.
    layout.x = std::floor((width - layout.width) / 2.0);
    layout.y = std::floor((height - layout.height) / 2.0);

    // Rounded up, so the picture covers the page rather than falling a fraction of a pixel short.
    const int pagePixelWidth = std::max(1, ceil_cast<int>(layout.width));
    const int pagePixelHeight = std::max(1, ceil_cast<int>(layout.height));

    if (cache != nullptr) {
        const gint64 now = g_get_monotonic_time();
        const std::uint64_t revision = control->getCanvasRevision();

        if (!cache->surface || cache->width != pagePixelWidth || cache->height != pagePixelHeight) {
            cache->surface.reset(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pagePixelWidth, pagePixelHeight),
                                 xoj::util::adopt);
            cache->width = pagePixelWidth;
            cache->height = pagePixelHeight;
            cache->revision = revision - 1;  // whatever is in the new surface, it is not this page
        }

        if (cache->page != page || cache->revision != revision || now - cache->renderedAt >= MAX_CACHE_AGE) {
            cairo_t* into = cairo_create(cache->surface.get());
            cairo_set_operator(into, CAIRO_OPERATOR_CLEAR);
            cairo_paint(into);
            cairo_set_operator(into, CAIRO_OPERATOR_OVER);
            cairo_scale(into, scale, scale);
            renderPageContent(control, page, into);
            cairo_destroy(into);

            cache->page = page;
            cache->revision = revision;
            cache->renderedAt = now;
        }

        cairo_set_source_surface(cr, cache->surface.get(), layout.x, layout.y);
        cairo_paint(cr);
    }

    cairo_save(cr);
    cairo_translate(cr, layout.x, layout.y);
    cairo_scale(cr, scale, scale);
    cairo_rectangle(cr, 0, 0, pageWidth, pageHeight);
    cairo_clip(cr);

    if (cache == nullptr) {
        renderPageContent(control, page, cr);
    }

    // The stroke currently under the pen does not exist in the model yet -- it lives in the main
    // view's overlays, and it is redrawn on every frame however little else has changed. Drawing
    // them brings the selection, laser pointer and geometry tools across as well.
    if (XojPageView* pageView = xournal != nullptr ? xournal->getViewFor(pageNo) : nullptr;
        pageView != nullptr && pageView->getPage() == page) {
        pageView->drawOverlays(cr);
    }

    cairo_restore(cr);
    return layout;
}

}  // namespace xoj::canvas
