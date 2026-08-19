/*
 * Xournal++
 *
 * Undo action for dragging one end of an already drawn line shape
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>  // for unique_ptr
#include <string>  // for string

#include "model/Element.h"  // for Element, ElementPtr
#include "model/PageRef.h"  // for PageRef

#include "UndoAction.h"  // for UndoAction

class Control;
class Layer;

/**
 * @brief Swaps a line shape stroke for the one drawn in its place when one of its ends was
 * dragged.
 *
 * Follows the same shape as RecognizerUndoAction: the action owns whichever of the two strokes
 * is currently not in the layer, and undo/redo trade them at the position the removed one had.
 */
class ShapeEndpointUndoAction: public UndoAction {
public:
    ShapeEndpointUndoAction(const PageRef& page, Layer* layer, ElementPtr original, Element* replacement);
    ~ShapeEndpointUndoAction() override;

public:
    bool undo(Control* control) override;
    bool redo(Control* control) override;

    std::string getText() override;

private:
    Layer* layer;
    Element* original;
    ElementPtr originalOwned;
    Element* replacement;
    ElementPtr replacementOwned;
};
