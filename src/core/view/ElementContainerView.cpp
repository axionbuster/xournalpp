#include "ElementContainerView.h"

#include <memory>  // for unique_ptr

#include <cairo.h>

#include "model/ElementContainer.h"  // for ElementContainer
#include "util/raii/CairoWrappers.h"

#include "View.h"  // for ElementView

class Element;

using namespace xoj::view;

ElementContainerView::ElementContainerView(const ElementContainer* container): container(container) {}

void ElementContainerView::draw(const Context& ctx) const {
    container->forEachElement([&ctx](const Element* e) {
        auto elementView = ElementView::createFromElement(e);
        elementView->draw(ctx);
    });
}

void ElementContainerView::drawTransformed(cairo_t* cr, const xoj::util::Rectangle<double>& sourceBounds,
                                           const xoj::util::Rectangle<double>& targetBounds) const {
    if (cr == nullptr || sourceBounds.width == 0.0 || sourceBounds.height == 0.0 || targetBounds.width == 0.0 ||
        targetBounds.height == 0.0) {
        return;
    }

    xoj::util::CairoSaveGuard saveGuard(cr);
    cairo_translate(cr, targetBounds.x, targetBounds.y);
    cairo_scale(cr, targetBounds.width / sourceBounds.width, targetBounds.height / sourceBounds.height);
    cairo_translate(cr, -sourceBounds.x, -sourceBounds.y);
    draw(Context::createDefault(cr));
}
