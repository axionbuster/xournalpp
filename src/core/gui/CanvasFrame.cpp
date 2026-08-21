#include "CanvasFrame.h"

#include <algorithm>     // for max, min
#include <cmath>         // for floor
#include <shared_mutex>  // for shared_lock
#include <utility>       // for move

#include "control/Control.h"              // for Control
#include "control/jobs/Job.h"             // for Job, JOB_TYPE_RENDER
#include "control/jobs/XournalScheduler.h"  // for XournalScheduler
#include "gui/MainWindow.h"               // for MainWindow
#include "gui/PageView.h"                 // for XojPageView
#include "gui/XournalView.h"              // for XournalView
#include "model/Document.h"               // for Document
#include "model/XojPage.h"                // for XojPage
#include "util/Color.h"                   // for cairo_set_source_rgbi
#include "util/Util.h"                    // for execInUiThread
#include "util/safe_casts.h"              // for ceil_cast
#include "view/DocumentView.h"            // for DocumentView
#include "view/overlays/OverlayView.h"    // for ToolView

namespace xoj::canvas {

namespace {

/**
 * How long a kept picture is used before the page is drawn again regardless.
 *
 * Every route a change is known to take reports itself, and this is the answer to the ones nobody
 * has thought of: a plugin, a tool added later, a corner of the model that repaints by some path of
 * its own. A recording that quietly stops following the page is the one failure worth spending a
 * few background renders a second to rule out, and a quarter of a second is short enough that
 * nothing is ever seen to be stale.
 */
constexpr gint64 MAX_CACHE_AGE = 250 * 1000;  // microseconds

/// Draw the page's settled content -- everything except ink still under the pen -- at scale 1.
/// The caller holds the document's shared lock.
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

/**
 * One background re-render of a FrameCache's picture, run on the scheduler's worker thread the
 * same way the main view's own RenderJob is. Everything it needs was captured on the UI thread
 * under the document lock; the worker takes the shared lock again for the drawing itself, and the
 * result is handed back to the UI thread, where the cache decides whether it still fits.
 */
class FrameRefreshJob final: public Job {
public:
    FrameRefreshJob(Control* control, PageRef page, double pageScale, int width, int height, std::uint64_t revision,
                    gint64 startedAt, std::shared_ptr<FrameCache*> aliveToken):
            control(control),
            page(std::move(page)),
            pageScale(pageScale),
            width(width),
            height(height),
            revision(revision),
            startedAt(startedAt),
            aliveToken(std::move(aliveToken)) {}

    auto getType() -> JobType override { return JOB_TYPE_RENDER; }

protected:
    void run() override {
        xoj::util::CairoSurfaceSPtr rendered(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, this->width, this->height),
                                             xoj::util::adopt);
        cairo_t* cr = cairo_create(rendered.get());
        cairo_scale(cr, this->pageScale, this->pageScale);
        {
            std::shared_lock<Document> lock(*this->control->getDocument());
            renderPageContent(this->control, this->page, cr);
        }
        cairo_destroy(cr);
        cairo_surface_flush(rendered.get());

        Util::execInUiThread([token = this->aliveToken, surface = std::move(rendered), page = this->page,
                              w = this->width, h = this->height, rev = this->revision,
                              at = this->startedAt]() mutable {
            if (FrameCache* cache = *token; cache != nullptr) {
                cache->completeRefresh(std::move(surface), page, w, h, rev, at);
            }
        });
    }

private:
    Control* control;
    PageRef page;
    double pageScale;
    int width;
    int height;
    std::uint64_t revision;
    gint64 startedAt;
    std::shared_ptr<FrameCache*> aliveToken;
};

FrameCache::FrameCache(): aliveToken(std::make_shared<FrameCache*>(this)) {}

FrameCache::~FrameCache() { *this->aliveToken = nullptr; }

void FrameCache::invalidate() {
    this->surface.reset();
    this->page = PageRef{};
    // In-flight renders die on arrival: completeRefresh only adopts into a matching live surface.
}

void FrameCache::setRefreshedCallback(std::function<void()> callback) { this->onRefreshed = std::move(callback); }

auto FrameCache::getGeneration() const -> std::uint64_t { return this->generation; }

void FrameCache::drawSettled(const PageRef& settledPage, const xoj::view::ToolView* v) {
    if (!this->surface || this->page != settledPage) {
        return;
    }

    cairo_t* cr = cairo_create(this->surface.get());
    cairo_scale(cr, this->scale, this->scale);
    v->drawWithoutDrawingAids(cr);
    cairo_destroy(cr);

    this->generation++;
    // The revision is deliberately left where it was: the caller bumps the global one, so the
    // staleness check will still schedule a background reconcile of this shortcut.
    if (this->onRefreshed) {
        this->onRefreshed();
    }
}

void FrameCache::kickRefresh(Control* control) {
    if (this->refreshInFlight || !this->page) {
        return;
    }
    this->refreshInFlight = true;

    auto* job = new FrameRefreshJob(control, this->page, this->scale, this->width, this->height,
                                    control->getCanvasRevision(), g_get_monotonic_time(), this->aliveToken);
    control->getScheduler()->addJob(job, JOB_PRIORITY_HIGH);
    job->unref();
}

void FrameCache::completeRefresh(xoj::util::CairoSurfaceSPtr renderedSurface, const PageRef& renderedPage,
                                 int renderedWidth, int renderedHeight, std::uint64_t renderedRevision,
                                 gint64 startedAt) {
    this->refreshInFlight = false;

    // Adopt only if the picture still answers the current question. A page flip or a resize while
    // the render was under way has already been handled synchronously; this render is then about
    // yesterday and is dropped on the floor.
    if (!this->surface || this->page != renderedPage || this->width != renderedWidth ||
        this->height != renderedHeight) {
        return;
    }

    this->surface = std::move(renderedSurface);
    this->revision = renderedRevision;
    this->renderedAt = startedAt;
    this->generation++;
    if (this->onRefreshed) {
        this->onRefreshed();
    }
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

        const bool wrongSize =
                !cache->surface || cache->width != pagePixelWidth || cache->height != pagePixelHeight;

        if (wrongSize || cache->page != page) {
            // A genuinely different picture: first frame, another page, another output size. The
            // only case rendered here and now, because keeping the previous page on show while the
            // right one renders in the background would be a lie the projector's audience sees.
            if (wrongSize) {
                cache->surface.reset(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pagePixelWidth, pagePixelHeight),
                                     xoj::util::adopt);
                cache->width = pagePixelWidth;
                cache->height = pagePixelHeight;
            }
            cairo_t* into = cairo_create(cache->surface.get());
            cairo_set_operator(into, CAIRO_OPERATOR_CLEAR);
            cairo_paint(into);
            cairo_set_operator(into, CAIRO_OPERATOR_OVER);
            cairo_scale(into, scale, scale);
            renderPageContent(control, page, into);
            cairo_destroy(into);

            cache->page = page;
            cache->scale = scale;
            cache->revision = revision;
            cache->renderedAt = now;
            cache->generation++;
        } else if (cache->revision != revision || now - cache->renderedAt >= MAX_CACHE_AGE) {
            // The same picture gone stale: renew it on a worker and keep showing what we have.
            // Whatever the change was, the settled-stroke shortcut has already patched the common
            // case, so "what we have" is at worst a few frames old, never wrong for long.
            cache->kickRefresh(control);
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
        layout.overlaysDrawn = pageView->drawOverlays(cr) > 0;
    }

    cairo_restore(cr);
    return layout;
}

// ===========================================================================================
// Frame rate meter
// ===========================================================================================

void FrameRateMeter::tick() {
    const gint64 now = g_get_monotonic_time();

    std::lock_guard<std::mutex> lock(this->mutex);
    if (this->firstTick == 0) {
        this->firstTick = now;
    }
    this->times[this->next] = now;
    this->next = (this->next + 1) % CAPACITY;
    if (this->next == 0) {
        this->filled = true;
    }
}

void FrameRateMeter::reset() {
    std::lock_guard<std::mutex> lock(this->mutex);
    this->next = 0;
    this->filled = false;
    this->firstTick = 0;
}

auto FrameRateMeter::rate() const -> double {
    const gint64 now = g_get_monotonic_time();

    std::lock_guard<std::mutex> lock(this->mutex);
    if (this->firstTick == 0) {
        return 0.0;
    }

    // Too soon to say anything. A rate worked out from the first tick or two is arithmetic on a
    // sample of one -- the first reading of a 30 Hz clock came out as a million -- and a wrong
    // number is worse than no number, so nothing is reported until there is something to divide by.
    const gint64 age = now - this->firstTick;
    if (age < WINDOW / 2) {
        return 0.0;
    }

    const gint64 cutoff = now - WINDOW;
    const std::size_t stored = this->filled ? CAPACITY : this->next;

    std::size_t counted = 0;
    for (std::size_t i = 0; i < stored; i++) {
        // Walk back from the newest, and stop at the first one that has fallen out of the window:
        // the ring is in time order, so everything before it has fallen out too.
        const std::size_t index = (this->next + CAPACITY - 1 - i) % CAPACITY;
        if (this->times[index] <= cutoff) {
            break;
        }
        counted++;
    }

    // Divided by however much of the window has actually been measured. Dividing by the whole
    // window before it has filled would report a rate climbing towards the real one from below --
    // a recording that appears to start out stuttering and then recover, which it did not.
    const gint64 span = std::min<gint64>(WINDOW, age);
    return static_cast<double>(counted) * 1e6 / static_cast<double>(span);
}

}  // namespace xoj::canvas
