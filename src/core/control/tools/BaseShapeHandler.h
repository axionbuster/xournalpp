/*
 * Xournal++
 *
 * Handles input of the ruler
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>    // for shared_ptr
#include <optional>  // for optional
#include <utility>   // for pair
#include <vector>    // for vector

#include <gdk/gdk.h>  // for GdkEventKey

#include "model/ElementInsertionPosition.h"  // for InsertionPosition
#include "model/LineShape.h"                 // for LineShape, Anchor
#include "model/PageRef.h"                   // for PageRef
#include "model/Point.h"                     // for Point
#include "util/Range.h"                      // for Range

#include "InputHandler.h"            // for InputHandler
#include "SnapToGridInputHandler.h"  // for SnapToGridInputHandler

class Layer;
class PositionInputData;

namespace xoj::util {
template <class T>
class DispatchPool;
}

namespace xoj::view {
class OverlayView;
class Repaintable;
class ShapeToolView;
};

enum DIRSET_MODIFIERS { NONE = 0, SET = 1, SHIFT = 1 << 1, CONTROL = 1 << 2 };


class BaseShapeHandler: public InputHandler {
public:
    BaseShapeHandler(Control* control, const PageRef& page, bool flipShift = false, bool flipControl = false);

    ~BaseShapeHandler() override;

    void onSequenceCancelEvent() override;
    bool onMotionNotifyEvent(const PositionInputData& pos, double zoom) override;
    void onButtonReleaseEvent(const PositionInputData& pos, double zoom) override;
    void onButtonPressEvent(const PositionInputData& pos, double zoom) override;
    void onButtonDoublePressEvent(const PositionInputData& pos, double zoom) override;
    bool onKeyPressEvent(const KeyEvent& event) override;
    bool onKeyReleaseEvent(const KeyEvent& event) override;

    std::unique_ptr<xoj::view::OverlayView> createView(xoj::view::Repaintable* parent) const override;

    const std::shared_ptr<xoj::util::DispatchPool<xoj::view::ShapeToolView>>& getViewPool() const;

    /**
     * @brief Get the shape's points.
     */
    const std::vector<Point>& getShape() const;

    /**
     * @brief Re-edit an existing line shape stroke instead of drawing a new one.
     *
     * The handler takes ownership of `original`, which the caller has already removed from
     * `layer`, and seeds itself from the stroke's own metadata: the anchor opposite `grabbed`
     * stays put while the drag moves `grabbed`. The replacement stroke keeps the original's
     * style, including the width its arrow heads are scaled to.
     *
     * Call this before onButtonPressEvent(). If the drag is cancelled — or the handler is
     * dropped mid-drag — the original goes back into the layer unchanged and no undo action is
     * recorded; on release, one undo action swaps original and replacement.
     */
    void grabExistingStroke(Layer* layer, InsertionPosition original, xoj::lineshape::Anchor grabbed);

private:
    /**
     * @brief Create the shape (to be drawn and added as a stroke), depending on the last event in
     * @return Pair [vector, range] where vector contains the points drawing the shape and range is the smallest range
     * containing all those points. WARNING: Stroke thickness is not taken into account.
     */
    virtual std::pair<std::vector<Point>, Range> createShape(bool isAltDown, bool isShiftDown, bool isControlDown) = 0;

    /**
     * @brief Update the current shape with the latest event info.
     *      Also warns the listeners about the change, usually triggering a redraw during the next screen update.
     */
    void updateShape(bool isAltDown, bool isShiftDown, bool isControlDown);

    /**
     * @brief Cancel the current shape creation: clears all data and wipes any drawing made
     */
    void cancelStroke();

    /**
     * @brief Put a grabbed original stroke back into its layer, unchanged and without an undo
     *      entry. Does nothing when no stroke is being re-edited, or when the original has
     *      already been handed over to an undo action.
     */
    void restoreGrabbedStroke();

    bool onKeyEvent(const KeyEvent& event, bool pressed);

protected:
    /**
     * @brief The line shape metadata to record on the committed stroke, if this handler draws
     *      one of the line shapes. Computed by createShape(), so it reflects the last drag.
     */
    virtual std::optional<LineShape> getLineShapeMetadata() const { return std::nullopt; }

    /**
     * @brief The stroke width the shape is drawn with — the current tool's, except when
     *      re-editing an existing stroke, where the original's width is kept.
     */
    double getShapeThickness() const;

protected:
    /**
     * modifyModifiersByDrawDir
     * @brief 	Toggle shift and control modifiers depending on initial drawing direction.
     */
    void modifyModifiersByDrawDir(double width, double height, double zoom, bool changeCursor = true);

protected:
    std::vector<Point> shape;

    /**
     * @brief Bounding box of the stroke after its last update
     *      WARNING: The stroke width is not taken into account (i.e. this is the snapping box)
     */
    Range lastSnappingRange;

    DIRSET_MODIFIERS drawModifierFixed = NONE;
    bool flipShift =
            false;  // use to reverse Shift key modifier action. i.e.  for separate Rectangle and Square Tool buttons.
    bool flipControl = false;  // use to reverse Control key modifier action.
    bool modShift = false;
    bool modControl = false;
    SnapToGridInputHandler snappingHandler;

    Point currPoint;
    Point buttonDownPoint;  // used for tapSelect and filtering - never snapped to grid.
    Point startPoint;       // May be snapped to grid

    /**
     * @brief Set while re-editing an existing shape by its first anchor: startPoint then holds
     *      the fixed anchor B, and the dragged point plays the role of anchor A.
     */
    bool draggingFirstAnchor = false;

    /**
     * @brief Where the grabbed anchor sits relative to the press, in page units.
     *
     * A grab lands anywhere within the grab radius — often on the arrow head rather than on the
     * anchor itself. Motion events add this offset so the anchor moves with the cursor from
     * where it already is, instead of jumping under the cursor on the first movement. Zero when
     * drawing a new shape.
     */
    double grabOffsetX = 0;
    double grabOffsetY = 0;

    /**
     * @brief Has the pointer actually moved since the press?
     *
     * A grab released without any motion puts the original back untouched instead of committing
     * an identical replacement and an undo entry nobody asked for.
     */
    bool grabbedStrokeMoved = false;

    /// The stroke being re-edited, owned while the drag lasts, and the layer it came from
    Layer* grabbedLayer = nullptr;
    InsertionPosition grabbedOriginal;

    std::shared_ptr<xoj::util::DispatchPool<xoj::view::ShapeToolView>> viewPool;
};
