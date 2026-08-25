#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <cairo.h>
#include <gtest/gtest.h>

#include "model/Element.h"
#include "model/ElementContainer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "util/Color.h"
#include "util/Rectangle.h"
#include "util/raii/CairoWrappers.h"
#include "view/ElementContainerView.h"
#include "view/View.h"

namespace {

class TestElementContainer final: public ElementContainer {
public:
    void add(ElementPtr element) { this->elements.emplace_back(std::move(element)); }

    void forEachElement(std::function<void(const Element*)> f) const override {
        for (const auto& element: this->elements) {
            f(element.get());
        }
    }

private:
    std::vector<ElementPtr> elements;
};

auto alphaAt(cairo_surface_t* surface, int x, int y) -> std::uint8_t {
    cairo_surface_flush(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const auto* row = reinterpret_cast<const std::uint32_t*>(cairo_image_surface_get_data(surface) + y * stride);
    return static_cast<std::uint8_t>(row[x] >> 24U);
}

TEST(ElementContainerViewTest, DrawsMovedResizedAndRotatedSelectionContentWithoutEditingAids) {
    TestElementContainer container;
    auto stroke = std::make_unique<Stroke>();
    stroke->setColor(Colors::black);
    stroke->setWidth(2.0);
    stroke->addPoint(Point(12.0, 30.0));
    stroke->addPoint(Point(18.0, 30.0));
    container.add(std::move(stroke));

    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 120, 100), xoj::util::adopt);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);

    // Map the original selection from {10, 20, 20, 20} to {50, 40, 40, 20},
    // then rotate it 90 degrees around the transformed content's centre. This is
    // the same transform composition EditSelection uses for clean output frames.
    cairo_translate(cr.get(), 60.0, 50.0);
    cairo_rotate(cr.get(), 1.5707963267948966);
    cairo_translate(cr.get(), -60.0, -50.0);
    xoj::view::ElementContainerView(&container)
            .drawTransformed(cr.get(), xoj::util::Rectangle<double>{10.0, 20.0, 20.0, 20.0},
                             xoj::util::Rectangle<double>{50.0, 40.0, 40.0, 20.0}, xoj::view::HIDE_EDITOR_ONLY);

    // The transformed horizontal stroke is now vertical. Its old position and
    // the target rectangle's corner remain clear: no selection tint, border or
    // handles were rendered along with the owned element.
    EXPECT_GT(alphaAt(surface.get(), 60, 46), 0U);
    EXPECT_EQ(alphaAt(surface.get(), 54, 50), 0U);
    EXPECT_EQ(alphaAt(surface.get(), 15, 30), 0U);
    EXPECT_EQ(alphaAt(surface.get(), 50, 40), 0U);
    EXPECT_EQ(alphaAt(surface.get(), 55, 65), 0U);
}

TEST(ElementContainerViewTest, EditorOnlyElementsFollowTheContextTreatment) {
    TestElementContainer container;
    auto stroke = std::make_unique<Stroke>();
    stroke->setColor(Colors::black);
    stroke->setWidth(4.0);
    stroke->addPoint(Point(10.0, 20.0));
    stroke->addPoint(Point(30.0, 20.0));
    stroke->setEditorOnly(true);
    container.add(std::move(stroke));

    const xoj::util::Rectangle<double> bounds{0.0, 0.0, 40.0, 40.0};

    // Hidden for output: nothing lands on the surface at all.
    {
        xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 40, 40), xoj::util::adopt);
        xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
        xoj::view::ElementContainerView(&container).drawTransformed(cr.get(), bounds, bounds,
                                                                    xoj::view::HIDE_EDITOR_ONLY);
        EXPECT_EQ(alphaAt(surface.get(), 20, 20), 0U);
    }

    // Shown for editing: present, but faded well below full opacity.
    {
        xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 40, 40), xoj::util::adopt);
        xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
        xoj::view::ElementContainerView(&container).drawTransformed(cr.get(), bounds, bounds,
                                                                    xoj::view::SHOW_EDITOR_ONLY);
        const auto alpha = alphaAt(surface.get(), 20, 20);
        EXPECT_GT(alpha, 0U);
        EXPECT_LT(alpha, 200U);
    }
}

}  // namespace
