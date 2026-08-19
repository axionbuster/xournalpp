#include "ArrowHandler.h"

#include <algorithm>  // for minmax_element
#include <cmath>      // for cos, sin, atan2, M_PI

#include "control/tools/ArrowHead.h"               // for arrowhead::append
#include "control/tools/BaseShapeHandler.h"        // for BaseShapeHandler
#include "control/tools/SnapToGridInputHandler.h"  // for SnapToGridInputHan...
#include "gui/inputdevices/PositionInputData.h"    // for PositionInputData
#include "model/LineShape.h"                       // for LineShape, LineShapeType
#include "model/Point.h"                           // for Point
#include "util/Range.h"                            // for Range

ArrowHandler::ArrowHandler(Control* control, const PageRef& page, bool doubleEnded):
        BaseShapeHandler(control, page), doubleEnded(doubleEnded) {}

ArrowHandler::~ArrowHandler() = default;

auto ArrowHandler::createShape(bool isAltDown, bool isShiftDown, bool isControlDown)
        -> std::pair<std::vector<Point>, Range> {
    const Point dragged = snappingHandler.snap(this->currPoint, this->startPoint, isAltDown);

    // While drawing, the press point is the tail and the dragged point the head. When an
    // existing arrow is re-edited by its tail, the two swap roles: the head stays put.
    this->anchorA = this->draggingFirstAnchor ? dragged : this->startPoint;
    this->anchorB = this->draggingFirstAnchor ? this->startPoint : dragged;

    const double lineLength = std::hypot(this->anchorB.x - this->anchorA.x, this->anchorB.y - this->anchorA.y);
    const double thickness = this->getShapeThickness();

    // We've now computed the line points for the arrow, so we just have to build the head(s)
    const auto headSize = xoj::arrowhead::computeSize(lineLength, thickness, doubleEnded ? 0.5 : 0.8);

    const double angle = atan2(this->anchorB.y - this->anchorA.y, this->anchorB.x - this->anchorA.x);

    std::pair<std::vector<Point>, Range> res; // members initialised below
    std::vector<Point>& shape = res.first;

    shape.reserve(doubleEnded ? 10 : 6);

    shape.emplace_back(this->anchorA);

    if (doubleEnded) {
        xoj::arrowhead::append(shape, this->anchorA, angle + M_PI, headSize);
    }

    shape.emplace_back(this->anchorB);
    xoj::arrowhead::append(shape, this->anchorB, angle, headSize);

    auto [minX, maxX] = std::minmax_element(shape.begin(), shape.end(), [](auto& p, auto& q) { return p.x < q.x; });
    auto [minY, maxY] = std::minmax_element(shape.begin(), shape.end(), [](auto& p, auto& q) { return p.y < q.y; });
    res.second = Range(minX->x, minY->y, maxX->x, maxY->y);

    return res;
}

auto ArrowHandler::getLineShapeMetadata() const -> std::optional<LineShape> {
    return xoj::lineshape::makeIfMeaningful(doubleEnded ? LineShapeType::DOUBLE_ARROW : LineShapeType::ARROW,
                                            this->anchorA, this->anchorB);
}
