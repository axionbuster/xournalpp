#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include <cairo.h>
#include <glib.h>
#include <gtest/gtest.h>

#include "model/Stroke.h"
#include "util/Color.h"
#include "util/DispatchPool.h"
#include "util/Range.h"
#include "util/Rectangle.h"
#include "util/raii/CairoWrappers.h"
#include "view/Repaintable.h"
#include "view/overlays/StrokeToolView.h"

namespace {
constexpr int FRAME_WIDTH = 1920;
constexpr int FRAME_HEIGHT = 1080;
constexpr int FRAME_COUNT = 60;
constexpr size_t POINT_COUNT = 10'000;

class BenchmarkRepaintable final: public xoj::view::Repaintable {
public:
    Range getVisiblePart() const override { return Range(0.0, 0.0, FRAME_WIDTH, FRAME_HEIGHT); }
    double getZoom() const override { return 1.0; }
    ZoomControl* getZoomControl() const override { return nullptr; }
    double getWidth() const override { return FRAME_WIDTH; }
    double getHeight() const override { return FRAME_HEIGHT; }
    xoj::util::Rectangle<double> toWidgetCoordinates(const xoj::util::Rectangle<double>& r) const override { return r; }
    void flagDirtyRegion(const Range&) const override {}
    void drawAndDeleteToolView(xoj::view::ToolView*, const Range&) override {}
    void deleteOverlayView(xoj::view::OverlayView*, const Range&) override {}
};

class BenchmarkStrokeToolView final: public xoj::view::StrokeToolView {
    using Pool = xoj::util::DispatchPool<xoj::view::StrokeToolView>;

public:
    BenchmarkStrokeToolView(const Stroke& stroke, xoj::view::Repaintable* parent):
            BenchmarkStrokeToolView(stroke, parent, std::make_shared<Pool>()) {}

private:
    BenchmarkStrokeToolView(const Stroke& stroke, xoj::view::Repaintable* parent, std::shared_ptr<Pool> viewPool):
            StrokeToolView(nullptr, stroke, parent, viewPool), viewPool(std::move(viewPool)) {}

    std::shared_ptr<Pool> viewPool;
};

auto runFrames(const xoj::view::StrokeToolView& view, cairo_t* cr, bool cleanFrame) -> gint64 {
    const gint64 start = g_get_monotonic_time();
    for (int frame = 0; frame < FRAME_COUNT; frame++) {
        if (cleanFrame) {
            view.drawForFrame(cr);
        } else {
            view.draw(cr);
        }
        cairo_surface_flush(cairo_get_target(cr));
    }
    return g_get_monotonic_time() - start;
}

auto runGrowingFrames(Stroke& stroke, xoj::view::StrokeToolView& view, cairo_t* cr) -> gint64 {
    const gint64 start = g_get_monotonic_time();
    for (int frame = 0; frame < FRAME_COUNT; frame++) {
        const double phase = static_cast<double>(frame) * 0.2;
        const Point point(1900.0 - 2.0 * frame, FRAME_HEIGHT / 2.0 + 200.0 * std::sin(phase),
                          1.0 + 4.0 * (0.5 + 0.5 * std::sin(phase * 0.7)));
        stroke.addPoint(point);
        view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, point);
        view.drawForFrame(cr);
        cairo_surface_flush(cairo_get_target(cr));
    }
    return g_get_monotonic_time() - start;
}
}  // namespace

TEST(StrokeToolViewBenchmark, LongPressureStrokeAt1080p60) {
    Stroke stroke;
    stroke.setColor(Colors::black);
    stroke.setWidth(5.0);
    stroke.addPoint(Point(20.0, FRAME_HEIGHT / 2.0, 2.0));

    BenchmarkRepaintable parent;
    BenchmarkStrokeToolView view(stroke, &parent);
    for (size_t i = 1; i < POINT_COUNT; i++) {
        const double phase = static_cast<double>(i) * 0.015;
        const Point point(20.0 + 1880.0 * static_cast<double>(i) / static_cast<double>(POINT_COUNT - 1),
                          FRAME_HEIGHT / 2.0 + 400.0 * std::sin(phase),
                          1.0 + 4.0 * (0.5 + 0.5 * std::sin(phase * 0.7)));
        stroke.addPoint(point);
        view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, point);
    }

    xoj::util::CairoSurfaceSPtr liveSurface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, FRAME_WIDTH, FRAME_HEIGHT),
                                            xoj::util::adopt);
    xoj::util::CairoSPtr liveCr(cairo_create(liveSurface.get()), xoj::util::adopt);
    xoj::util::CairoSurfaceSPtr frameSurface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, FRAME_WIDTH, FRAME_HEIGHT),
                                             xoj::util::adopt);
    xoj::util::CairoSPtr frameCr(cairo_create(frameSurface.get()), xoj::util::adopt);

    view.draw(liveCr.get());  // Build the incremental live mask before timing its steady-state blit.
    view.drawForFrame(frameCr.get());

    std::vector<gint64> liveSamples;
    std::vector<gint64> cleanFrameSamples;
    for (int sample = 0; sample < 5; sample++) {
        liveSamples.emplace_back(runFrames(view, liveCr.get(), false));
        cleanFrameSamples.emplace_back(runFrames(view, frameCr.get(), true));
    }
    std::ranges::sort(liveSamples);
    std::ranges::sort(cleanFrameSamples);
    const gint64 liveMedian = liveSamples[liveSamples.size() / 2];
    const gint64 cleanFrameMedian = cleanFrameSamples[cleanFrameSamples.size() / 2];
    const gint64 growingCleanFrame = runGrowingFrames(stroke, view, frameCr.get());

    std::cout << "1080p/60, 10k pressure points: incremental mask " << static_cast<double>(liveMedian) / 1000.0
              << " ms; clean-frame renderer " << static_cast<double>(cleanFrameMedian) / 1000.0
              << " ms; clean frame with one appended point/frame " << static_cast<double>(growingCleanFrame) / 1000.0
              << " ms\n";

    // This benchmark is not part of CTest, but keep an explicit alarm for manual performance runs.
    EXPECT_LT(cleanFrameMedian, 1'000'000);              // One second of CPU per rendered second (60 frames).
    EXPECT_LT(growingCleanFrame, 3 * cleanFrameMedian);  // Catch whole-stroke reconstruction on every append.
}
