/*
 * Xournal++
 *
 * Handles input to draw an arrow
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

class ArrowHandler: public BaseShapeHandler {
public:
    ArrowHandler(Control* control, const PageRef& page, bool doubleEnded);
    ~ArrowHandler() override;

private:
    auto createShape(bool isAltDown, bool isShiftDown, bool isControlDown)
            -> std::pair<std::vector<Point>, Range> override;
    std::optional<LineShape> getLineShapeMetadata() const override;

    bool doubleEnded = false;

    /// The tail and the head of the arrow, updated by createShape() and recorded on the stroke
    Point anchorA;
    Point anchorB;
};
