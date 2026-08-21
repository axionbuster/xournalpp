/*
 * Xournal++
 *
 * Handles input to draw a ray or an infinite line
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>  // for optional
#include <vector>    // for vector

#include "model/LineShape.h"  // for LineShape
#include "model/PageRef.h"    // for PageRef
#include "model/Point.h"      // for Point

#include "BaseShapeHandler.h"  // for BaseShapeHandler

class Control;

/**
 * @brief Draws a straight line through the two dragged points, overshooting them by a fixed
 * distance and ending in an arrow head.
 *
 * Both dragged points snap like the ones of the arrow tool, so that the line can be made to pass
 * exactly through plotted points. A ray overshoots the second point only, an infinite line
 * overshoots both. The overshoot is shortened so that the tips of the shaft stay on the page.
 *
 * The arrow heads hanging off those tips are not clipped: a line drawn close to a page border and
 * running along it has its heads reach sideways past that border, the same way the arrow tool's
 * heads do. Shortening the overshoot cannot prevent that, since the sideways reach does not depend
 * on how far along the line the head sits.
 */
class ExtendedLineHandler: public BaseShapeHandler {
public:
    ExtendedLineHandler(Control* control, const PageRef& page, bool bothDirections);
    ~ExtendedLineHandler() override;

private:
    auto createShape(bool isAltDown, bool isShiftDown, bool isControlDown)
            -> std::pair<std::vector<Point>, Range> override;
    std::optional<LineShape> getLineShapeMetadata() const override;

    bool bothDirections = false;

    /// The two dragged points, updated by createShape() and recorded on the stroke
    Point anchorA;
    Point anchorB;
};
