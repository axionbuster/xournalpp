// Table of contents
//   1. Construction ............. window, drawing area, signal wiring
//   2. Show / hide .............. and the geometry that survives both
//   3. Geometry memory .......... saveGeometry / restoreGeometry / aspect ratio hint
//   4. Painting ................. onDraw / drawPage
//   5. Change notifications ..... DocumentListener and the repaint hook

#include "ProjectorWindow.h"

#include <algorithm>  // for clamp, max, min
#include <string>     // for string
#include <utility>    // for move

#include "control/Control.h"                 // for Control
#include "control/actions/ActionDatabase.h"  // for ActionDatabase
#include "control/settings/Settings.h"       // for Settings
#include "gui/CanvasFrame.h"                 // for drawCurrentPage, FrameLayout
#include "model/XojPage.h"                   // for XojPage
#include "util/i18n.h"                       // for _

#ifdef GDK_WINDOWING_QUARTZ
#include <objc/message.h>   // for objc_msgSend
#include <objc/runtime.h>   // for sel_registerName

/**
 * Declared here rather than included. The real declaration lives in
 * <gdk/quartz/gdkquartz-cocoa-access.h>, which returns NSWindow* and therefore only compiles in
 * Objective-C. The pointer is opaque to us either way, and the ABI is identical.
 */
extern "C" void* gdk_quartz_window_get_nswindow(GdkWindow* window);
#endif

// ===========================================================================================
// 1. Construction
// ===========================================================================================

ProjectorWindow::ProjectorWindow(Control* control): control(control) {
    this->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(this->window), _("Projector"));
    // From the remembered size, not a constant, so the window is never briefly the wrong size on
    // the way to being the right one.
    gtk_window_set_default_size(GTK_WINDOW(this->window), std::max(160, control->getSettings()->getProjectorWidth()),
                                std::max(90, control->getSettings()->getProjectorHeight()));

    // Not transient for the main window on purpose. A transient window is tied to its parent's
    // stacking and workspace, which is exactly wrong for something meant to sit on a second
    // display in front of whatever else is there.
    gtk_window_set_destroy_with_parent(GTK_WINDOW(this->window), FALSE);

    this->drawingArea = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(this->window), this->drawingArea);
    gtk_widget_show(this->drawingArea);

    g_signal_connect(this->drawingArea, "draw", G_CALLBACK(&ProjectorWindow::onDraw), this);
    g_signal_connect(this->window, "delete-event", G_CALLBACK(&ProjectorWindow::onDeleteEvent), this);

    // Realized here, well before it is ever shown, purely so that the native window exists in time
    // to be told it is not allowed to become a tab. Realizing creates that window; it does not put
    // anything on screen.
    gtk_widget_realize(this->window);
    applyNativeWindowTabbing();

    // A background re-render landing is invisible until something repaints; the redraw clock
    // turns this flag into an actual frame.
    this->frameCache.setRefreshedCallback([this]() { queueRedraw(); });

    this->registerListener(control);
    applySettings();
}

void ProjectorWindow::onToolViewSettled(const PageRef& page, const xoj::view::ToolView* v) {
    if (this->visible) {
        this->frameCache.drawSettled(page, v);
    }
}

ProjectorWindow::~ProjectorWindow() {
    this->unregisterListener();
    stopRedrawClock();

    if (this->window != nullptr) {
        if (this->visible) {
            saveGeometry();
        }
        // Drop our handlers first: destroying the window emits "delete-event" on some backends,
        // and the callback would run against a half-destroyed ProjectorWindow.
        g_signal_handlers_disconnect_by_data(this->window, this);
        g_signal_handlers_disconnect_by_data(this->drawingArea, this);
        gtk_widget_destroy(this->window);
        this->window = nullptr;
        this->drawingArea = nullptr;
    }
}

void ProjectorWindow::applySettings() {
    Settings* settings = control->getSettings();
    const bool keepAbove = settings->isProjectorKeepAbove();

    gtk_window_set_keep_above(GTK_WINDOW(this->window), keepAbove ? TRUE : FALSE);
    applyNativeWindowLevel(keepAbove);

    applyAspectRatioHint();
    queueRedraw();
}

void ProjectorWindow::applyNativeWindowTabbing() {
#ifdef GDK_WINDOWING_QUARTZ
    // macOS merges a newly opened window into the frontmost window's tab bar rather than giving it
    // a window of its own, and the factory setting for when it does that is "in full screen" -- so
    // an application restored into presentation mode swallows the projector as a tab the moment it
    // opens. A tab is the one shape this window must never take: it exists to be a second, floating
    // view of the page, and as a tab it is neither second nor floating, it just hides the canvas.
    //
    // This has to be set before the window is first ordered in, which is why the window is realized
    // in the constructor: a window that has already joined a tab group does not leave it because
    // the mode changed afterwards.
    GdkWindow* gdkWindow = gtk_widget_get_window(this->window);
    if (gdkWindow == nullptr) {
        return;
    }

    void* nsWindow = gdk_quartz_window_get_nswindow(gdkWindow);
    if (nsWindow == nullptr) {
        return;
    }

    constexpr long NS_WINDOW_TABBING_MODE_DISALLOWED = 2;

    using SetTabbingModeFn = void (*)(void*, SEL, long);
    reinterpret_cast<SetTabbingModeFn>(objc_msgSend)(nsWindow, sel_registerName("setTabbingMode:"),
                                                     NS_WINDOW_TABBING_MODE_DISALLOWED);
#endif
}

void ProjectorWindow::applyNativeWindowLevel(bool keepAbove) {
#ifdef GDK_WINDOWING_QUARTZ
    // gtk_window_set_keep_above alone is not enough here. It holds on the first showing, but a
    // projector that has been closed and reopened comes back at the ordinary window level: GtkWindow
    // caches the flag and skips the backend call, so the freshly ordered-in native window never
    // learns about it. The result is a projector that looks right for as long as it keeps focus and
    // then silently sinks behind the main window -- during a lecture, which is the worst time to
    // find out. Setting the level on the native window directly is unconditional and settles it.
    //
    // Reached through the Objective-C runtime rather than an Objective-C++ file, to keep this
    // translation unit plain C++ for one message send.
    GdkWindow* gdkWindow = gtk_widget_get_window(this->window);
    if (gdkWindow == nullptr) {
        return;  // not realized yet; show() calls this again once it is
    }

    void* nsWindow = gdk_quartz_window_get_nswindow(gdkWindow);
    if (nsWindow == nullptr) {
        return;
    }

    // NSFloatingWindowLevel and NSNormalWindowLevel, which are plain integers rather than symbols
    // we could link against from here.
    constexpr long NS_FLOATING_WINDOW_LEVEL = 3;
    constexpr long NS_NORMAL_WINDOW_LEVEL = 0;

    using SetLevelFn = void (*)(void*, SEL, long);
    reinterpret_cast<SetLevelFn>(objc_msgSend)(nsWindow, sel_registerName("setLevel:"),
                                               keepAbove ? NS_FLOATING_WINDOW_LEVEL : NS_NORMAL_WINDOW_LEVEL);
#else
    (void)keepAbove;
#endif
}

// ===========================================================================================
// 2. Show / hide
// ===========================================================================================

void ProjectorWindow::show() {
    if (this->visible) {
        gtk_window_present(GTK_WINDOW(this->window));
        return;
    }

    gtk_widget_show_all(this->window);
    this->visible = true;
    startRedrawClock();

    // After the window is on screen: gtk_window_move on an unrealized window is advisory at best,
    // and the size has to be known before the position can be clamped onto the monitor.
    restoreGeometry();

    // Also after mapping, and this ordering is load-bearing. On the quartz backend keep-above is an
    // NSWindow level, and a level set while the window is unmapped does not survive the next map --
    // so a projector that had been closed and reopened would silently stop floating, which is
    // exactly the state it is in during a lecture.
    applySettings();

    gtk_window_present(GTK_WINDOW(this->window));
}

void ProjectorWindow::hide() {
    if (!this->visible) {
        return;
    }

    saveGeometry();
    stopRedrawClock();
    gtk_widget_hide(this->window);
    this->visible = false;

    // A closed projector should cost nothing at all, and its picture of the page is several
    // megabytes that will be out of date by the time it is opened again.
    this->frameCache.invalidate();
}

auto ProjectorWindow::isVisible() const -> bool { return this->visible; }

auto ProjectorWindow::onDeleteEvent(GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
    auto* self = static_cast<ProjectorWindow*>(data);

    // Closing the projector is a view change and nothing else. Any recording in progress is run by
    // a separate process capturing the screen and never learns this happened.
    self->hide();
    self->control->getActionDatabase()->setActionState(Action::PROJECTOR_WINDOW, false);

    return TRUE;  // handled: do not let GTK destroy the window
}

// ===========================================================================================
// 3. Geometry memory
// ===========================================================================================

void ProjectorWindow::saveGeometry() {
    if (this->window == nullptr || !gtk_widget_get_realized(this->window)) {
        return;
    }

    GdkWindow* gdkWindow = gtk_widget_get_window(this->window);
    if (gdkWindow == nullptr) {
        return;
    }

    // A maximized or full-screen window is the size of the screen, and that is not a size anything
    // should reopen at -- restoring it would mean the window could never be small again, since
    // every close would write the screen size back. Keeping the last ordinary geometry is what
    // "where it was" is supposed to mean.
    const GdkWindowState state = gdk_window_get_state(gdkWindow);
    if ((state & (GDK_WINDOW_STATE_FULLSCREEN | GDK_WINDOW_STATE_MAXIMIZED)) != 0) {
        return;
    }

    GdkDisplay* display = gtk_widget_get_display(this->window);
    GdkMonitor* monitor = gdk_display_get_monitor_at_window(display, gdkWindow);
    if (monitor == nullptr) {
        return;
    }

    // The work area, not the full monitor rectangle, and restoreGeometry has to use the same one:
    // they differ by the menu bar on macOS and by panels elsewhere, so measuring against one and
    // restoring against the other moves the window by that difference on every reopen.
    GdkRectangle workarea{};
    gdk_monitor_get_workarea(monitor, &workarea);

    gint x = 0;
    gint y = 0;
    gint width = 0;
    gint height = 0;
    gtk_window_get_position(GTK_WINDOW(this->window), &x, &y);
    gtk_window_get_size(GTK_WINDOW(this->window), &width, &height);

    // Stored relative to the work area's origin, and with the monitor identified by description
    // rather than index, for the same reason the main window is: indices are reassigned whenever a
    // display is plugged in, so an index restores onto the wrong panel exactly when it matters.
    control->getSettings()->setProjectorGeometry(x - workarea.x, y - workarea.y, width, height,
                                                 Settings::describeMonitor(monitor));
}

void ProjectorWindow::restoreGeometry() {
    Settings* settings = control->getSettings();

    const int width = settings->getProjectorWidth();
    const int height = settings->getProjectorHeight();
    if (width > 0 && height > 0) {
        gtk_window_resize(GTK_WINDOW(this->window), width, height);
    }

    const std::string& wanted = settings->getProjectorMonitor();
    if (wanted.empty()) {
        return;
    }

    GdkDisplay* display = gtk_widget_get_display(this->window);
    GdkMonitor* match = nullptr;
    const int monitorCount = gdk_display_get_n_monitors(display);
    for (int i = 0; i < monitorCount; i++) {
        GdkMonitor* candidate = gdk_display_get_monitor(display, i);
        if (Settings::describeMonitor(candidate) == wanted) {
            match = candidate;
            break;
        }
    }

    if (match == nullptr) {
        g_message("Projector: monitor \"%s\" is not connected, using default placement", wanted.c_str());
        return;
    }

    GdkRectangle workarea{};
    gdk_monitor_get_workarea(match, &workarea);

    gint currentWidth = 0;
    gint currentHeight = 0;
    gtk_window_get_size(GTK_WINDOW(this->window), &currentWidth, &currentHeight);

    // Clamp onto the monitor, so a projector remembered from a larger display still comes back
    // fully on screen and can be reached by its title bar.
    const int maxX = workarea.x + std::max(0, workarea.width - currentWidth);
    const int maxY = workarea.y + std::max(0, workarea.height - currentHeight);
    const int x = std::clamp(workarea.x + settings->getProjectorPosX(), workarea.x, maxX);
    const int y = std::clamp(workarea.y + settings->getProjectorPosY(), workarea.y, maxY);

    moveTo(x, y);
}

void ProjectorWindow::moveTo(int x, int y) {
    // gtk_window_get_position is documented as returning exactly what gtk_window_move needs to be
    // given to leave a window where it is. The quartz backend does not honour that -- it moves the
    // content area and reports the frame -- so a projector restored on macOS lands one title bar
    // away from where it was closed, every time, in the same direction. Rather than hard-code a
    // decoration offset for one backend, ask, look at where the window actually went, and correct
    // by the difference. On a backend that got it right the difference is zero and this does
    // nothing at all.
    int askedX = x;
    int askedY = y;
    gtk_window_move(GTK_WINDOW(this->window), askedX, askedY);

    for (int attempt = 0; attempt < 2; attempt++) {
        gint actualX = 0;
        gint actualY = 0;
        gtk_window_get_position(GTK_WINDOW(this->window), &actualX, &actualY);
        if (actualX == x && actualY == y) {
            return;
        }
        askedX += x - actualX;
        askedY += y - actualY;
        gtk_window_move(GTK_WINDOW(this->window), askedX, askedY);
    }
}

void ProjectorWindow::applyAspectRatioHint() {
    Settings* settings = control->getSettings();

    GdkGeometry geometry{};
    if (settings->isProjectorLockAspectRatio()) {
        const int width = std::max(1, settings->getVideoRecordingWidth());
        const int height = std::max(1, settings->getVideoRecordingHeight());
        geometry.min_aspect = static_cast<gdouble>(width) / height;
        geometry.max_aspect = geometry.min_aspect;
        gtk_window_set_geometry_hints(GTK_WINDOW(this->window), nullptr, &geometry, GDK_HINT_ASPECT);
    } else {
        gtk_window_set_geometry_hints(GTK_WINDOW(this->window), nullptr, &geometry, static_cast<GdkWindowHints>(0));
    }
}

// ===========================================================================================
// 4. Painting
// ===========================================================================================

auto ProjectorWindow::onDraw(GtkWidget* widget, cairo_t* cr, gpointer data) -> gboolean {
    auto* self = static_cast<ProjectorWindow*>(data);

    GtkAllocation allocation{};
    gtk_widget_get_allocation(widget, &allocation);
    self->drawPage(cr, allocation.width, allocation.height);

    return TRUE;
}

void ProjectorWindow::drawPage(cairo_t* cr, int width, int height) {
    Settings* settings = control->getSettings();

    const auto layout = xoj::canvas::drawCurrentPage(this->control, cr, width, height,
                                                     settings->getProjectorBackgroundColor(), &this->frameCache);
    // Kept so that a repaint notification can be answered without touching the document: that
    // notification arrives once per motion event, sometimes with the document lock already held.
    this->shownPage = layout.page;

    drawSafeArea(cr, layout.x, layout.y, layout.width, layout.height);
    drawFrameRate(cr, width, height);
}

void ProjectorWindow::drawSafeArea(cairo_t* cr, double x, double y, double areaWidth, double areaHeight) {
    Settings* settings = control->getSettings();
    if (!settings->isProjectorShowSafeArea() || areaWidth <= 0.0 || areaHeight <= 0.0) {
        return;
    }

    // The setting is given in lines of the finished video, because that is how a subtitling
    // requirement is written down ("keep the bottom 150 px clear"). Turning it into a fraction of
    // the frame is what makes it mean the same thing in a projector window of any size.
    const int frameHeight = std::max(1, settings->getVideoRecordingHeight());
    const double fraction = static_cast<double>(settings->getProjectorSafeAreaHeight()) / frameHeight;
    const double bandHeight = std::min(areaHeight, areaHeight * fraction);
    if (bandHeight <= 0.0) {
        return;
    }

    const double top = y + areaHeight - bandHeight;

    // Drawn only here, never into the recording: this window renders the page a second time for
    // the screen, and the encoder is fed its own frames, which nothing in this file touches.
    // Translucent so the guide shows what is underneath it rather than hiding the very writing it
    // is there to warn about. A plain fill and nothing else -- an edge line invites the question
    // of whether the line's own rows are covered, and a fill answers it: everything tinted is
    // covered, and the first clear row is the first safe one.
    cairo_set_source_rgba(cr, 0.65, 0.13, 0.11, 0.45);
    cairo_rectangle(cr, x, top, areaWidth, bandHeight);
    cairo_fill(cr);
}

namespace {
/// How often the projector may repaint, in milliseconds. 30 frames a second, at most.
constexpr guint REDRAW_INTERVAL = 33;

/// How long the projector may go without repainting, whatever it has been told. Microseconds.
constexpr gint64 MAX_REDRAW_GAP = 250 * 1000;

/// How often the frame rate indicator's text is worked out again. Microseconds.
constexpr gint64 FRAME_RATE_TEXT_INTERVAL = 250 * 1000;
}  // namespace

auto ProjectorWindow::buildFrameRateText() const -> std::string {
    // While a video is being recorded the rate that matters is the recording's, not this window's:
    // it is the one that decides whether the file stutters. The tag says which is being shown, so
    // that 30 during a 60 fps recording can never be mistaken for the recording faltering.
    const bool recording = control->getVideoTargetFrameRate() > 0;
    const double rate = recording ? control->getVideoFrameRate() : this->previewMeter.rate();
    if (rate <= 0.0) {
        // Nothing measured yet -- half a second after opening the window, or after starting a
        // recording. Saying so as "0.0 fps" would be a false alarm, so the last reading stands.
        return this->frameRateText;
    }

    char buffer[32];
    g_snprintf(buffer, sizeof(buffer), recording ? "REC %.1f fps" : "%.1f fps", rate);
    return buffer;
}

void ProjectorWindow::drawFrameRate(cairo_t* cr, int width, int height) {
    if (this->frameRateText.empty()) {
        return;
    }

    // Sized against the window rather than fixed, so it stays readable on a projector filling a
    // second display and stays out of the way in a small one parked in a corner.
    const double fontSize = std::clamp(height / 34.0, 11.0, 24.0);
    const double margin = std::max(6.0, fontSize * 0.5);

    cairo_save(cr);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, fontSize);

    cairo_text_extents_t extents{};
    cairo_text_extents(cr, this->frameRateText.c_str(), &extents);

    const double padX = fontSize * 0.5;
    const double padY = fontSize * 0.3;
    const double boxWidth = extents.width + 2 * padX;
    const double boxHeight = fontSize + 2 * padY;
    const double boxX = width - margin - boxWidth;
    const double boxY = margin;

    // A plate behind the text, because the page underneath is usually white and white-on-white is
    // no indicator at all.
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
    cairo_rectangle(cr, boxX, boxY, boxWidth, boxHeight);
    cairo_fill(cr);

    // Amber below nine tenths of the rate being aimed for, red below six -- the same idea as OBS
    // colouring its dropped-frame count, so that a glance is enough and reading the number is not.
    const int target = control->getVideoTargetFrameRate();
    const double rate = target > 0 ? control->getVideoFrameRate() : this->previewMeter.rate();
    const double expected = target > 0 ? target : 1000.0 / REDRAW_INTERVAL;
    if (rate <= 0.0) {
        // No reading yet; the text on screen is the previous one, so colour it as it was.
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
    } else if (rate < expected * 0.6) {
        cairo_set_source_rgb(cr, 1.0, 0.35, 0.30);
    } else if (rate < expected * 0.9) {
        cairo_set_source_rgb(cr, 1.0, 0.72, 0.20);
    } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
    }

    cairo_move_to(cr, boxX + padX - extents.x_bearing, boxY + padY - extents.y_bearing);
    cairo_show_text(cr, this->frameRateText.c_str());
    cairo_restore(cr);
}

void ProjectorWindow::queueRedraw() { this->needsRedraw = true; }

void ProjectorWindow::startRedrawClock() {
    if (this->redrawTimer == 0) {
        // A rate measured before the window was last closed says nothing about now.
        this->previewMeter.reset();
        this->frameRateText.clear();
        this->frameRateTextAt = 0;
        this->needsRedraw = true;
        this->redrawTimer = g_timeout_add(REDRAW_INTERVAL, &ProjectorWindow::onRedrawTick, this);
    }
}

void ProjectorWindow::stopRedrawClock() {
    if (this->redrawTimer != 0) {
        g_source_remove(this->redrawTimer);
        this->redrawTimer = 0;
    }
}

auto ProjectorWindow::onRedrawTick(gpointer data) -> gboolean {
    auto* self = static_cast<ProjectorWindow*>(data);
    if (!self->visible || self->drawingArea == nullptr) {
        return G_SOURCE_CONTINUE;
    }

    const gint64 now = g_get_monotonic_time();
    self->previewMeter.tick();

    // The indicator is the one thing on screen that has to keep changing on a page nobody is
    // touching, so it asks for its own repaints -- but only when the reading it would show has
    // actually changed, which on a healthy machine is a few times a minute rather than thirty times
    // a second.
    if (self->control->getSettings()->isShowFrameRate()) {
        if (now - self->frameRateTextAt >= FRAME_RATE_TEXT_INTERVAL) {
            self->frameRateTextAt = now;
            std::string text = self->buildFrameRateText();
            if (text != self->frameRateText) {
                self->frameRateText = std::move(text);
                self->needsRedraw = true;
            }
        }
    } else if (!self->frameRateText.empty()) {
        self->frameRateText.clear();
        self->needsRedraw = true;
    }

    // The second condition is the safety net, and it is the reason this window can be trusted as a
    // preview: a change that reached the page by a route nobody reports -- and there are such
    // routes, an undo used to be one -- is on screen a quarter of a second later rather than
    // waiting for the next stroke to shake it loose. Drawing a page nobody has touched is cheap,
    // because the picture is kept between frames.
    if (self->needsRedraw || now - self->lastRedrawQueued >= MAX_REDRAW_GAP) {
        self->needsRedraw = false;
        self->lastRedrawQueued = now;
        gtk_widget_queue_draw(self->drawingArea);
    }

    return G_SOURCE_CONTINUE;
}

// ===========================================================================================
// 5. Change notifications
// ===========================================================================================

void ProjectorWindow::notifyRepaint(const PageRef& page) {
    if (!this->visible) {
        return;
    }
    // Compared against the page the last frame actually drew, rather than asking the document what
    // the current page is. This runs once per motion event while a stroke is being drawn, and the
    // document lock may well be held by the caller -- so it must not touch the document at all.
    if (page && this->shownPage && page != this->shownPage) {
        return;
    }
    queueRedraw();
}

void ProjectorWindow::documentChanged(DocumentChangeType) { queueRedraw(); }
void ProjectorWindow::pageSizeChanged(size_t) { queueRedraw(); }
void ProjectorWindow::pageChanged(size_t) { queueRedraw(); }
void ProjectorWindow::pageInserted(size_t) { queueRedraw(); }
void ProjectorWindow::pageDeleted(size_t) { queueRedraw(); }
void ProjectorWindow::pageSelected(size_t) { queueRedraw(); }
