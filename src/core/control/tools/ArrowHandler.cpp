#include "ArrowHandler.h"

#include <algorithm>  // for minmax_element
#include <cmath>      // for cos, sin, atan2, M_PI

#include "control/Control.h"                       // for Control
#include "control/ToolHandler.h"                   // for ToolHandler
#include "control/tools/ArrowHead.h"               // for arrowhead::append
#include "control/tools/BaseShapeHandler.h"        // for BaseShapeHandler
#include "control/tools/SnapToGridInputHandler.h"  // for SnapToGridInputHan...
#include "gui/inputdevices/PositionInputData.h"    // for PositionInputData
#include "model/Point.h"                           // for Point
#include "util/Range.h"                            // for Range

ArrowHandler::ArrowHandler(Control* control, const PageRef& page, bool doubleEnded):
        BaseShapeHandler(control, page), doubleEnded(doubleEnded) {}

ArrowHandler::~ArrowHandler() = default;

auto ArrowHandler::createShape(bool isAltDown, bool isShiftDown, bool isControlDown)
        -> std::pair<std::vector<Point>, Range> {
    Point c = snappingHandler.snap(this->currPoint, this->startPoint, isAltDown);
    const double lineLength = std::hypot(c.x - this->startPoint.x, c.y - this->startPoint.y);
    const double thickness = control->getToolHandler()->getThickness();

    // We've now computed the line points for the arrow, so we just have to build the head(s)
    const auto headSize = xoj::arrowhead::computeSize(lineLength, thickness, doubleEnded ? 0.5 : 0.8);

    const double angle = atan2(c.y - this->startPoint.y, c.x - this->startPoint.x);

    std::pair<std::vector<Point>, Range> res; // members initialised below
    std::vector<Point>& shape = res.first;

    shape.reserve(doubleEnded ? 10 : 6);

    shape.emplace_back(this->startPoint);

    if (doubleEnded) {
        xoj::arrowhead::append(shape, this->startPoint, angle + M_PI, headSize);
    }

    shape.emplace_back(c);
    xoj::arrowhead::append(shape, c, angle, headSize);

    auto [minX, maxX] = std::minmax_element(shape.begin(), shape.end(), [](auto& p, auto& q) { return p.x < q.x; });
    auto [minY, maxY] = std::minmax_element(shape.begin(), shape.end(), [](auto& p, auto& q) { return p.y < q.y; });
    res.second = Range(minX->x, minY->y, maxX->x, maxY->y);

    return res;
}
