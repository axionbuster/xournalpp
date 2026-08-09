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

#include <cstdint>  // for uint64_t

#include <cairo.h>
#include <glib.h>  // for gint64

#include "model/PageRef.h"                // for PageRef
#include "util/Color.h"
#include "util/raii/CairoWrappers.h"  // for CairoSurfaceSPtr

class Control;

namespace xoj::canvas {

/// Where the page landed inside the target area, letterbox excluded. Zero-sized if nothing drew.
struct FrameLayout {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    /// The page that was drawn, so a caller can tell whether a later change concerns it.
    PageRef page;

    [[nodiscard]] bool isEmpty() const { return width <= 0.0 || height <= 0.0; }
};

/**
 * The last drawn picture of a page, kept so that the next frame does not have to draw it again.
 *
 * Both callers redraw many times a second and the page is nearly always the same one it was a
 * sixtieth of a second ago, so drawing every stroke of a full page each time is work spent on an
 * answer already known -- and the time it costs comes out of the UI thread, which is the thread
 * collecting the pen input. Rendering only when Control::getCanvasRevision() moves turns a lecture
 * into one render per finished stroke instead of sixty per second.
 *
 * Ink still under the pen is never cached: it is an overlay, and it is redrawn on every frame.
 *
 * Owned by whoever draws -- one cache per caller, since they draw at different sizes. Passing none
 * at all is supported and simply renders everything every time.
 */
class FrameCache {
public:
    FrameCache() = default;
    FrameCache(const FrameCache&) = delete;
    auto operator=(const FrameCache&) -> FrameCache& = delete;

    /// Throw the kept picture away, so the next frame is drawn from the document again.
    void invalidate();

private:
    friend FrameLayout drawCurrentPage(Control*, cairo_t*, double, double, Color, FrameCache*);

    /// The page's settled content, at exactly the size it is drawn on screen. Null when empty.
    xoj::util::CairoSurfaceSPtr surface;

    PageRef page;
    int width = 0;
    int height = 0;
    std::uint64_t revision = 0;

    /// When the picture was drawn, so that a change nothing reported still shows up in a moment.
    gint64 renderedAt = 0;
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
