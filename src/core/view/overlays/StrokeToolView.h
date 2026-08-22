/*
 * Xournal++
 *
 * View active stroke tool - abstract base class
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <cairo.h>

#include "util/DispatchPool.h"
#include "util/Range.h"
#include "view/Mask.h"

#include "BaseStrokeToolView.h"

class StrokeHandler;
class Point;
class Range;
class Stroke;
class OverlayBase;

namespace xoj::view {
class Repaintable;

class StrokeToolView: public BaseStrokeToolView, public xoj::util::Listener<StrokeToolView> {
public:
    StrokeToolView(const StrokeHandler* strokeHandler, const Stroke& stroke, Repaintable* parent);
    virtual ~StrokeToolView() noexcept;

    bool isViewOf(const OverlayBase* overlay) const override;

    void draw(cairo_t* cr) const override;
    void drawForFrame(cairo_t* cr) const override;

    /**
     * Listener interface
     */
    static constexpr struct AddPointRequest {
    } ADD_POINT_REQUEST = {};
    virtual void on(AddPointRequest, const Point& p);

    static constexpr struct ThickenFirstPointRequest {
    } THICKEN_FIRST_POINT_REQUEST = {};
    void on(ThickenFirstPointRequest, double newPressure);

    static constexpr struct StrokeReplacementRequest {
    } STROKE_REPLACEMENT_REQUEST = {};
    virtual void on(StrokeReplacementRequest, const Stroke& newStroke);

    static constexpr struct CancellationRequest {
    } CANCELLATION_REQUEST = {};
    void deleteOn(CancellationRequest, const Range& rg);

    /**
     * @brief Called before the corresponding StrokeHandler's destruction
     */
    static constexpr struct FinalizationRequest {
    } FINALIZATION_REQUEST = {};
    void deleteOn(FinalizationRequest, const Range& rg);

protected:
    /**
     * Constructor with an injected listener pool for specialized renderers. The pool must outlive this view.
     */
    StrokeToolView(const StrokeHandler* strokeHandler, const Stroke& stroke, Repaintable* parent,
                   const std::shared_ptr<xoj::util::DispatchPool<StrokeToolView>>& viewPool);

    /**
     * @brief Compute the bounding box of the given segment, taking stroke width into account.
     */
    auto getRepaintRange(const Point& lastPoint, const Point& addedPoint) const -> Range;

    void drawDot(cairo_t* cr, const Point& p) const;

    /** Render the controller-owned stroke without touching either the live or clean-frame incremental state. */
    void drawModelSnapshot(cairo_t* cr, const Stroke& stroke) const;

    struct FrameMaskState {
        Mask mask;
        Range extent;
        double zoom = 0.0;
        size_t pointCount = 0;
        uint64_t lastUsed = 0;
        bool containsSingleDot = false;
    };

    /** Update the matching output-geometry mask with points not yet present in it. */
    FrameMaskState* updateFrameMask(cairo_t* cr, const Stroke& stroke) const;

    void resetFrameMasks() const;

    /**
     * Draw a filled highlighter without touching the live mask/filling state. The temporary alpha group is bounded by
     * the destination's current clip, rather than by a potentially enormous custom page or stroke bounding box.
     */
    void drawFilledHighlighterForFrame(cairo_t* cr, const Stroke& stroke) const;

    /**
     * @brief (Thread-safe) Flush the communication buffer and returns its content.
     */
    std::vector<Point> flushBuffer() const;

    // Nothing in the base class
    virtual void drawFilling(cairo_t*, const std::vector<Point>&) const {}

protected:
    const StrokeHandler* strokeHandler;

    /// The controller-owned stroke is updated before requests are dispatched to this view. Clean output frames read
    /// it without advancing the interactive canvas's point buffer or mask.
    const Stroke* liveStroke;

protected:
    bool singleDot = true;

    /**
     * @brief offset for drawing dashes (if any)
     *      In effect, this stores the length of the path already drawn to the mask.
     */
    mutable double dashOffset = 0;

    /**
     * @brief Controller/View communication buffer
     *      Those are in the same thread. Add mutex protection if this changes
     */
    mutable std::vector<Point> pointBuffer;  // Todo: implement a lock-free fifo?

    /**
     * @brief Drawing mask.
     *
     * The stroke is drawn to the mask and the mask is then blitted wherever needed.
     * Upon calls to draw(), the buffer is flushed and the corresponding part of stroke is added to the mask.
     */
    mutable Mask mask;

    /**
     * Clean output consumers share a separate, target-independent incremental alpha mask. It may be painted at a
     * different scale without ever consuming the interactive canvas's pointBuffer or mask.
     */
    mutable std::vector<FrameMaskState> frameMasks;
    mutable uint64_t frameMaskUseCounter = 0;
};
};  // namespace xoj::view
