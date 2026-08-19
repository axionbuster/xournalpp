#include "ExtendedLineHandler.h"

#include <algorithm>  // for minmax_element, min, max
#include <cmath>      // for hypot, atan2, M_PI

#include "control/Control.h"                       // for Control
#include "control/ToolHandler.h"                   // for ToolHandler
#include "control/settings/Settings.h"             // for Settings
#include "control/tools/ArrowHead.h"               // for arrowhead::append
#include "control/tools/BaseShapeHandler.h"        // for BaseShapeHandler
#include "control/tools/SnapToGridInputHandler.h"  // for SnapToGridInputHan...
#include "model/Point.h"                           // for Point
#include "model/XojPage.h"                         // for XojPage
#include "util/Range.h"                            // for Range

/**
 * @brief How far one may travel from `from` along the unit vector (dx, dy) without leaving the page
 * @return the travelled distance, clamped to [0, maxDist]
 */
static auto clipToPage(const Point& from, double dx, double dy, double maxDist, double width, double height)
        -> double {
    double dist = maxDist;
    auto clip = [&dist](double numerator, double denominator) { dist = std::min(dist, numerator / denominator); };

    if (dx > 0.0) {
        clip(width - from.x, dx);
    } else if (dx < 0.0) {
        clip(-from.x, dx);
    }
    if (dy > 0.0) {
        clip(height - from.y, dy);
    } else if (dy < 0.0) {
        clip(-from.y, dy);
    }

    return std::max(0.0, dist);
}

ExtendedLineHandler::ExtendedLineHandler(Control* control, const PageRef& page, bool bothDirections):
        BaseShapeHandler(control, page), bothDirections(bothDirections) {}

ExtendedLineHandler::~ExtendedLineHandler() = default;

auto ExtendedLineHandler::createShape(bool isAltDown, bool isShiftDown, bool isControlDown)
        -> std::pair<std::vector<Point>, Range> {
    const Point b = snappingHandler.snap(this->currPoint, this->startPoint, isAltDown);
    const double dist = std::hypot(b.x - this->startPoint.x, b.y - this->startPoint.y);

    if (dist == 0.0) {
        // No direction to extend along yet
        Range rg(this->startPoint.x, this->startPoint.y);
        return {{this->startPoint, b}, rg};
    }

    // Unit vector pointing from the first towards the second anchor point
    const double ux = (b.x - this->startPoint.x) / dist;
    const double uy = (b.y - this->startPoint.y) / dist;

    const double overshoot = control->getSettings()->getExtendedLineOvershoot();
    const double pageWidth = this->page->getWidth();
    const double pageHeight = this->page->getHeight();

    // Only the overshoot gets clipped: the anchor points themselves are where the user put them.
    // This keeps the tips of the shaft on the page — the arrow heads sitting on them may still
    // reach past a border sideways, just like the arrow tool's heads do.
    const double endOvershoot = clipToPage(b, ux, uy, overshoot, pageWidth, pageHeight);
    const Point end(b.x + endOvershoot * ux, b.y + endOvershoot * uy);

    Point begin = this->startPoint;
    if (this->bothDirections) {
        const double beginOvershoot = clipToPage(this->startPoint, -ux, -uy, overshoot, pageWidth, pageHeight);
        begin = Point(this->startPoint.x - beginOvershoot * ux, this->startPoint.y - beginOvershoot * uy);
    }

    const double thickness = control->getToolHandler()->getThickness();
    const double shaftLength = std::hypot(end.x - begin.x, end.y - begin.y);
    const auto headSize = xoj::arrowhead::computeSize(shaftLength, thickness, this->bothDirections ? 0.5 : 0.8);

    const double angle = atan2(uy, ux);

    std::pair<std::vector<Point>, Range> res;  // members initialised below
    std::vector<Point>& shape = res.first;

    shape.reserve(this->bothDirections ? 10 : 6);

    shape.emplace_back(begin);

    if (this->bothDirections) {
        xoj::arrowhead::append(shape, begin, angle + M_PI, headSize);
    }

    shape.emplace_back(end);
    xoj::arrowhead::append(shape, end, angle, headSize);

    auto [minX, maxX] = std::minmax_element(shape.begin(), shape.end(), [](auto& p, auto& q) { return p.x < q.x; });
    auto [minY, maxY] = std::minmax_element(shape.begin(), shape.end(), [](auto& p, auto& q) { return p.y < q.y; });
    res.second = Range(minX->x, minY->y, maxX->x, maxY->y);

    return res;
}
