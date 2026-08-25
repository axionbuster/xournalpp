/*
 * Xournal++
 *
 * Undo action for toggling the editor-only flag (Edit selection)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string
#include <vector>  // for vector

#include "model/PageRef.h"  // for PageRef

#include "UndoAction.h"  // for UndoAction

class Element;
class Layer;
class Control;

class EditorOnlyUndoAction: public UndoAction {
public:
    /// @param newState The editor-only state every added element was switched to
    EditorOnlyUndoAction(const PageRef& page, Layer* layer, bool newState);

    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override;

    void addElement(Element* e, bool oldState);

private:
    struct Entry {
        Element* e;
        bool oldState;
    };
    std::vector<Entry> data;
    Layer* layer;
    bool newState;
};
