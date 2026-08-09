#include "RecordButton.h"

#include <utility>  // for move

#include "control/Control.h"  // for Control
#include "util/gtk4_helper.h"  // for gtk_button_set_child, gtk_widget_add_css_class

namespace {

/// CSS class the stylesheet turns red. See ui/xournalpp.css.
constexpr const char* RECORDING_CLASS = "xopp-recording";

/**
 * Everything one instance of the button needs while it is counting. Toolbars are rebuilt whenever
 * they are reconfigured, and the same action can appear on more than one of them, so this hangs off
 * the widget rather than off the RecordButton that created it.
 */
struct ElapsedTimeCounter {
    Control* control = nullptr;
    GtkWidget* button = nullptr;
    GtkWidget* label = nullptr;
    guint source = 0;
};

/// "7:24", or "1:07:24" once a recording has run past an hour.
auto formatElapsed(gint64 microseconds) -> std::string {
    const long long seconds = microseconds > 0 ? microseconds / G_USEC_PER_SEC : 0;
    const long long hours = seconds / 3600;
    const long long minutes = (seconds / 60) % 60;

    char buffer[32];
    if (hours > 0) {
        g_snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld", hours, minutes, seconds % 60);
    } else {
        g_snprintf(buffer, sizeof(buffer), "%lld:%02lld", minutes, seconds % 60);
    }
    return buffer;
}

/**
 * Redraw the counter from the recording's own start time rather than from a tick count, so a
 * missed timeout -- a busy main loop, a laptop that was asleep -- shows up as a skipped second
 * instead of accumulating into a wrong duration.
 */
auto updateCounter(gpointer data) -> gboolean {
    auto* counter = static_cast<ElapsedTimeCounter*>(data);
    const gint64 started = counter->control->getRecordingStartTime();
    gtk_label_set_text(GTK_LABEL(counter->label), formatElapsed(started > 0 ? g_get_monotonic_time() - started : 0).c_str());
    return G_SOURCE_CONTINUE;
}

void stopCounting(ElapsedTimeCounter* counter) {
    if (counter->source != 0) {
        g_source_remove(counter->source);
        counter->source = 0;
    }
}

/// Bring the button in line with whether a recording is running: red and counting, or ordinary.
void applyRecordingState(ElapsedTimeCounter* counter, bool recording) {
    if (recording) {
        gtk_widget_add_css_class(counter->button, RECORDING_CLASS);
        gtk_widget_show(counter->label);
        updateCounter(counter);
        if (counter->source == 0) {
            // Half a second, not a whole one: on a one-second timer the displayed value can sit a
            // full second behind the truth, and a counter that visibly lags is worse than none.
            counter->source = g_timeout_add(500, updateCounter, counter);
        }
    } else {
        stopCounting(counter);
        gtk_widget_remove_css_class(counter->button, RECORDING_CLASS);
        gtk_widget_hide(counter->label);
        gtk_label_set_text(GTK_LABEL(counter->label), "");
    }
}

}  // namespace

RecordButton::RecordButton(std::string id, Category cat, Action action, std::string iconName, std::string description,
                           Control* control):
        ToolButton(std::move(id), cat, action, std::move(iconName), std::move(description), true), control(control) {}

auto RecordButton::createItem(bool horizontal) -> xoj::util::WidgetSPtr {
    // The plain toggle button, its action binding and its overflow-menu proxy, all built by
    // ToolButton; only the child and the styling are ours.
    xoj::util::WidgetSPtr item = ToolButton::createItem(horizontal);

    GtkWidget* button = gtk_bin_get_child(GTK_BIN(item.get()));
    if (button == nullptr || !GTK_IS_TOGGLE_BUTTON(button)) {
        return item;
    }

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(box), getNewToolIcon());

    GtkWidget* label = gtk_label_new("");
    // The counter is the same width in every digit position, so the button does not shuffle its
    // neighbours along the toolbar once a second.
    PangoAttrList* attributes = pango_attr_list_new();
    pango_attr_list_insert(attributes, pango_attr_family_new("monospace"));
    gtk_label_set_attributes(GTK_LABEL(label), attributes);
    pango_attr_list_unref(attributes);
    gtk_box_append(GTK_BOX(box), label);

    gtk_button_set_child(GTK_BUTTON(button), box);
    gtk_widget_show_all(box);
    // Whether the counter is visible is ours to decide, and a gtk_widget_show_all on the toolbar
    // that contains it -- which happens whenever a toolbar is built -- would otherwise override it.
    gtk_widget_set_no_show_all(label, TRUE);
    gtk_widget_hide(label);

    auto* counter = new ElapsedTimeCounter{this->control, button, label, 0};
    g_object_set_data_full(G_OBJECT(button), "xopp-record-counter", counter, +[](gpointer data) {
        auto* counter = static_cast<ElapsedTimeCounter*>(data);
        stopCounting(counter);
        delete counter;
    });

    // The action's own state is what says whether a recording is running -- it is what the record
    // action sets when a start succeeds, and what the stop button and a dying ffmpeg both put back
    // down -- so following the button's "active" is following the recording itself.
    g_signal_connect(button, "notify::active", G_CALLBACK(+[](GObject* button, GParamSpec*, gpointer data) {
                         applyRecordingState(static_cast<ElapsedTimeCounter*>(data),
                                             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
                     }),
                     counter);

    // A toolbar reconfigured mid-recording builds a fresh button, which has to come up already red
    // and already counting from where the recording actually started.
    applyRecordingState(counter, this->control->isRecording());

    return item;
}
