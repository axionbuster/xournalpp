#include "ExtendedLineHandler.h"

#include <algorithm>  // for minmax_element, min, max
#include <cmath>      // for hypot, atan2, M_PI

#include "control/Control.h"                       // for Control
#include "control/settings/Settings.h"             // for Settings
#include "control/tools/ArrowHead.h"               // for arrowhead::append
#include "control/tools/BaseShapeHandler.h"        // for BaseShapeHandler
#include "control/tools/SnapToGridInputHandler.h"  // for SnapToGridInputHan...
#include "model/LineShape.h"                       // for LineShape, LineShapeType
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
    const Point dragged = snappingHandler.snap(this->currPoint, this->startPoint, isAltDown);

    // While drawing, the press point is the first anchor and the dragged point the second. When
    // an existing shape is re-edited by its first anchor, the two swap roles: for a ray, that is
    // how the origin can be moved while the arrow head end stays put.
    const Point a = this->draggingFirstAnchor ? dragged : this->startPoint;
    const Point b = this->draggingFirstAnchor ? this->startPoint : dragged;
    this->anchorA = a;
    this->anchorB = b;

    const double dist = std::hypot(b.x - a.x, b.y - a.y);

    if (dist == 0.0) {
        // No direction to extend along yet
        Range rg(a.x, a.y);
        return {{a, b}, rg};
    }

    // Unit vector pointing from the first towards the second anchor point
    const double ux = (b.x - a.x) / dist;
    const double uy = (b.y - a.y) / dist;

    const double overshoot = control->getSettings()->getExtendedLineOvershoot();
    const double pageWidth = this->page->getWidth();
    const double pageHeight = this->page->getHeight();

    // Only the overshoot gets clipped: the anchor points themselves are where the user put them.
    // This keeps the tips of the shaft on the page — the arrow heads sitting on them may still
    // reach past a border sideways, just like the arrow tool's heads do.
    const double endOvershoot = clipToPage(b, ux, uy, overshoot, pageWidth, pageHeight);
    const Point end(b.x + endOvershoot * ux, b.y + endOvershoot * uy);

    Point begin = a;
    if (this->bothDirections) {
        const double beginOvershoot = clipToPage(a, -ux, -uy, overshoot, pageWidth, pageHeight);
        begin = Point(a.x - beginOvershoot * ux, a.y - beginOvershoot * uy);
    }

    const double thickness = this->getShapeThickness();
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

auto ExtendedLineHandler::getLineShapeMetadata() const -> std::optional<LineShape> {
    return xoj::lineshape::makeIfMeaningful(this->bothDirections ? LineShapeType::INFINITE_LINE : LineShapeType::RAY,
                                            this->anchorA, this->anchorB);
}
