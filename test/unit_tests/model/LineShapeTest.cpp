/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <memory>
#include <optional>
#include <vector>

#include <config-test.h>
#include <gtest/gtest.h>

#include "model/Layer.h"
#include "model/LineShape.h"
#include "model/PathParameter.h"
#include "model/Point.h"
#include "model/Stroke.h"

namespace {

using xoj::lineshape::Anchor;

constexpr double DEFAULT_WIDTH = 2.0;

/// The grabbed anchor as a plain integer, so that a miss and either anchor are all printable
int grabbedAnchorCode(const Stroke& stroke, const Point& p) {
    const auto hit = xoj::lineshape::grabbedAnchor(stroke, p);
    if (!hit) {
        return -1;
    }
    return hit->anchor == Anchor::A ? 0 : 1;
}

constexpr int NO_ANCHOR = -1;
constexpr int ANCHOR_A = 0;
constexpr int ANCHOR_B = 1;

/**
 * A ray-like stroke: anchor A at (100, 100), anchor B at (200, 100), and a shaft overshooting
 * B up to (230, 100) where the arrow head sits. The head's legs trail behind the tip, so they
 * never become the extreme point along the anchor axis.
 */
std::unique_ptr<Stroke> makeRayStroke() {
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(DEFAULT_WIDTH);
    stroke->addPoint(Point(100, 100));
    stroke->addPoint(Point(230, 100));
    stroke->addPoint(Point(215, 108));
    stroke->addPoint(Point(230, 100));
    stroke->addPoint(Point(215, 92));
    stroke->addPoint(Point(230, 100));
    stroke->setLineShape(LineShape{LineShapeType::RAY, Point(100, 100), Point(200, 100)});
    return stroke;
}

TEST(ModelLineShape, grabRadiusHasAFloorAndScalesWithWidth) {
    // Thin strokes get the floor
    EXPECT_DOUBLE_EQ(10.0, xoj::lineshape::grabRadius(0.0));
    EXPECT_DOUBLE_EQ(10.0, xoj::lineshape::grabRadius(1.0));
    EXPECT_DOUBLE_EQ(10.0, xoj::lineshape::grabRadius(10.0 / 3.0));
    // Beyond that, three times the stroke width
    EXPECT_DOUBLE_EQ(15.0, xoj::lineshape::grabRadius(5.0));
    EXPECT_DOUBLE_EQ(60.0, xoj::lineshape::grabRadius(20.0));
}

TEST(ModelLineShape, strokesWithoutMetadataAreNeverGrabbed) {
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(DEFAULT_WIDTH);
    stroke->addPoint(Point(100, 100));
    stroke->addPoint(Point(200, 100));

    EXPECT_EQ(NO_ANCHOR, grabbedAnchorCode(*stroke, Point(100, 100)));
    EXPECT_EQ(NO_ANCHOR, grabbedAnchorCode(*stroke, Point(200, 100)));
}

TEST(ModelLineShape, anchorsAreGrabbable) {
    const auto stroke = makeRayStroke();

    // Exactly on an anchor
    EXPECT_EQ(ANCHOR_A, grabbedAnchorCode(*stroke, Point(100, 100)));
    EXPECT_EQ(ANCHOR_B, grabbedAnchorCode(*stroke, Point(200, 100)));

    // Just inside the radius of 10
    EXPECT_EQ(ANCHOR_A, grabbedAnchorCode(*stroke, Point(100, 109)));
    EXPECT_EQ(ANCHOR_B, grabbedAnchorCode(*stroke, Point(194, 100)));
}

TEST(ModelLineShape, arrowHeadTipIsGrabbable) {
    const auto stroke = makeRayStroke();

    // The tip of the head sits 30 units past anchor B, well outside the anchor's own radius,
    // but it is the end the user sees and points at.
    EXPECT_EQ(ANCHOR_B, grabbedAnchorCode(*stroke, Point(230, 100)));
    EXPECT_EQ(ANCHOR_B, grabbedAnchorCode(*stroke, Point(236, 100)));
}

TEST(ModelLineShape, pressesAwayFromBothEndsGrabNothing) {
    const auto stroke = makeRayStroke();

    // Halfway along the shaft
    EXPECT_EQ(NO_ANCHOR, grabbedAnchorCode(*stroke, Point(150, 100)));
    // Between anchor B and the head tip, but too far from either
    EXPECT_EQ(NO_ANCHOR, grabbedAnchorCode(*stroke, Point(215, 100)));
    // Off to the side
    EXPECT_EQ(NO_ANCHOR, grabbedAnchorCode(*stroke, Point(100, 130)));
}

TEST(ModelLineShape, aWideStrokeGetsAWiderGrabZone) {
    auto stroke = makeRayStroke();
    stroke->setWidth(20.0);  // radius 60

    EXPECT_EQ(ANCHOR_A, grabbedAnchorCode(*stroke, Point(100, 155)));
    EXPECT_EQ(NO_ANCHOR, grabbedAnchorCode(*stroke, Point(100, 165)));
}

TEST(ModelLineShape, theNearerEndWins) {
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(40.0);  // radius 120, so both ends of a 100 unit shape are always in range
    stroke->addPoint(Point(0, 0));
    stroke->addPoint(Point(100, 0));
    stroke->setLineShape(LineShape{LineShapeType::ARROW, Point(0, 0), Point(100, 0)});

    EXPECT_EQ(ANCHOR_A, grabbedAnchorCode(*stroke, Point(40, 0)));
    EXPECT_EQ(ANCHOR_B, grabbedAnchorCode(*stroke, Point(60, 0)));
}

TEST(ModelLineShape, findGrabPicksTheTopmostShapeStroke) {
    Layer layer;

    auto plain = std::make_unique<Stroke>();
    plain->setWidth(DEFAULT_WIDTH);
    plain->addPoint(Point(100, 100));
    plain->addPoint(Point(200, 100));
    layer.addElement(std::move(plain));

    auto lower = makeRayStroke();
    Stroke* lowerPtr = lower.get();
    layer.addElement(std::move(lower));

    auto upper = makeRayStroke();
    Stroke* upperPtr = upper.get();
    layer.addElement(std::move(upper));

    auto grab = xoj::lineshape::findGrab(&layer, Point(100, 100));
    ASSERT_TRUE(grab.has_value());
    EXPECT_EQ(upperPtr, grab->stroke);
    EXPECT_NE(lowerPtr, grab->stroke);
    EXPECT_TRUE(grab->anchor == Anchor::A);

    EXPECT_FALSE(xoj::lineshape::findGrab(&layer, Point(150, 100)).has_value());
}

TEST(ModelLineShape, findGrabPrefersTheNearestEndOverTheTopmostStroke) {
    Layer layer;

    // A thin ray with anchor A at (100, 100), grab radius 10
    auto thin = makeRayStroke();
    Stroke* thinPtr = thin.get();
    layer.addElement(std::move(thin));

    // A wide shape drawn afterwards, so it is on top. Its grab radius of 60 reaches back over
    // the thin ray's anchor A, but its own nearest end is 55 units away from it.
    auto wide = std::make_unique<Stroke>();
    wide->setWidth(20.0);
    wide->addPoint(Point(155, 100));
    wide->addPoint(Point(255, 100));
    wide->setLineShape(LineShape{LineShapeType::RAY, Point(155, 100), Point(255, 100)});
    Stroke* widePtr = wide.get();
    layer.addElement(std::move(wide));

    // A press dead on the thin ray's anchor grabs it, not the wide shape lying on top
    auto grab = xoj::lineshape::findGrab(&layer, Point(100, 100));
    ASSERT_TRUE(grab.has_value());
    EXPECT_EQ(thinPtr, grab->stroke);
    EXPECT_TRUE(grab->anchor == Anchor::A);

    // Where the wide shape really is the nearer one, it still wins
    grab = xoj::lineshape::findGrab(&layer, Point(150, 100));
    ASSERT_TRUE(grab.has_value());
    EXPECT_EQ(widePtr, grab->stroke);
    EXPECT_TRUE(grab->anchor == Anchor::A);
}

TEST(ModelLineShape, degenerateShapesGetNoMetadata) {
    using xoj::lineshape::makeIfMeaningful;

    // Two anchors under the same pixel describe a dot, not a line
    EXPECT_FALSE(makeIfMeaningful(LineShapeType::RAY, Point(50, 50), Point(50, 50)).has_value());
    EXPECT_FALSE(makeIfMeaningful(LineShapeType::ARROW, Point(50, 50), Point(50.25, 50.25)).has_value());

    const auto shape = makeIfMeaningful(LineShapeType::RAY, Point(50, 50), Point(50, 80));
    ASSERT_TRUE(shape.has_value());
    EXPECT_EQ(LineShapeType::RAY, shape->type);
    EXPECT_DOUBLE_EQ(80.0, shape->anchorB.y);
}

TEST(ModelLineShape, projectOntoAxisDiscardsSidewaysTravel) {
    using xoj::lineshape::projectOntoAxis;

    const Point fixedAnchor(100, 100);
    const Point grabbedAnchor(200, 100);

    // Pure sideways travel leaves the anchor where it was
    Point p = projectOntoAxis(fixedAnchor, grabbedAnchor, Point(200, 160));
    EXPECT_DOUBLE_EQ(200.0, p.x);
    EXPECT_DOUBLE_EQ(100.0, p.y);

    // Diagonal travel keeps only the along-axis part
    p = projectOntoAxis(fixedAnchor, grabbedAnchor, Point(260, 130));
    EXPECT_DOUBLE_EQ(260.0, p.x);
    EXPECT_DOUBLE_EQ(100.0, p.y);

    // Dragging past the fixed anchor stays on the axis too
    p = projectOntoAxis(fixedAnchor, grabbedAnchor, Point(40, 90));
    EXPECT_DOUBLE_EQ(40.0, p.x);
    EXPECT_DOUBLE_EQ(100.0, p.y);
}

TEST(ModelLineShape, projectOntoAxisOnASlantedAxis) {
    // A 45 degree axis through the origin: (100, 0) projects onto its midpoint
    const Point p = xoj::lineshape::projectOntoAxis(Point(0, 0), Point(100, 100), Point(100, 0));
    EXPECT_DOUBLE_EQ(50.0, p.x);
    EXPECT_DOUBLE_EQ(50.0, p.y);
}

TEST(ModelLineShape, projectOntoAxisWithoutAnAxisReturnsTheDraggedPoint) {
    // Anchors closer than MIN_ANCHOR_SEPARATION give no direction to project onto
    const Point p = xoj::lineshape::projectOntoAxis(Point(100, 100), Point(100.2, 100.2), Point(300, 50));
    EXPECT_DOUBLE_EQ(300.0, p.x);
    EXPECT_DOUBLE_EQ(50.0, p.y);
}

TEST(ModelLineShape, cloningKeepsTheMetadataButSectionsDropIt) {
    const auto stroke = makeRayStroke();

    const auto clone = stroke->cloneStroke();
    ASSERT_TRUE(clone->getLineShape().has_value());
    EXPECT_EQ(LineShapeType::RAY, clone->getLineShape()->type);
    EXPECT_DOUBLE_EQ(100.0, clone->getLineShape()->anchorA.x);
    EXPECT_DOUBLE_EQ(200.0, clone->getLineShape()->anchorB.x);

    // A partial copy no longer matches the recorded anchors, so it carries no shape
    const auto section = stroke->cloneSection(PathParameter(0, 0.0), PathParameter(1, 0.5));
    EXPECT_FALSE(section->getLineShape().has_value());
}

TEST(ModelLineShape, anchorsFollowTheStroke) {
    auto stroke = makeRayStroke();

    stroke->move(10.0, -5.0);
    ASSERT_TRUE(stroke->getLineShape().has_value());
    EXPECT_DOUBLE_EQ(110.0, stroke->getLineShape()->anchorA.x);
    EXPECT_DOUBLE_EQ(95.0, stroke->getLineShape()->anchorA.y);
    EXPECT_DOUBLE_EQ(210.0, stroke->getLineShape()->anchorB.x);
    EXPECT_DOUBLE_EQ(95.0, stroke->getLineShape()->anchorB.y);

    stroke->scale(0.0, 0.0, 2.0, 1.0, 0.0, false);
    EXPECT_DOUBLE_EQ(220.0, stroke->getLineShape()->anchorA.x);
    EXPECT_DOUBLE_EQ(95.0, stroke->getLineShape()->anchorA.y);
    EXPECT_DOUBLE_EQ(420.0, stroke->getLineShape()->anchorB.x);
}

}  // namespace
