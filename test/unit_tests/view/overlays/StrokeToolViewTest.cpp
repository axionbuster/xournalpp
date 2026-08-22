#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cairo.h>
#include <gtest/gtest.h>

#include "model/LineStyle.h"
#include "model/Stroke.h"
#include "util/Color.h"
#include "util/DispatchPool.h"
#include "util/Range.h"
#include "util/Rectangle.h"
#include "util/raii/CairoWrappers.h"
#include "view/Repaintable.h"
#include "view/StrokeView.h"
#include "view/View.h"
#include "view/overlays/LaserPointerView.h"
#include "view/overlays/StrokeToolFilledHighlighterView.h"
#include "view/overlays/StrokeToolView.h"

namespace {
class TestRepaintable final: public xoj::view::Repaintable {
public:
    Range getVisiblePart() const override { return visiblePart; }
    double getZoom() const override { return 1.0; }
    ZoomControl* getZoomControl() const override { return nullptr; }
    double getWidth() const override { return width; }
    double getHeight() const override { return height; }

    xoj::util::Rectangle<double> toWidgetCoordinates(const xoj::util::Rectangle<double>& r) const override { return r; }

    void flagDirtyRegion(const Range&) const override {}
    void drawAndDeleteToolView(xoj::view::ToolView*, const Range&) override {}
    void deleteOverlayView(xoj::view::OverlayView*, const Range&) override {}

    Range visiblePart;
    double width = 64.0;
    double height = 64.0;
};

class TestStrokeToolView final: public xoj::view::StrokeToolView {
    using Pool = xoj::util::DispatchPool<xoj::view::StrokeToolView>;

public:
    TestStrokeToolView(const Stroke& stroke, xoj::view::Repaintable* parent):
            TestStrokeToolView(stroke, parent, std::make_shared<Pool>()) {}

    size_t pendingPointCount() const { return this->pointBuffer.size(); }
    bool hasInteractiveMask() const { return this->mask.isInitialized(); }
    double currentDashOffset() const { return this->dashOffset; }
    size_t cleanFrameMaskCount() const { return this->frameMasks.size(); }

private:
    TestStrokeToolView(const Stroke& stroke, xoj::view::Repaintable* parent, std::shared_ptr<Pool> viewPool):
            StrokeToolView(nullptr, stroke, parent, viewPool), viewPool(std::move(viewPool)) {}

    std::shared_ptr<Pool> viewPool;
};

class TestFilledHighlighterView final: public xoj::view::StrokeToolFilledHighlighterView {
    using Pool = xoj::util::DispatchPool<xoj::view::StrokeToolView>;

public:
    TestFilledHighlighterView(const Stroke& stroke, xoj::view::Repaintable* parent):
            TestFilledHighlighterView(stroke, parent, std::make_shared<Pool>()) {}

    size_t pendingPointCount() const { return this->pointBuffer.size(); }
    size_t fillingPointCount() const { return this->filling.contour.size(); }
    bool hasInteractiveMask() const { return this->mask.isInitialized(); }

private:
    TestFilledHighlighterView(const Stroke& stroke, xoj::view::Repaintable* parent, std::shared_ptr<Pool> viewPool):
            StrokeToolFilledHighlighterView(nullptr, stroke, parent, viewPool), viewPool(std::move(viewPool)) {}

    std::shared_ptr<Pool> viewPool;
};

class TestLaserPointerView final: public xoj::view::LaserPointerView {
    using Pool = xoj::util::DispatchPool<xoj::view::LaserPointerView>;

public:
    explicit TestLaserPointerView(xoj::view::Repaintable* parent):
            TestLaserPointerView(parent, std::make_shared<Pool>()) {}

    void setActiveStroke(std::unique_ptr<xoj::view::StrokeToolView> view, const Stroke* stroke) {
        this->activeStrokeView = std::move(view);
        this->activeStrokeModel = stroke;
    }
    bool hasLaserMask() const { return this->mask.isInitialized(); }
    size_t finishedStrokeCount() const { return this->finishedStrokes.size(); }

private:
    TestLaserPointerView(xoj::view::Repaintable* parent, std::shared_ptr<Pool> viewPool):
            LaserPointerView(nullptr, parent, viewPool), viewPool(std::move(viewPool)) {}

    std::shared_ptr<Pool> viewPool;
};

bool surfaceHasInk(cairo_surface_t* surface) {
    cairo_surface_flush(surface);
    const auto* data = cairo_image_surface_get_data(surface);
    const auto dataSize = static_cast<size_t>(cairo_image_surface_get_stride(surface)) *
                          static_cast<size_t>(cairo_image_surface_get_height(surface));
    return std::any_of(data, data + dataSize, [](uint8_t byte) { return byte != 0U; });
}

void clearSurface(cairo_t* cr) {
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);
}

auto surfaceBytes(cairo_surface_t* surface) -> std::vector<uint8_t> {
    cairo_surface_flush(surface);
    const auto* data = cairo_image_surface_get_data(surface);
    const auto dataSize = static_cast<size_t>(cairo_image_surface_get_stride(surface)) *
                          static_cast<size_t>(cairo_image_surface_get_height(surface));
    return {data, data + dataSize};
}

auto alphaAt(cairo_surface_t* surface, int x, int y) -> uint8_t {
    cairo_surface_flush(surface);
    const auto* pixel = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(surface) +
                                                          y * cairo_image_surface_get_stride(surface));
    return static_cast<uint8_t>(pixel[x] >> 24U);
}

void expectIncrementalPreviewNearSettledRenderer(const std::vector<uint8_t>& actual,
                                                 const std::vector<uint8_t>& expected) {
    ASSERT_EQ(actual.size(), expected.size());
    size_t differingBytes = 0;
    int maximumDelta = 0;
    for (size_t i = 0; i < actual.size(); i++) {
        const int delta = std::abs(static_cast<int>(actual[i]) - static_cast<int>(expected[i]));
        if (delta != 0) {
            differingBytes++;
            maximumDelta = std::max(maximumDelta, delta);
        }
    }
    // Separately antialiased tail batches can differ at a handful of edge pixels from Cairo's one-path rasterization.
    // Bound that local preview seam while still catching geometry, alpha, or cross-consumer cache corruption.
    EXPECT_LE(differingBytes, 256U) << "maximum channel delta: " << maximumDelta;
    EXPECT_LE(maximumDelta, 80);
}

auto renderReference(const Stroke& stroke, int width = 64, int height = 64, double scale = 1.0,
                     double deviceScale = 1.0) -> std::vector<uint8_t> {
    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height),
                                        xoj::util::adopt);
    cairo_surface_set_device_scale(surface.get(), deviceScale, deviceScale);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
    cairo_scale(cr.get(), scale, scale);
    xoj::view::StrokeView(&stroke).draw(xoj::view::Context::createDefault(cr.get()));
    return surfaceBytes(surface.get());
}

auto renderCleanFrame(const Stroke& stroke, int width = 64, int height = 64) -> std::vector<uint8_t> {
    TestRepaintable parent;
    TestStrokeToolView view(stroke, &parent);
    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height),
                                        xoj::util::adopt);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
    view.drawForFrame(cr.get());
    return surfaceBytes(surface.get());
}
};  // namespace

TEST(StrokeToolViewTest, RecorderAndLiveCanvasRenderPendingInkBeforeVisibleRangeRecovers) {
    Stroke stroke;
    stroke.setColor(Colors::black);
    stroke.setWidth(2.0);
    stroke.addPoint(Point(8.0, 8.0));

    TestRepaintable parent;
    TestStrokeToolView view(stroke, &parent);
    const Point endpoint(48.0, 8.0);
    stroke.addPoint(endpoint);  // The controller updates the model before dispatching to its views.
    view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, endpoint);

    xoj::util::CairoSurfaceSPtr recorderSurface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64),
                                                xoj::util::adopt);
    xoj::util::CairoSPtr recorderCr(cairo_create(recorderSurface.get()), xoj::util::adopt);
    xoj::util::CairoSurfaceSPtr liveSurface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64), xoj::util::adopt);
    xoj::util::CairoSPtr liveCr(cairo_create(liveSurface.get()), xoj::util::adopt);

    // A recording timer may run before GTK has completed allocation/visibility bookkeeping. It must paint the model
    // snapshot without stealing the live view's pending points or initializing its destination-specific mask.
    view.drawForFrame(recorderCr.get());
    EXPECT_EQ(view.pendingPointCount(), 2U);
    EXPECT_FALSE(view.hasInteractiveMask());
    EXPECT_TRUE(surfaceHasInk(recorderSurface.get()));

    // A live GTK damage callback is itself proof of a valid destination. Even while getVisiblePart() is stale, it must
    // render immediately and remain state-neutral so mask recovery can consume every point exactly once.
    view.draw(liveCr.get());
    EXPECT_EQ(view.pendingPointCount(), 2U);
    EXPECT_FALSE(view.hasInteractiveMask());
    EXPECT_TRUE(surfaceHasInk(liveSurface.get()));

    parent.visiblePart = Range(0.0, 0.0, 63.0, 63.0);
    clearSurface(liveCr.get());
    view.draw(liveCr.get());
    EXPECT_EQ(view.pendingPointCount(), 1U);
    EXPECT_TRUE(view.hasInteractiveMask());
    EXPECT_TRUE(surfaceHasInk(liveSurface.get()));
}

TEST(StrokeToolViewTest, RecorderAfterInteractiveMaskDoesNotConsumeNewPendingInk) {
    Stroke stroke;
    stroke.setColor(Colors::black);
    stroke.setWidth(3.0);
    LineStyle dashed;
    dashed.setDashes({6.0, 3.0});
    stroke.setLineStyle(dashed);
    stroke.addPoint(Point(8.0, 8.0));

    TestRepaintable parent;
    parent.visiblePart = Range(0.0, 0.0, 63.0, 63.0);
    TestStrokeToolView view(stroke, &parent);

    const Point second(40.0, 8.0);
    stroke.addPoint(second);
    view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, second);

    xoj::util::CairoSurfaceSPtr liveSurface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64), xoj::util::adopt);
    xoj::util::CairoSPtr liveCr(cairo_create(liveSurface.get()), xoj::util::adopt);
    view.draw(liveCr.get());
    ASSERT_TRUE(view.hasInteractiveMask());
    ASSERT_EQ(view.pendingPointCount(), 1U);
    const double dashBeforeRecorder = view.currentDashOffset();

    const Point third(40.0, 48.0);
    stroke.addPoint(third);
    view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, third);

    xoj::util::CairoSurfaceSPtr recorderSurface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64),
                                                xoj::util::adopt);
    xoj::util::CairoSPtr recorderCr(cairo_create(recorderSurface.get()), xoj::util::adopt);
    view.drawForFrame(recorderCr.get());

    EXPECT_EQ(surfaceBytes(recorderSurface.get()), renderReference(stroke));
    EXPECT_EQ(view.pendingPointCount(), 2U);
    EXPECT_TRUE(view.hasInteractiveMask());
    EXPECT_DOUBLE_EQ(view.currentDashOffset(), dashBeforeRecorder);

    clearSurface(liveCr.get());
    view.draw(liveCr.get());
    EXPECT_EQ(view.pendingPointCount(), 1U);
    EXPECT_TRUE(view.hasInteractiveMask());
    EXPECT_GT(view.currentDashOffset(), dashBeforeRecorder);
    EXPECT_TRUE(surfaceHasInk(liveSurface.get()));
}

TEST(StrokeToolViewTest, IncrementalCleanFrameMatchesWholeStrokeAcrossSuccessiveUpdates) {
    for (const bool pressureSensitive: {false, true}) {
        SCOPED_TRACE(pressureSensitive ? "pressure" : "constant width");
        Stroke stroke;
        stroke.setColor(Colors::black);
        stroke.setWidth(5.0);
        stroke.addPoint(Point(8.0, 8.0, pressureSensitive ? 2.0 : Point::NO_PRESSURE));

        TestRepaintable parent;
        TestStrokeToolView view(stroke, &parent);
        xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64), xoj::util::adopt);
        xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);

        const Point second(40.0, 8.0, pressureSensitive ? 4.0 : Point::NO_PRESSURE);
        stroke.addPoint(second);
        view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, second);
        view.drawForFrame(cr.get());

        for (const Point& point: {Point(52.0, 32.0, pressureSensitive ? 1.5 : Point::NO_PRESSURE),
                                  Point(20.0, 50.0, pressureSensitive ? 5.0 : Point::NO_PRESSURE)}) {
            stroke.addPoint(point);
            view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, point);
            clearSurface(cr.get());
            view.drawForFrame(cr.get());
        }

        expectIncrementalPreviewNearSettledRenderer(surfaceBytes(surface.get()), renderReference(stroke));
        EXPECT_EQ(view.pendingPointCount(), 4U);
        EXPECT_FALSE(view.hasInteractiveMask());
    }
}

TEST(StrokeToolViewTest, DifferentScaleFrameConsumersKeepIndependentIncrementalMasks) {
    Stroke stroke;
    stroke.setColor(Colors::black);
    stroke.setWidth(3.0);
    stroke.addPoint(Point(8.0, 8.0, 2.0));

    TestRepaintable parent;
    TestStrokeToolView view(stroke, &parent);
    xoj::util::CairoSurfaceSPtr oneXSurface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64), xoj::util::adopt);
    xoj::util::CairoSPtr oneXCr(cairo_create(oneXSurface.get()), xoj::util::adopt);
    xoj::util::CairoSurfaceSPtr twoXSurface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 128, 128),
                                            xoj::util::adopt);
    cairo_surface_set_device_scale(twoXSurface.get(), 2.0, 2.0);
    xoj::util::CairoSPtr twoXCr(cairo_create(twoXSurface.get()), xoj::util::adopt);

    const Point second(40.0, 8.0, 4.0);
    stroke.addPoint(second);
    view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, second);
    view.drawForFrame(oneXCr.get());

    const Point third(40.0, 40.0, 2.5);
    stroke.addPoint(third);
    view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, third);
    view.drawForFrame(twoXCr.get());

    const Point fourth(12.0, 52.0, 3.5);
    stroke.addPoint(fourth);
    view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, fourth);
    clearSurface(oneXCr.get());
    view.drawForFrame(oneXCr.get());
    clearSurface(twoXCr.get());
    view.drawForFrame(twoXCr.get());

    expectIncrementalPreviewNearSettledRenderer(surfaceBytes(oneXSurface.get()), renderReference(stroke));
    expectIncrementalPreviewNearSettledRenderer(surfaceBytes(twoXSurface.get()),
                                                renderReference(stroke, 128, 128, 1.0, 2.0));
    EXPECT_EQ(view.pendingPointCount(), 4U);
    EXPECT_FALSE(view.hasInteractiveMask());
}

TEST(StrokeToolViewTest, IncrementalHighlighterSelfOverlapMatchesSingleMultiplyComposite) {
    Stroke stroke;
    stroke.setColor(Colors::xopp_royalblue);
    stroke.setWidth(8.0);
    stroke.setToolType(StrokeTool::HIGHLIGHTER);
    stroke.addPoint(Point(8.0, 8.0));

    TestRepaintable parent;
    TestStrokeToolView view(stroke, &parent);
    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64), xoj::util::adopt);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);

    for (const Point& point: {Point(52.0, 52.0), Point(8.0, 52.0), Point(52.0, 8.0)}) {
        stroke.addPoint(point);
        view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, point);
        clearSurface(cr.get());
        view.drawForFrame(cr.get());
    }

    expectIncrementalPreviewNearSettledRenderer(surfaceBytes(surface.get()), renderReference(stroke));
    EXPECT_EQ(view.pendingPointCount(), 4U);
    EXPECT_FALSE(view.hasInteractiveMask());
    EXPECT_NEAR(alphaAt(surface.get(), 30, 30), 120, 2);
}

TEST(StrokeToolViewTest, ThirdOutputScaleEvictsAndRebuildsLeastRecentlyUsedMask) {
    Stroke stroke;
    stroke.setColor(Colors::black);
    stroke.setWidth(3.0);
    stroke.addPoint(Point(8.0, 8.0, 2.0));

    TestRepaintable parent;
    TestStrokeToolView view(stroke, &parent);
    const Point second(40.0, 8.0, 4.0);
    stroke.addPoint(second);
    view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, second);

    xoj::util::CairoSurfaceSPtr oneX(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64), xoj::util::adopt);
    xoj::util::CairoSPtr oneXCr(cairo_create(oneX.get()), xoj::util::adopt);
    xoj::util::CairoSurfaceSPtr twoX(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 128, 128), xoj::util::adopt);
    xoj::util::CairoSPtr twoXCr(cairo_create(twoX.get()), xoj::util::adopt);
    cairo_scale(twoXCr.get(), 2.0, 2.0);
    xoj::util::CairoSurfaceSPtr threeX(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 192, 192), xoj::util::adopt);
    xoj::util::CairoSPtr threeXCr(cairo_create(threeX.get()), xoj::util::adopt);
    cairo_scale(threeXCr.get(), 3.0, 3.0);

    view.drawForFrame(oneXCr.get());
    view.drawForFrame(twoXCr.get());
    ASSERT_EQ(view.cleanFrameMaskCount(), 2U);

    const Point third(40.0, 48.0, 2.5);
    stroke.addPoint(third);
    view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, third);
    view.drawForFrame(threeXCr.get());
    EXPECT_EQ(view.cleanFrameMaskCount(), 2U);

    clearSurface(oneXCr.get());
    view.drawForFrame(oneXCr.get());
    EXPECT_EQ(view.cleanFrameMaskCount(), 2U);
    expectIncrementalPreviewNearSettledRenderer(surfaceBytes(oneX.get()), renderReference(stroke));
    expectIncrementalPreviewNearSettledRenderer(surfaceBytes(threeX.get()), renderReference(stroke, 192, 192, 3.0));
}

TEST(StrokeToolViewTest, HugeTranslatedClipFallsBackWithoutOversizedMask) {
    constexpr double OFFSET = 3.0e9;
    Stroke stroke;
    stroke.setColor(Colors::black);
    stroke.setWidth(3.0);
    stroke.addPoint(Point(OFFSET + 8.0, 8.0));
    stroke.addPoint(Point(OFFSET + 48.0, 8.0));

    TestRepaintable parent;
    TestStrokeToolView view(stroke, &parent);
    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64), xoj::util::adopt);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
    cairo_translate(cr.get(), -OFFSET, 0.0);

    view.drawForFrame(cr.get());

    EXPECT_TRUE(surfaceHasInk(surface.get()));
    EXPECT_EQ(view.cleanFrameMaskCount(), 0U);
    EXPECT_EQ(view.pendingPointCount(), 2U);
}

TEST(StrokeToolViewTest, LaserPointerCleanFrameDoesNotConsumeNestedActiveStroke) {
    Stroke stroke;
    stroke.setColor(Colors::red);
    stroke.setWidth(3.0);
    stroke.addPoint(Point(8.0, 8.0));

    TestRepaintable parent;
    parent.visiblePart = Range(0.0, 0.0, 63.0, 63.0);
    auto activeView = std::make_unique<TestStrokeToolView>(stroke, &parent);
    TestStrokeToolView* activeViewPtr = activeView.get();
    const Point endpoint(48.0, 8.0);
    stroke.addPoint(endpoint);
    activeView->on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, endpoint);

    TestLaserPointerView laserView(&parent);
    laserView.setActiveStroke(std::move(activeView), &stroke);

    xoj::util::CairoSurfaceSPtr recorderSurface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64),
                                                xoj::util::adopt);
    xoj::util::CairoSPtr recorderCr(cairo_create(recorderSurface.get()), xoj::util::adopt);
    laserView.drawForFrame(recorderCr.get());
    EXPECT_TRUE(surfaceHasInk(recorderSurface.get()));
    EXPECT_EQ(activeViewPtr->pendingPointCount(), 2U);
    EXPECT_FALSE(activeViewPtr->hasInteractiveMask());
    EXPECT_FALSE(laserView.hasLaserMask());

    xoj::util::CairoSurfaceSPtr liveSurface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64), xoj::util::adopt);
    xoj::util::CairoSPtr liveCr(cairo_create(liveSurface.get()), xoj::util::adopt);
    laserView.draw(liveCr.get());
    EXPECT_TRUE(surfaceHasInk(liveSurface.get()));
    EXPECT_EQ(activeViewPtr->pendingPointCount(), 1U);
    EXPECT_TRUE(activeViewPtr->hasInteractiveMask());
    EXPECT_TRUE(laserView.hasLaserMask());
}

TEST(StrokeToolViewTest, LaserPointerFinishBeforeLiveDrawRemainsInCleanFrames) {
    Stroke stroke;
    stroke.setColor(Colors::red);
    stroke.setWidth(3.0);
    stroke.addPoint(Point(8.0, 8.0));

    TestRepaintable parent;
    auto activeView = std::make_unique<TestStrokeToolView>(stroke, &parent);
    const Point endpoint(48.0, 8.0);
    stroke.addPoint(endpoint);
    activeView->on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, endpoint);

    TestLaserPointerView laserView(&parent);
    laserView.setActiveStroke(std::move(activeView), &stroke);

    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64), xoj::util::adopt);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
    laserView.drawForFrame(cr.get());
    ASSERT_TRUE(surfaceHasInk(surface.get()));
    ASSERT_FALSE(laserView.hasLaserMask());

    laserView.on(xoj::view::LaserPointerView::FINISH_STROKE_REQUEST, Range(stroke.getBoundingBox()));
    EXPECT_EQ(laserView.finishedStrokeCount(), 1U);
    EXPECT_FALSE(laserView.hasLaserMask());

    clearSurface(cr.get());
    laserView.drawForFrame(cr.get());
    EXPECT_TRUE(surfaceHasInk(surface.get()));
    EXPECT_EQ(surfaceBytes(surface.get()), renderReference(stroke));

    // A later valid GTK draw initializes the live laser mask from the retained model too.
    parent.visiblePart = Range(0.0, 0.0, 63.0, 63.0);
    clearSurface(cr.get());
    laserView.draw(cr.get());
    EXPECT_TRUE(laserView.hasLaserMask());
    EXPECT_TRUE(surfaceHasInk(surface.get()));
}

TEST(StrokeToolViewTest, ActualFilledHighlighterFallbackIsStateNeutralAndClipBounded) {
    Stroke stroke;
    stroke.setColor(Colors::xopp_royalblue);
    stroke.setWidth(4.0);
    stroke.setToolType(StrokeTool::HIGHLIGHTER);
    stroke.setFill(96);
    stroke.addPoint(Point(8.0, 8.0));

    TestRepaintable parent;
    // A full-page fallback mask would be catastrophically large here. The frame renderer's temporary alpha group is
    // instead limited by the 64x64 destination clip.
    parent.width = 1.0e9;
    parent.height = 1.0e9;
    TestFilledHighlighterView view(stroke, &parent);
    for (const Point& point: {Point(48.0, 8.0), Point(48.0, 48.0), Point(8.0, 48.0)}) {
        stroke.addPoint(point);
        view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, point);
    }

    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64), xoj::util::adopt);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);

    // Exercise StrokeToolFilledHighlighterView::draw(), not just the inherited clean-frame helper.
    view.draw(cr.get());
    const auto first = surfaceBytes(surface.get());
    EXPECT_EQ(view.pendingPointCount(), 4U);
    EXPECT_EQ(view.fillingPointCount(), 1U);
    EXPECT_FALSE(view.hasInteractiveMask());
    EXPECT_TRUE(surfaceHasInk(surface.get()));
    EXPECT_NEAR(alphaAt(surface.get(), 24, 24), 96, 2);

    clearSurface(cr.get());
    view.draw(cr.get());
    EXPECT_EQ(surfaceBytes(surface.get()), first);
    EXPECT_EQ(view.pendingPointCount(), 4U);
    EXPECT_EQ(view.fillingPointCount(), 1U);
    EXPECT_FALSE(view.hasInteractiveMask());

    parent.visiblePart = Range(0.0, 0.0, 63.0, 63.0);
    clearSurface(cr.get());
    view.draw(cr.get());
    EXPECT_EQ(view.pendingPointCount(), 1U);
    EXPECT_EQ(view.fillingPointCount(), 4U);
    EXPECT_TRUE(view.hasInteractiveMask());
    EXPECT_TRUE(surfaceHasInk(surface.get()));
}

TEST(StrokeToolViewTest, CleanFrameMatchesFinalStrokeRendererForStylesAndPressure) {
    std::vector<std::pair<std::string, Stroke>> cases;

    Stroke dashedPen;
    dashedPen.setColor(Colors::black);
    dashedPen.setWidth(5.0);
    dashedPen.setStrokeCapStyle(StrokeCapStyle::SQUARE);
    LineStyle dashed;
    dashed.setDashes({6.0, 3.0});
    dashedPen.setLineStyle(dashed);
    dashedPen.addPoint(Point(8.0, 10.0));
    dashedPen.addPoint(Point(52.0, 10.0));
    dashedPen.addPoint(Point(52.0, 28.0));
    cases.emplace_back("dashed square-cap pen", dashedPen);

    Stroke pressurePen;
    pressurePen.setColor(Colors::xopp_royalblue);
    pressurePen.setWidth(5.0);
    pressurePen.setStrokeCapStyle(StrokeCapStyle::BUTT);
    pressurePen.addPoint(Point(8.0, 12.0, 1.5));
    pressurePen.addPoint(Point(22.0, 42.0, 4.0));
    pressurePen.addPoint(Point(42.0, 20.0, 2.0));
    pressurePen.addPoint(Point(56.0, 50.0, 5.0));
    cases.emplace_back("pressure pen", pressurePen);

    Stroke filledPen;
    filledPen.setColor(Colors::xopp_royalblue);
    filledPen.setWidth(3.0);
    filledPen.setFill(112);
    filledPen.setStrokeCapStyle(StrokeCapStyle::SQUARE);
    filledPen.setLineStyle(dashed);
    filledPen.addPoint(Point(8.0, 8.0));
    filledPen.addPoint(Point(52.0, 8.0));
    filledPen.addPoint(Point(52.0, 48.0));
    filledPen.addPoint(Point(8.0, 48.0));
    cases.emplace_back("filled dashed pen", filledPen);

    Stroke filledHighlighter;
    filledHighlighter.setColor(Colors::xopp_royalblue);
    filledHighlighter.setWidth(4.0);
    filledHighlighter.setToolType(StrokeTool::HIGHLIGHTER);
    filledHighlighter.setFill(96);
    filledHighlighter.setStrokeCapStyle(StrokeCapStyle::BUTT);
    filledHighlighter.addPoint(Point(8.0, 8.0));
    filledHighlighter.addPoint(Point(52.0, 8.0));
    filledHighlighter.addPoint(Point(52.0, 48.0));
    filledHighlighter.addPoint(Point(8.0, 48.0));
    cases.emplace_back("filled highlighter", filledHighlighter);

    for (const auto& [description, stroke]: cases) {
        SCOPED_TRACE(description);
        EXPECT_EQ(renderCleanFrame(stroke), renderReference(stroke));
    }
}

TEST(StrokeToolViewTest, StrokeReplacementUpdatesCleanFrameModel) {
    Stroke original;
    original.setColor(Colors::black);
    original.setWidth(3.0);
    original.addPoint(Point(8.0, 8.0));
    original.addPoint(Point(48.0, 8.0));

    Stroke replacement;
    replacement.setColor(Colors::black);
    replacement.setWidth(6.0);
    replacement.addPoint(Point(8.0, 48.0));
    replacement.addPoint(Point(48.0, 48.0));

    TestRepaintable parent;
    TestStrokeToolView view(original, &parent);
    view.on(xoj::view::StrokeToolView::STROKE_REPLACEMENT_REQUEST, replacement);

    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64), xoj::util::adopt);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
    view.drawForFrame(cr.get());

    EXPECT_EQ(surfaceBytes(surface.get()), renderReference(replacement));
    EXPECT_NE(surfaceBytes(surface.get()), renderReference(original));
    EXPECT_EQ(view.pendingPointCount(), replacement.getPointCount());
}
