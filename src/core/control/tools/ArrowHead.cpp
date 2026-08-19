#include "ArrowHead.h"

#include <cmath>  // for cos, sin, M_PI

#include "model/Point.h"  // for Point

auto xoj::arrowhead::computeSize(double lineLength, double thickness, double shrunkLengthRatio) -> Size {
    const double slimness = lineLength / thickness;

    // dist is the distance between the line's and the arrow's tips
    // delta is the angle between each arrow leg and the line

    // an appropriate opening angle 2*delta is Pi/3 radians for an arrow shape
    double delta = M_PI / 6.0;
    // We use different slimness regimes for proper sizing:
    const double THICK1 = 7, THICK3 = 1.6;
    const double LENGTH2 = 0.4, LENGTH4 = shrunkLengthRatio;
    // set up the size of the arrow head to be THICK1 x the thickness of the line
    double dist = thickness * THICK1;
    // but not too large compared to the line length
    if (slimness >= THICK1 / LENGTH2) {
        // arrow head is not too long compared to the line length (regime 1)
    } else if (slimness >= THICK3 / LENGTH2) {
        // arrow head is not too short compared to the thickness (regime 2)
        dist = lineLength * LENGTH2;
    } else if (slimness >= THICK3 / LENGTH4) {
        // arrow head is not too thick compared to the line length (regime 3)
        dist = thickness * THICK3;
        // help visibility by widening the angle
        delta = (1 + (slimness - THICK3 / LENGTH2) / (THICK3 / LENGTH4 - THICK3 / LENGTH2)) * M_PI / 6.0;
        // which allows to shorten the tips and keep the horizonzal distance
        dist *= sin(M_PI / 6.0) / sin(delta);
    } else {
        // shrinking down gracefully (regime 4)
        dist = lineLength * LENGTH4;
        delta = M_PI / 3.0;
        dist *= sin(M_PI / 6.0) / sin(M_PI / 3.0);
    }

    return {dist, delta};
}

void xoj::arrowhead::append(std::vector<Point>& shape, const Point& tip, double angle, Size size) {
    shape.emplace_back(tip.x - size.dist * cos(angle + size.delta), tip.y - size.dist * sin(angle + size.delta));
    shape.emplace_back(tip);
    shape.emplace_back(tip.x - size.dist * cos(angle - size.delta), tip.y - size.dist * sin(angle - size.delta));
    shape.emplace_back(tip);
}
