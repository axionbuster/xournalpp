/*
 * Xournal++
 *
 * Shared geometry of the arrow heads drawn by the arrow, ray and infinite line tools
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <vector>  // for vector

#include "model/Point.h"  // for Point

namespace xoj::arrowhead {

/**
 * @brief Dimensions of an arrow head: the distance between the line's and the arrow's tips, and the
 * angle between each arrow leg and the line.
 */
struct Size {
    double dist;
    double delta;
};

/**
 * @brief Compute an arrow head size fitting both the line's thickness and its length
 * @param lineLength the length of the line the head sits on
 * @param thickness the thickness of the stroke
 * @param shrunkLengthRatio the largest fraction of the line length a head may take up once the line
 *      gets very short. Lines carrying a head on either end need a smaller value than lines
 *      carrying a single one.
 */
Size computeSize(double lineLength, double thickness, double shrunkLengthRatio);

/**
 * @brief Append an arrow head at `tip` to `shape`, as a retraced zig-zag (leg, back to tip, other
 *      leg, back to tip) so that the head is part of one single stroke.
 * @param angle the direction in which the line runs into `tip`
 * @pre `shape` ends with `tip`
 */
void append(std::vector<Point>& shape, const Point& tip, double angle, Size size);

}  // namespace xoj::arrowhead
