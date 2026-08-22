#include "LineShape.h"

#include <algorithm>  // for max, min
#include <cmath>      // for hypot
#include <limits>     // for numeric_limits
#include <vector>     // for vector

#include "model/Element.h"  // for Element, ELEMENT_STROKE
#include "model/Layer.h"    // for Layer
#include "model/Stroke.h"   // for Stroke

/// The grab zone never shrinks below this, however thin the stroke is.
static constexpr double MIN_GRAB_RADIUS = 10.0;

/// A wide stroke gets a grab zone scaled to it, since its arrow head is scaled to it too.
static constexpr double GRAB_RADIUS_PER_WIDTH = 3.0;

auto xoj::lineshape::grabRadius(double strokeWidth) -> double {
    return std::max(MIN_GRAB_RADIUS, GRAB_RADIUS_PER_WIDTH * strokeWidth);
}

auto xoj::lineshape::makeIfMeaningful(LineShapeType type, const Point& a, const Point& b) -> std::optional<LineShape> {
    if (std::hypot(b.x - a.x, b.y - a.y) < MIN_ANCHOR_SEPARATION) {
        return std::nullopt;
    }
    return LineShape{type, a, b};
}

auto xoj::lineshape::projectOntoAxis(const Point& fixedAnchor, const Point& grabbedAnchor, const Point& dragged)
        -> Point {
    const double dx = grabbedAnchor.x - fixedAnchor.x;
    const double dy = grabbedAnchor.y - fixedAnchor.y;
    const double length = std::hypot(dx, dy);
    if (length < MIN_ANCHOR_SEPARATION) {
        return dragged;
    }
    const double ux = dx / length;
    const double uy = dy / length;
    const double along = (dragged.x - fixedAnchor.x) * ux + (dragged.y - fixedAnchor.y) * uy;
    return Point(fixedAnchor.x + along * ux, fixedAnchor.y + along * uy);
}

auto xoj::lineshape::grabbedAnchor(const Stroke& stroke, const Point& p) -> std::optional<AnchorHit> {
    const auto& shape = stroke.getLineShape();
    if (!shape) {
        return std::nullopt;
    }

    const Point& a = shape->anchorA;
    const Point& b = shape->anchorB;

    // The arrow head tips are the extreme points of the drawn stroke along the anchor axis.
    // Reading them off the points themselves keeps the test exact whatever overshoot the
    // stroke was drawn with, and whatever the overshoot setting says today.
    Point tipA = a;
    Point tipB = b;
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length = std::hypot(dx, dy);
    if (length > 0.0) {
        const double ux = dx / length;
        const double uy = dy / length;
        double minProjection = std::numeric_limits<double>::max();
        double maxProjection = std::numeric_limits<double>::lowest();
        for (const Point& q: stroke.getPointVector()) {
            const double projection = (q.x - a.x) * ux + (q.y - a.y) * uy;
            if (projection < minProjection) {
                minProjection = projection;
                tipA = q;
            }
            if (projection > maxProjection) {
                maxProjection = projection;
                tipB = q;
            }
        }
    }

    const double radius = grabRadius(stroke.getWidth());
    const double distanceA = std::min(p.lineLengthTo(a), p.lineLengthTo(tipA));
    const double distanceB = std::min(p.lineLengthTo(b), p.lineLengthTo(tipB));

    if (distanceA > radius && distanceB > radius) {
        return std::nullopt;
    }
    return distanceA <= distanceB ? AnchorHit{Anchor::A, distanceA} : AnchorHit{Anchor::B, distanceB};
}

auto xoj::lineshape::findGrab(Layer* layer, const Point& p) -> std::optional<Grab> {
    if (!layer) {
        return std::nullopt;
    }

    // Every candidate is measured, and the nearest end wins. Stopping at the first hit would let
    // a wide stroke — whose grab zone is scaled to its width — swallow a press that lands exactly
    // on a thinner shape's anchor underneath it.
    std::optional<Grab> best;
    double bestDistance = std::numeric_limits<double>::max();

    // Topmost element first, so that equal distances go to the one the user sees under the cursor.
    auto& elements = layer->getElements();
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
        if (!*it || (*it)->getType() != ELEMENT_STROKE) {
            continue;
        }
        auto* stroke = static_cast<Stroke*>(it->get());
        if (auto hit = grabbedAnchor(*stroke, p); hit && hit->distance < bestDistance) {
            bestDistance = hit->distance;
            best = Grab{stroke, hit->anchor};
        }
    }
    return best;
}
