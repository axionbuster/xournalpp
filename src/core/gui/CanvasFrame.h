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

#include <cairo.h>

#include "model/PageRef.h"  // for PageRef
#include "util/Color.h"

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
 * Fill @p width × @p height with @p background and draw the current page into it, scaled to fit.
 *
 * Rendered from the document model rather than copied from the main view, so the result is sharp
 * at its own size instead of an upscale of whatever zoom the main window happens to be at. Strokes
 * still under the pen are picked up from the main view's overlays, which is what makes ink appear
 * as it is written rather than a beat behind.
 *
 * Takes the document's shared lock for the duration, exactly once. Safe to call from the UI thread
 * and only from there: it reaches into the main view for those overlays.
 */
FrameLayout drawCurrentPage(Control* control, cairo_t* cr, double width, double height, Color background);

}  // namespace xoj::canvas
