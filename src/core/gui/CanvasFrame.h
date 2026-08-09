/*
 * Xournal++
 *
 * Draws the current page the way an audience should see it: the page alone, letterboxed and
 * centred, with no toolbars, no scrollbars and no window furniture.
 *
 * Two things need exactly this picture -- the projector window, and the video recorder -- and they
 * must agree, because the projector is how you check what is being recorded. Hence one function,
 * used by both.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstdint>     // for uint64_t
#include <functional>  // for function
#include <memory>      // for shared_ptr

#include <cairo.h>
#include <glib.h>  // for gint64

#include "model/PageRef.h"            // for PageRef
#include "util/Color.h"
#include "util/raii/CairoWrappers.h"  // for CairoSurfaceSPtr

class Control;

namespace xoj::view {
class ToolView;
}

namespace xoj::canvas {

/// Where the page landed inside the target area, letterbox excluded. Zero-sized if nothing drew.
struct FrameLayout {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    /// The page that was drawn, so a caller can tell whether a later change concerns it.
    PageRef page;

    /**
     * Whether anything beyond the settled page went into this frame -- ink still under the pen,
     * a selection, the laser pointer. Overlays change from frame to frame while they exist, so a
     * consumer looking to skip identical frames cannot skip while this is set.
     */
    bool overlaysDrawn = false;

    [[nodiscard]] bool isEmpty() const { return width <= 0.0 || height <= 0.0; }
};

/**
 * The last drawn picture of a page, kept so that the next frame does not have to draw it again.
 *
 * Both callers redraw many times a second and the page is nearly always the same one it was a
 * sixtieth of a second ago, so drawing every stroke of a full page each time is work spent on an
 * answer already known -- and the time it costs comes out of the UI thread, which is the thread
 * collecting the pen input. The picture is renewed only when something changes, and the renewals
 * themselves are done by three routes, in order of cheapness:
 *
 *   - A stroke that has just been finished is drawn straight into the kept picture
 *     (drawSettled) -- the common event of a lecture, at the cost of one stroke.
 *   - Anything else that moves Control::getCanvasRevision() -- an undo, an eraser pass, a
 *     background change -- triggers a re-render of the whole page ON A SCHEDULER THREAD, while
 *     frames keep using the old picture until the new one lands. A full page of notes costs tens
 *     of milliseconds to draw, and a stall of that length on the UI thread is a visible hiccup in
 *     the very ink being written; on a worker it costs nobody anything.
 *   - Only a change of page or of output size is rendered synchronously: those need a genuinely
 *     different picture, and showing the previous page while the right one renders would be worse
 *     than the one-off pause.
 *
 * Ink still under the pen is never cached: it is an overlay, and it is redrawn on every frame.
 *
 * Owned by whoever draws -- one cache per caller, since they draw at different sizes. Passing none
 * at all is supported and simply renders everything every time. All methods are UI-thread only;
 * the background render touches nothing shared but the document lock and the PDF cache, exactly
 * as the main view's own render jobs do.
 */
class FrameCache {
public:
    FrameCache();
    ~FrameCache();
    FrameCache(const FrameCache&) = delete;
    auto operator=(const FrameCache&) -> FrameCache& = delete;

    /// Throw the kept picture away, so the next frame is drawn from the document again.
    void invalidate();

    /**
     * Called on the UI thread whenever the kept picture's pixels change for a reason other than a
     * drawCurrentPage call -- a background refresh landing, a settled stroke drawn in -- so the
     * owner can put a repaint in motion. The recorder needs none: its next tick picks the new
     * picture up by itself.
     */
    void setRefreshedCallback(std::function<void()> callback);

    /**
     * Draw a tool view that has just settled -- a finished stroke, mostly -- straight into the
     * kept picture, exactly as the main view draws it into its own buffer. This is what keeps the
     * newest stroke from flickering out of the projector and the recording for the few frames a
     * background re-render takes: the picture is patched immediately, and the re-render that the
     * caller's revision bump triggers repairs anything the patch got wrong (a stroke drawn on a
     * layer that is not the top one, say). No-op when the cache holds a different page or nothing.
     */
    void drawSettled(const PageRef& page, const xoj::view::ToolView* v);

    /**
     * Changes whenever the kept picture's pixels do. A consumer that saw the same generation, the
     * same page and no overlays twice in a row knows the two frames are identical and can skip
     * the second one entirely.
     */
    std::uint64_t getGeneration() const;

private:
    friend FrameLayout drawCurrentPage(Control*, cairo_t*, double, double, Color, FrameCache*);
    friend class FrameRefreshJob;

    /// Start a background re-render of the current picture, if one is not already running.
    void kickRefresh(Control* control);

    /// UI-thread completion of a background render: adopt the surface if it still fits.
    void completeRefresh(xoj::util::CairoSurfaceSPtr renderedSurface, const PageRef& renderedPage, int renderedWidth,
                         int renderedHeight, std::uint64_t renderedRevision, gint64 startedAt);

    /// The page's settled content, at exactly the size it is drawn on screen. Null when empty.
    xoj::util::CairoSurfaceSPtr surface;

    PageRef page;
    int width = 0;
    int height = 0;
    double scale = 1.0;
    std::uint64_t revision = 0;

    /// When the picture was drawn, so that a change nothing reported still shows up in a moment.
    gint64 renderedAt = 0;

    /// See getGeneration().
    std::uint64_t generation = 0;

    /// One background render at a time; a second request just lets the staleness check re-fire.
    bool refreshInFlight = false;

    std::function<void()> onRefreshed;

    /**
     * Outlives this object inside in-flight render jobs; the destructor nulls it, and the job's
     * UI-thread completion checks it before touching anything. Both run on the UI thread, so
     * there is no race to lose.
     */
    std::shared_ptr<FrameCache*> aliveToken;
};

/**
 * Fill @p width × @p height with @p background and draw the current page into it, scaled to fit.
 *
 * Rendered from the document model rather than copied from the main view, so the result is sharp
 * at its own size instead of an upscale of whatever zoom the main window happens to be at. Strokes
 * still under the pen are picked up from the main view's overlays, which is what makes ink appear
 * as it is written rather than a beat behind.
 *
 * Takes the document's shared lock for the duration, exactly once. Safe to call from the UI thread
 * and only from there: it reaches into the main view for those overlays.
 *
 * @param cache Where to keep the page's picture between frames, or null to draw everything afresh.
 */
FrameLayout drawCurrentPage(Control* control, cairo_t* cr, double width, double height, Color background,
                            FrameCache* cache = nullptr);

}  // namespace xoj::canvas
