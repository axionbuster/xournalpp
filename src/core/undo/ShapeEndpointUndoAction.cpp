#include "ShapeEndpointUndoAction.h"

#include <utility>  // for move

#include "control/Control.h"  // for Control
#include "model/Document.h"   // for Document
#include "model/Element.h"    // for Element
#include "model/Layer.h"      // for Layer
#include "model/XojPage.h"    // for XojPage
#include "undo/UndoAction.h"  // for UndoAction
#include "util/i18n.h"        // for _

ShapeEndpointUndoAction::ShapeEndpointUndoAction(const PageRef& page, Layer* layer, ElementPtr original,
                                                 Element* replacement):
        UndoAction("ShapeEndpointUndoAction"),
        layer(layer),
        original(original.get()),
        originalOwned(std::move(original)),
        replacement(replacement) {
    this->page = page;
}

ShapeEndpointUndoAction::~ShapeEndpointUndoAction() = default;

auto ShapeEndpointUndoAction::undo(Control* control) -> bool {
    Document* doc = control->getDocument();
    doc->lock();
    auto [owned, pos] = this->layer->removeElement(this->replacement);
    this->replacementOwned = std::move(owned);
    this->layer->insertElement(std::move(this->originalOwned), pos);
    doc->unlock();

    this->page->fireElementChanged(this->replacement);
    this->page->fireElementChanged(this->original);

    this->undone = true;
    return true;
}

auto ShapeEndpointUndoAction::redo(Control* control) -> bool {
    Document* doc = control->getDocument();
    doc->lock();
    auto [owned, pos] = this->layer->removeElement(this->original);
    this->originalOwned = std::move(owned);
    this->layer->insertElement(std::move(this->replacementOwned), pos);
    doc->unlock();

    this->page->fireElementChanged(this->original);
    this->page->fireElementChanged(this->replacement);

    this->undone = false;
    return true;
}

auto ShapeEndpointUndoAction::getText() -> std::string { return _("Drag shape endpoint"); }
