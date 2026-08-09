/*
 * Xournal++
 *
 * The record button: a toggle that says, while it is on, that it is on and for how long.
 *
 * A plain toggle button is a poor fit for recording. The pressed-in look is subtle enough to miss
 * across a wide toolbar, and there is nothing anywhere in the window saying how far into a take
 * you are -- so the two mistakes a recording invites are exactly the two this button is meant to
 * prevent: talking for ten minutes to a recorder that was never started, and leaving one running
 * long after the lecture ended.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

#include <gtk/gtk.h>

#include "enums/Action.enum.h"

#include "ToolButton.h"

class Control;

class RecordButton final: public ToolButton {
public:
    RecordButton(std::string id, Category cat, Action action, std::string iconName, std::string description,
                 Control* control);
    ~RecordButton() override = default;

protected:
    xoj::util::WidgetSPtr createItem(bool horizontal) override;

private:
    Control* control;
};
