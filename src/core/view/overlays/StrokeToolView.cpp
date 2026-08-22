#include "StrokeToolView.h"

#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>

#include "control/tools/StrokeHandler.h"
#include "model/LineStyle.h"
#include "model/Stroke.h"
#include "util/Assert.h"
#include "util/Color.h"
#include "util/PairView.h"
#include "util/Range.h"
#include "util/raii/CairoWrappers.h"  // for CairoSaveGuard
#include "view/Repaintable.h"
#include "view/StrokeView.h"
#include "view/StrokeViewHelper.h"
#include "view/View.h"

using namespace xoj::view;

namespace {
constexpr double FRAME_ZOOM_EPSILON = 1.0e-9;
// Two A8 masks cover recorder + projector while bounding retained geometry storage to roughly 64 MiB total.
constexpr double MAX_FRAME_MASK_PIXELS = 32.0 * 1024.0 * 1024.0;
constexpr size_t MAX_FRAME_MASKS = 2;

bool rangeContains(const Range& outer, const Range& inner) {
    return outer.isValid() && inner.isValid() && outer.minX <= inner.minX && outer.minY <= inner.minY &&
           outer.maxX >= inner.maxX && outer.maxY >= inner.maxY;
}
}  // namespace

StrokeToolView::StrokeToolView(const StrokeHandler* strokeHandler, const Stroke& stroke, Repaintable* parent):
        StrokeToolView(strokeHandler, stroke, parent, strokeHandler->getViewPool()) {}

StrokeToolView::StrokeToolView(const StrokeHandler* strokeHandler, const Stroke& stroke, Repaintable* parent,
                               const std::shared_ptr<xoj::util::DispatchPool<StrokeToolView>>& viewPool):
        BaseStrokeToolView(parent, stroke),
        strokeHandler(strokeHandler),
        liveStroke(&stroke),
        pointBuffer(stroke.getPointVector()) {
    this->registerToPool(viewPool);
    parent->flagDirtyRegion(Range(stroke.getBoundingBox()));
}

StrokeToolView::~StrokeToolView() noexcept { this->unregisterFromPool(); }

bool StrokeToolView::isViewOf(const OverlayBase* overlay) const { return overlay == this->strokeHandler; }

void StrokeToolView::draw(cairo_t* cr) const {
    if (this->pointBuffer.empty()) {
        // The input sequence has probably been cancelled. This view should soon be deleted
        return;
    }

    if (!mask.isInitialized()) {
        // Initialize the mask on first call
        mask = createMask(cr);
        if (!mask.isInitialized()) {
            // A GTK damage callback proves that there is a destination to paint even when Layout's visible-range
            // bookkeeping is temporarily invalid. Render from the controller-owned stroke without consuming this
            // view's pending points; a later successful mask creation can still catch up exactly once.
            drawModelSnapshot(cr, *this->liveStroke);
            return;
        }
    }

    // Do not consume points until there is somewhere persistent to paint them. If mask creation temporarily fails,
    // the next successful draw must be able to catch up on the complete stroke.
    std::vector<Point> pts = this->flushBuffer();
    // pts.front() is the last point we painted on the mask during the last iteration (see flushBuffer()).

    xoj::util::CairoSaveGuard saveGuard(cr);
    cairo_set_operator(cr, this->cairoOp);

    this->drawFilling(cr, pts);  // Noop in the base class.

    Util::cairo_set_source_argb(cr, strokeColor);

    if (pts.size() > 1) {
        // Draw the new segments on the mask
        if (pts.front().z == Point::NO_PRESSURE) {
            StrokeViewHelper::drawNoPressure(this->mask.get(), pts, this->strokeWidth, this->lineStyle,
                                             this->dashOffset);
            if (this->lineStyle.hasDashes()) {
                // Keep the offset up to date, so we do not have to redraw the entire stroke every time.
                PairView segments(pts);
                this->dashOffset =
                        std::transform_reduce(segments.begin(), segments.end(), this->dashOffset, std::plus<>(),
                                              [](auto& seg) { return seg.front().lineLengthTo(seg.back()); });
            }
        } else {
            this->dashOffset =
                    StrokeViewHelper::drawWithPressure(this->mask.get(), pts, this->lineStyle, this->dashOffset);
        }
    } else if (this->singleDot) {
        this->drawDot(this->mask.get(), pts.back());
    }

    this->mask.blitTo(cr);
}

void StrokeToolView::drawForFrame(cairo_t* cr) const {
    if (cr == nullptr || this->liveStroke == nullptr || this->liveStroke->getPointCount() == 0) {
        return;
    }

    const Stroke& stroke = *this->liveStroke;
    const bool supportsIncrementalFrameMask = stroke.getFill() == -1 &&
                                              stroke.getStrokeCapStyle() == StrokeCapStyle::ROUND &&
                                              !stroke.getLineStyle().hasDashes();
    FrameMaskState* frameState = supportsIncrementalFrameMask ? updateFrameMask(cr, stroke) : nullptr;
    if (frameState == nullptr) {
        // Filled strokes can change their entire interior when a point is appended. Keep their clean-frame fallback
        // exact and state-neutral; the common unfilled pen/highlighter path above stays incremental at recording rate.
        drawModelSnapshot(cr, stroke);
        return;
    }

    xoj::util::CairoSaveGuard saveGuard(cr);
    cairo_set_operator(cr,
                       stroke.getToolType() == StrokeTool::HIGHLIGHTER ? CAIRO_OPERATOR_MULTIPLY : CAIRO_OPERATOR_OVER);
    Util::cairo_set_source_argb(cr, strokeColorWithAlpha(stroke));
    frameState->mask.blitTo(cr);
}

void StrokeToolView::drawModelSnapshot(cairo_t* cr, const Stroke& stroke) const {
    const auto& points = stroke.getPointVector();

    // Filled highlighters normally use a persistent alpha mask so their translucent fill and outline are composited
    // only once. A state-neutral fallback uses a temporary group bounded by the current clip instead of the
    // potentially enormous page or stroke bounds.
    if (stroke.getToolType() == StrokeTool::HIGHLIGHTER && stroke.getFill() != -1 && points.size() > 1) {
        drawFilledHighlighterForFrame(cr, stroke);
        return;
    }

    if (points.size() == 1) {
        xoj::util::CairoSaveGuard saveGuard(cr);
        cairo_new_path(cr);
        cairo_set_operator(
                cr, stroke.getToolType() == StrokeTool::HIGHLIGHTER ? CAIRO_OPERATOR_MULTIPLY : CAIRO_OPERATOR_OVER);
        Util::cairo_set_source_argb(cr, strokeColorWithAlpha(stroke));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        const Point& point = points.front();
        cairo_set_line_width(cr, point.z == Point::NO_PRESSURE ? stroke.getWidth() : point.z);
        cairo_move_to(cr, point.x, point.y);
        cairo_line_to(cr, point.x, point.y);
        cairo_stroke(cr);
        return;
    }

    // The input controller appends to the Stroke before dispatching the corresponding view request. Recorder,
    // projector, and GTK drawing are serialized on the UI thread, so the model is stable for this call.
    StrokeView(&stroke).draw(xoj::view::Context::createDefault(cr));
}

auto StrokeToolView::updateFrameMask(cairo_t* cr, const Stroke& stroke) const -> FrameMaskState* {
    cairo_matrix_t matrix;
    cairo_get_matrix(cr, &matrix);
    if (std::abs(matrix.xy) > FRAME_ZOOM_EPSILON || std::abs(matrix.yx) > FRAME_ZOOM_EPSILON || matrix.xx <= 0.0 ||
        matrix.yy <= 0.0 || std::abs(matrix.xx - matrix.yy) > FRAME_ZOOM_EPSILON * std::max(matrix.xx, matrix.yy)) {
        return nullptr;
    }
    double deviceScaleX = 1.0;
    double deviceScaleY = 1.0;
    cairo_surface_get_device_scale(cairo_get_target(cr), &deviceScaleX, &deviceScaleY);
    if (deviceScaleX <= 0.0 || deviceScaleY <= 0.0 ||
        std::abs(deviceScaleX - deviceScaleY) > FRAME_ZOOM_EPSILON * std::max(deviceScaleX, deviceScaleY)) {
        return nullptr;
    }
    const double zoom = matrix.xx * deviceScaleX;
    if (!std::isfinite(zoom) || zoom <= 0.0) {
        return nullptr;
    }

    double x1;
    double y1;
    double x2;
    double y2;
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
    if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2) || !std::isfinite(y2)) {
        return nullptr;
    }
    Range requestedExtent(x1, y1, x2, y2);
    if (!requestedExtent.isValid()) {
        return nullptr;
    }

    auto matching = std::find_if(this->frameMasks.begin(), this->frameMasks.end(), [zoom](const FrameMaskState& state) {
        return std::abs(state.zoom - zoom) <= FRAME_ZOOM_EPSILON * std::max(state.zoom, zoom);
    });
    size_t stateIndex = matching == this->frameMasks.end() ? this->frameMasks.size() :
                                                             static_cast<size_t>(matching - this->frameMasks.begin());
    const bool isNewState = stateIndex == this->frameMasks.size();
    const Range newExtent = isNewState || !this->frameMasks[stateIndex].extent.isValid() ?
                                    requestedExtent :
                                    this->frameMasks[stateIndex].extent.unite(requestedExtent);
    const bool needsLargerExtent = isNewState || !rangeContains(this->frameMasks[stateIndex].extent, requestedExtent);

    if (needsLargerExtent) {
        const double pixelWidth = std::ceil(newExtent.maxX * zoom) - std::floor(newExtent.minX * zoom);
        const double pixelHeight = std::ceil(newExtent.maxY * zoom) - std::floor(newExtent.minY * zoom);
        const double minPixelX = std::floor(newExtent.minX * zoom);
        const double minPixelY = std::floor(newExtent.minY * zoom);
        const double maxPixelX = std::ceil(newExtent.maxX * zoom);
        const double maxPixelY = std::ceil(newExtent.maxY * zoom);
        const double minInt = static_cast<double>(std::numeric_limits<int>::lowest());
        const double maxInt = static_cast<double>(std::numeric_limits<int>::max());
        if (!std::isfinite(pixelWidth) || !std::isfinite(pixelHeight) || !std::isfinite(minPixelX) ||
            !std::isfinite(minPixelY) || !std::isfinite(maxPixelX) || !std::isfinite(maxPixelY) || minPixelX < minInt ||
            minPixelY < minInt || maxPixelX > maxInt || maxPixelY > maxInt || pixelWidth <= 0.0 || pixelHeight <= 0.0 ||
            pixelWidth > static_cast<double>(std::numeric_limits<int>::max()) ||
            pixelHeight > static_cast<double>(std::numeric_limits<int>::max()) ||
            pixelWidth * pixelHeight > MAX_FRAME_MASK_PIXELS) {
            return nullptr;
        }
        if (isNewState && this->frameMasks.size() >= MAX_FRAME_MASKS) {
            auto lru = std::min_element(
                    this->frameMasks.begin(), this->frameMasks.end(),
                    [](const FrameMaskState& lhs, const FrameMaskState& rhs) { return lhs.lastUsed < rhs.lastUsed; });
            this->frameMasks.erase(lru);
        }
        if (isNewState) {
            stateIndex = this->frameMasks.size();
            this->frameMasks.emplace_back();
        }

        FrameMaskState& state = this->frameMasks[stateIndex];
        state.mask.reset();  // Keep peak retained mask storage within the aggregate pixel cap during replacement.
        state.mask = Mask(1, newExtent, zoom);
        if (!state.mask.isInitialized() || cairo_status(state.mask.get()) != CAIRO_STATUS_SUCCESS) {
            resetFrameMasks();
            return nullptr;
        }
        cairo_t* maskCr = state.mask.get();
        cairo_set_source_rgba(maskCr, 1.0, 1.0, 1.0, 1.0);
        cairo_set_operator(maskCr, CAIRO_OPERATOR_OVER);
        cairo_set_line_join(maskCr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_cap(maskCr, StrokeView::CAIRO_LINE_CAP[stroke.getStrokeCapStyle()]);
        state.extent = newExtent;
        state.zoom = zoom;
        state.pointCount = 0;
        state.containsSingleDot = false;
    }

    FrameMaskState& state = this->frameMasks[stateIndex];
    state.lastUsed = ++this->frameMaskUseCounter;

    const auto& points = stroke.getPointVector();
    if (state.pointCount > points.size()) {
        state.mask.wipe();
        state.pointCount = 0;
        state.containsSingleDot = false;
    }
    if (state.pointCount == points.size()) {
        return &state;
    }

    // A one-point preview is always a round dot. Once a real segment arrives, rebuild without that dot so non-round
    // cap styles remain pixel-identical to the settled StrokeView renderer.
    if (state.containsSingleDot && points.size() > 1) {
        state.mask.wipe();
        state.pointCount = 0;
        state.containsSingleDot = false;
    }

    if (points.size() == 1) {
        drawDot(state.mask.get(), points.front());
        state.pointCount = 1;
        state.containsSingleDot = true;
        return &state;
    }

    const auto firstNewPoint = state.pointCount == 0 ? points.begin() : std::next(points.begin(), state.pointCount - 1);
    std::vector<Point> newSegments(firstNewPoint, points.end());
    if (newSegments.size() > 1) {
        cairo_set_line_cap(state.mask.get(), StrokeView::CAIRO_LINE_CAP[stroke.getStrokeCapStyle()]);
        if (newSegments.front().z == Point::NO_PRESSURE) {
            StrokeViewHelper::drawNoPressure(state.mask.get(), newSegments, stroke.getWidth(), stroke.getLineStyle());
        } else {
            StrokeViewHelper::drawWithPressure(state.mask.get(), newSegments, stroke.getLineStyle());
        }
    }
    state.pointCount = points.size();
    return &state;
}

void StrokeToolView::resetFrameMasks() const {
    this->frameMasks.clear();
    this->frameMaskUseCounter = 0;
}

void StrokeToolView::drawFilledHighlighterForFrame(cairo_t* cr, const Stroke& stroke) const {
    xoj::util::CairoSaveGuard saveGuard(cr);

    cairo_push_group_with_content(cr, CAIRO_CONTENT_ALPHA);
    cairo_new_path(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, StrokeView::CAIRO_LINE_CAP[stroke.getStrokeCapStyle()]);

    StrokeViewHelper::pathToCairo(cr, stroke.getPointVector());
    cairo_fill(cr);
    StrokeViewHelper::drawNoPressure(cr, stroke.getPointVector(), stroke.getWidth(), stroke.getLineStyle());

    cairo_pattern_t* alphaMask = cairo_pop_group(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
    Util::cairo_set_source_rgbi(cr, stroke.getColor(), static_cast<double>(stroke.getFill()) / 255.0);
    cairo_mask(cr, alphaMask);
    cairo_pattern_destroy(alphaMask);
}

void StrokeToolView::on(StrokeToolView::AddPointRequest, const Point& p) {
    this->singleDot = false;
    xoj_assert(!this->pointBuffer.empty());  // front() is the last point we painted on the mask (see flushBuffer())
    Point lastPoint = this->pointBuffer.back();
    this->pointBuffer.emplace_back(p);
    this->parent->flagDirtyRegion(this->getRepaintRange(lastPoint, p));
}

void StrokeToolView::on(StrokeToolView::ThickenFirstPointRequest, double newWidth) {
    xoj_assert(newWidth > 0.0);
    xoj_assert(this->pointBuffer.size() == 1);
    Point& p = this->pointBuffer.back();
    xoj_assert(p.z <= newWidth);  // Thicken means thicken
    p.z = newWidth;
    resetFrameMasks();
    Range rg = Range(p.x, p.y);
    rg.addPadding(0.5 * newWidth);
    this->parent->flagDirtyRegion(rg);
}

void StrokeToolView::deleteOn(StrokeToolView::CancellationRequest, const Range& rg) {
    this->pointBuffer.clear();
    this->parent->drawAndDeleteToolView(this, rg);
}

void StrokeToolView::on(StrokeToolView::StrokeReplacementRequest, const Stroke& newStroke) {
    if (this->mask.isInitialized()) {
        // only wipe mask it actually exists (the view has already been drawn at least once)
        this->mask.wipe();
    }
    this->pointBuffer = newStroke.getPointVector();
    this->dashOffset = 0;
    this->strokeWidth = newStroke.getWidth();
    this->liveStroke = &newStroke;
    resetFrameMasks();
    xoj_assert(this->strokeColor == strokeColorWithAlpha(newStroke));
    xoj_assert(this->lineStyle == newStroke.getLineStyle());
    xoj_assert(this->cairoOp ==
               (newStroke.getToolType() == StrokeTool::HIGHLIGHTER ? CAIRO_OPERATOR_MULTIPLY : CAIRO_OPERATOR_OVER));
}

void StrokeToolView::deleteOn(StrokeToolView::FinalizationRequest, const Range& rg) {
    this->parent->drawAndDeleteToolView(this, rg);
}

auto StrokeToolView::getRepaintRange(const Point& lastPoint, const Point& addedPoint) const -> Range {
    const double width = lastPoint.z == Point::NO_PRESSURE ? this->strokeWidth : lastPoint.z;
    Range rg(lastPoint.x, lastPoint.y);
    rg.addPoint(addedPoint.x, addedPoint.y);
    rg.addPadding(0.5 * width);
    return rg;
}

void StrokeToolView::drawDot(cairo_t* cr, const Point& p) const {
    cairo_set_line_width(cr, p.z == Point::NO_PRESSURE ? this->strokeWidth : p.z);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, p.x, p.y);
    cairo_line_to(cr, p.x, p.y);
    cairo_stroke(cr);
}

std::vector<Point> StrokeToolView::flushBuffer() const {
    std::vector<Point> pts;
    std::swap(this->pointBuffer, pts);
    if (!pts.empty()) {
        // Keep the last point in the buffer - to be used in the next iteration
        this->pointBuffer.emplace_back(pts.back());
    }
    return pts;
}
