#include "EditorOnlyUndoAction.h"

#include "control/Control.h"
#include "model/Document.h"
#include "model/Element.h"  // for Element
#include "model/XojPage.h"  // for XojPage
#include "util/Assert.h"    // for xoj_assert
#include "util/Range.h"     // for Range
#include "util/i18n.h"      // for _

EditorOnlyUndoAction::EditorOnlyUndoAction(const PageRef& page, Layer* layer, bool newState):
        UndoAction("EditorOnlyUndoAction"), newState(newState) {
    this->page = page;
    this->layer = layer;
}

void EditorOnlyUndoAction::addElement(Element* e, bool oldState) { this->data.push_back({e, oldState}); }

auto EditorOnlyUndoAction::undo(Control* control) -> bool {
    if (this->data.empty()) {
        return true;
    }

    Range range;
    Document* doc = control->getDocument();
    doc->lock();

    for (const Entry& entry: this->data) {
        entry.e->setEditorOnly(entry.oldState);
        range = range.unite(Range(entry.e->getBoundingBox()));
    }

    doc->unlock();

    xoj_assert(!range.empty());
    this->page->fireRangeChanged(range);

    return true;
}

auto EditorOnlyUndoAction::redo(Control* control) -> bool {
    if (this->data.empty()) {
        return true;
    }

    Range range;
    Document* doc = control->getDocument();
    doc->lock();

    for (const Entry& entry: this->data) {
        entry.e->setEditorOnly(this->newState);
        range = range.unite(Range(entry.e->getBoundingBox()));
    }

    doc->unlock();

    xoj_assert(!range.empty());
    this->page->fireRangeChanged(range);

    return true;
}

auto EditorOnlyUndoAction::getText() -> std::string {
    return this->newState ? _("Hide in output") : _("Show in output");
}
