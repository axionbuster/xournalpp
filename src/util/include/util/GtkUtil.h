/*
 * Xournal++
 * Helper function for setting up some GtkWidget
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>

#include <gtk/gtk.h>

namespace xoj::util::gtk {
/**
 * @brief if `btn` has the GtkActionable properties action-name and action-target set, and a corresponding action has
 * been added to any ascendent's GActionMap, then make the ToggleButton to never be unset when clicking on it. Only
 * changing the action state can unset the button (e.g. by clicking on another button with the same action but a
 * different state).
 */
void setToggleButtonUnreleasable(GtkToggleButton* btn);

/**
 * @brief Make so a widget is automatically enabled/disabled whenever the given action is
 */
void setWidgetFollowActionEnabled(GtkWidget* w, GAction* a);

/**
 * @brief returns the user-space DPI (i.e. scaled by HiDPI scaling) of the monitor the widget is on.
 * Return std::nullopt if the widget is not realized
 */
std::optional<double> getWidgetDPI(GtkWidget* w);

/**
 * @brief Let a popup share a full-screen window's space without becoming full screen itself.
 *
 * macOS gives a window opened over a full-screen one the option of taking the whole space, and a
 * plain resizable GtkWindow qualifies. A dialog that does so is full screen in its own right, so
 * closing it makes AppKit run the exit-full-screen transition -- and that transition re-frames a
 * window GTK has already begun destroying, which segfaults inside GDK's quartz backend. The
 * application does not die outright, because the crash handler catches the signal; it stays on
 * screen as a black rectangle that answers no input. That is what "the app went black after I
 * clicked Cancel in preferences" was.
 *
 * Marking the window as an auxiliary occupant of the space keeps it a floating dialog over the
 * page, which is what a preferences window should look like anyway, and leaves no transition to
 * run when it closes. Call it before the window is shown.
 *
 * A no-op everywhere but macOS.
 */
void setFullScreenAuxiliary(GtkWindow* w);

#if GTK_MAJOR_VERSION == 3
/**
 * @brief RadioButton's and GAction don't work as expected in GTK3:
 * * Without setting the group, the RadioButtons are simply always ticked (all of them)
 * * With the group properly set, when a RadioButton is "un-selected" (because we selected another one), it still
 *   changes the GAction's state (it should only do that when being selected). This leads to infinite callback loops
 *
 * To circumvent this, we make our own GAction/RadioButton interactions. Don't forget to group the buttons together.
 */
void setRadioButtonActionName(GtkRadioButton* btn, const char* actionNamespace, const char* actionName);

/**
 * @brief some Gtk3 GtkActionable implementations (e.g. GtkMEnuItem) do not set their sensitivity properly on startup
 * Fixes that
 */
void fixActionableInitialSensitivity(GtkActionable* w);
#endif
};  // namespace xoj::util::gtk
