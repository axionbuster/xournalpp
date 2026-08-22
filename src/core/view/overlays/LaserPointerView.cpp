#include "LaserPointerView.h"

#include "control/tools/LaserPointerHandler.h"
#include "control/tools/StrokeHandler.h"
#include "model/Stroke.h"
#include "util/Range.h"
#include "util/raii/CairoWrappers.h"
#include "view/Repaintable.h"
#include "view/StrokeView.h"
#include "view/View.h"

#include "StrokeToolView.h"

using namespace xoj::view;

LaserPointerView::LaserPointerView(const LaserPointerHandler* handler, Repaintable* parent):
        LaserPointerView(handler, parent, handler->getViewPool()) {}

LaserPointerView::LaserPointerView(const LaserPointerHandler* handler, Repaintable* parent,
                                   const std::shared_ptr<xoj::util::DispatchPool<LaserPointerView>>& viewPool):
        OverlayView(parent), handler(handler) {
    this->registerToPool(viewPool);
}

LaserPointerView::~LaserPointerView() noexcept { this->unregisterFromPool(); }

void LaserPointerView::draw(cairo_t* cr) const {
    if (this->mask.isInitialized()) {
        xoj::util::CairoSaveGuard saveGuard(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        this->mask.paintToWithAlpha(cr, this->alpha);
    } else {
        this->mask = createMask(cr);
        if (this->mask.isInitialized()) {
            for (const auto& stroke: this->finishedStrokes) {
                StrokeView(stroke.get()).draw(Context::createDefault(this->mask.get()));
            }
            xoj::util::CairoSaveGuard saveGuard(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
            this->mask.paintToWithAlpha(cr, this->alpha);
        } else {
            drawFinishedStrokes(cr);
        }
    }
    if (this->activeStrokeView) {
        this->activeStrokeView->draw(cr);
    }
}

void LaserPointerView::drawForFrame(cairo_t* cr) const {
    // Render retained models rather than borrowing the live, destination-specific laser mask. This also keeps a
    // stroke visible when a recording frame is the only consumer before FINISH_STROKE_REQUEST.
    drawFinishedStrokes(cr);
    if (this->activeStrokeView) {
        this->activeStrokeView->drawForFrame(cr);
    }
}

void LaserPointerView::drawFinishedStrokes(cairo_t* cr) const {
    if (this->finishedStrokes.empty() || this->alpha == 0) {
        return;
    }

    xoj::util::CairoSaveGuard saveGuard(cr);
    cairo_push_group_with_content(cr, CAIRO_CONTENT_COLOR_ALPHA);
    for (const auto& stroke: this->finishedStrokes) {
        StrokeView(stroke.get()).draw(Context::createDefault(cr));
    }
    cairo_pop_group_to_source(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_paint_with_alpha(cr, static_cast<double>(this->alpha) / 255.0);
}

bool LaserPointerView::isViewOf(const OverlayBase* overlay) const { return overlay == this->handler; }

void xoj::view::LaserPointerView::on(StartNewStrokeRequest, StrokeHandler* handler) {
    this->activeStrokeModel = handler->getStroke();
    this->activeStrokeView = std::make_unique<StrokeToolView>(handler, *handler->getStroke(), parent);
    if (!this->extents.empty()) {
        on(SET_ALPHA_REQUEST, 255);
    }
}

void xoj::view::LaserPointerView::on(SetAlphaRequest, uint8_t alpha) {
    this->alpha = alpha;
    this->parent->flagDirtyRegion(this->extents);
}

void xoj::view::LaserPointerView::on(FinishStrokeRequest, const Range& strokeBox) {
    xoj_assert(this->activeStrokeView);
    xoj_assert(this->activeStrokeModel);
    this->finishedStrokes.emplace_back(this->activeStrokeModel->cloneStroke());
    if (this->mask.isInitialized()) {
        this->activeStrokeView->draw(this->mask.get());
    }
    this->activeStrokeView.reset();
    this->activeStrokeModel = nullptr;
    this->extents = this->extents.unite(strokeBox);
}

void xoj::view::LaserPointerView::on(InputCancellationRequest, const Range& rg) {
    this->activeStrokeView.reset();
    this->activeStrokeModel = nullptr;
    this->parent->flagDirtyRegion(rg);
}

void xoj::view::LaserPointerView::deleteOn(FinalizationRequest) {
    this->parent->deleteOverlayView(this, this->extents);
}


auto xoj::view::LaserPointerView::createMask(cairo_t* tgtcr) const -> Mask {
    const double zoom = this->parent->getZoom();
    Range visibleRange = this->parent->getVisiblePart();

    if (!visibleRange.isValid()) {
        /*
         * The user might be drawing on a page that is not visible at all:
         * e.g. https://github.com/xournalpp/xournalpp/pull/4158#issuecomment-1385954494
         */
        return Mask();
    }

    Mask mask(cairo_get_target(tgtcr), visibleRange, zoom, CAIRO_CONTENT_COLOR_ALPHA);
    cairo_t* cr = mask.get();

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    return mask;
}
