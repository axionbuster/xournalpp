#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <cairo.h>
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
class TestRepaintable final: public xoj::view::Repaintable {
public:
    Range getVisiblePart() const override { return visiblePart; }
    double getZoom() const override { return 1.0; }
    ZoomControl* getZoomControl() const override { return nullptr; }
    double getWidth() const override { return 64.0; }
    double getHeight() const override { return 64.0; }

    xoj::util::Rectangle<double> toWidgetCoordinates(const xoj::util::Rectangle<double>& r) const override { return r; }

    void flagDirtyRegion(const Range&) const override {}
    void drawAndDeleteToolView(xoj::view::ToolView*, const Range&) override {}
    void deleteOverlayView(xoj::view::OverlayView*, const Range&) override {}

    Range visiblePart;
};

class TestStrokeToolView final: public xoj::view::StrokeToolView {
    using Pool = xoj::util::DispatchPool<xoj::view::StrokeToolView>;

public:
    TestStrokeToolView(const Stroke& stroke, xoj::view::Repaintable* parent):
            TestStrokeToolView(stroke, parent, std::make_shared<Pool>()) {}

    size_t pendingPointCount() const { return this->pointBuffer.size(); }

private:
    TestStrokeToolView(const Stroke& stroke, xoj::view::Repaintable* parent, std::shared_ptr<Pool> viewPool):
            StrokeToolView(nullptr, stroke, parent, viewPool), viewPool(std::move(viewPool)) {}

    std::shared_ptr<Pool> viewPool;
};

bool surfaceHasInk(cairo_surface_t* surface) {
    cairo_surface_flush(surface);
    const auto* data = cairo_image_surface_get_data(surface);
    const auto dataSize = static_cast<size_t>(cairo_image_surface_get_stride(surface)) *
                          static_cast<size_t>(cairo_image_surface_get_height(surface));
    return std::any_of(data, data + dataSize, [](uint8_t byte) { return byte != 0U; });
}
};  // namespace

TEST(StrokeToolViewTest, PreservesPendingPointsUntilMaskCreationSucceeds) {
    Stroke stroke;
    stroke.setColor(Colors::black);
    stroke.setWidth(2.0);
    stroke.addPoint(Point(8.0, 8.0));

    TestRepaintable parent;
    TestStrokeToolView view(stroke, &parent);
    view.on(xoj::view::StrokeToolView::ADD_POINT_REQUEST, Point(48.0, 8.0));

    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64), xoj::util::adopt);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);

    view.draw(cr.get());
    EXPECT_EQ(view.pendingPointCount(), 2U);
    EXPECT_FALSE(surfaceHasInk(surface.get()));

    parent.visiblePart = Range(0.0, 0.0, 63.0, 63.0);
    view.draw(cr.get());
    EXPECT_EQ(view.pendingPointCount(), 1U);
    EXPECT_TRUE(surfaceHasInk(surface.get()));
}
