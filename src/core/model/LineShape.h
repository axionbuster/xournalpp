/*
 * Xournal++
 *
 * Optional metadata recording that a stroke was drawn as a straight line shape
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>     // for array
#include <optional>  // for optional

#include "Point.h"  // for Point

class Layer;
class Stroke;

/**
 * @brief The kind of line shape a stroke was drawn as.
 *
 * The NAMES array doubles as the vocabulary of the "shape" attribute in .xopp files and is
 * looked up by the XML parser's generic named-enum machinery, so the entries and their order
 * are part of the file format.
 */
class LineShapeType {
public:
    enum Value { RAY, INFINITE_LINE, ARROW, DOUBLE_ARROW };
    static constexpr std::array<const char8_t*, 4> NAMES = {u8"ray", u8"infiniteLine", u8"arrow", u8"doubleArrow"};
    LineShapeType(Value v): value(v) {}

    // Implicit conversion to underlying enum type
    operator const Value&() const { return value; }
    operator Value&() { return value; }

private:
    Value value = ARROW;
};

/**
 * @brief The two user-meaningful points of a straight line shape, in page coordinates.
 *
 * These are the points the user actually dragged between, not the extremities of the drawn
 * stroke: an arrow head sits past its anchor, and a ray or infinite line overshoots one or
 * both of them. Anchor A is where the drag started (for a ray, the origin), anchor B where it
 * ended (for an arrow, the head).
 */
struct LineShape {
    LineShapeType type = LineShapeType::ARROW;
    Point anchorA;
    Point anchorB;
};

namespace xoj::lineshape {

/// One of the two ends of a line shape.
enum class Anchor { A, B };

/**
 * @brief The shortest anchor separation that still describes a line, in page units.
 *
 * A page unit is a 72nd of an inch, so a shape whose two anchors are closer together than this
 * is a dot with no direction rather than a line. Such a shape gets no metadata: it would be
 * ungrabbable in practice — both ends sit under the same pixel — and recording it would tag the
 * whole document as using this fork's file format for something nobody can see.
 */
constexpr double MIN_ANCHOR_SEPARATION = 1.0;

/**
 * @brief The metadata for a shape drawn between `a` and `b`, or nothing if it is degenerate.
 *
 * See MIN_ANCHOR_SEPARATION.
 */
std::optional<LineShape> makeIfMeaningful(LineShapeType type, const Point& a, const Point& b);

/**
 * @brief How close to an end of a line shape a press must land to grab that end, in page units.
 *
 * Wide strokes get a proportionally wider grab zone; thin ones get a floor that stays
 * comfortable to hit.
 */
double grabRadius(double strokeWidth);

/// An end of a line shape a press landed near, and how far from that end it landed.
struct AnchorHit {
    Anchor anchor;
    /// Page units from the press to the nearer of the anchor and its arrow head tip
    double distance;
};

/**
 * @brief Which end of `stroke` a press at `p` grabs, if any, and how near it landed.
 *
 * Both the anchor itself and the arrow head tip belonging to that end are targets, so that
 * the head a user sees can be grabbed even though it sits past the anchor. Strokes without
 * line shape metadata are never grabbed.
 */
std::optional<AnchorHit> grabbedAnchor(const Stroke& stroke, const Point& p);

struct Grab {
    Stroke* stroke;
    Anchor anchor;
};

/**
 * @brief `dragged` moved onto the line through `fixedAnchor` and `grabbedAnchor`.
 *
 * Stretching an end of a ray or infinite line must not disturb the line itself: the grabbed
 * anchor slides along the original axis and sideways cursor travel is discarded. When the two
 * anchors are closer together than MIN_ANCHOR_SEPARATION there is no axis to preserve, and
 * `dragged` comes back unchanged.
 */
Point projectOntoAxis(const Point& fixedAnchor, const Point& grabbedAnchor, const Point& dragged);

/**
 * @brief The line shape stroke of `layer` whose end a press at `p` grabs, if any.
 *
 * The nearest end wins across the whole layer, not merely the first one found from the top:
 * a wide stroke has a wide grab zone, and it must not steal a press that lands dead on a
 * thinner shape's anchor just because it happens to be drawn on top. Equal distances go to the
 * topmost stroke, which is the one the user sees under the cursor.
 */
std::optional<Grab> findGrab(Layer* layer, const Point& p);

}  // namespace xoj::lineshape
