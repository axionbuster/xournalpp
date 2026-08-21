#include "TextEditor.h"

#include <cstring>  // for strcmp, size_t
#include <memory>   // for allocator, make_unique, __shared_p...
#include <string>   // for std::string()
#include <utility>  // for move

#include <gdk/gdkkeysyms.h>  // for GDK_KEY_B, GDK_KEY_ISO_Enter, GDK_...
#include <glib-object.h>     // for g_object_get, g_object_unref, G_CA...

#include "control/AudioController.h"
#include "control/Control.h"  // for Control
#include "control/actions/ActionDatabase.h"
#include "control/settings/Settings.h"
#include "gui/FlyingClickableIcon.h"
#include "gui/XournalppCursor.h"  // for XournalppCursor
#include "model/Document.h"       // for Document
#include "model/Font.h"           // for XojFont
#include "model/Text.h"           // for Text
#include "model/TextAlignment.h"  // for TextAlignment
#include "model/TextStyleRuns.h"  // for TextStyleRuns
#include "model/XojPage.h"        // for XojPage
#include "undo/DeleteUndoAction.h"
#include "undo/InsertUndoAction.h"
#include "undo/TextBoxUndoAction.h"
#include "undo/UndoRedoHandler.h"  // for UndoRedoHandler
#include "util/Assert.h"
#include "util/DispatchPool.h"
#include "util/Range.h"
#include "util/glib_casts.h"  // for wrap_for_once_v
#include "util/gtk4_helper.h"
#include "util/raii/CStringWrapper.h"
#include "util/safe_casts.h"  // for round_cast, as_unsigned
#include "view/overlays/TextEditionView.h"

#include "TextEditorKeyBindings.h"
#include "TextRunTags.h"

using xoj::text::getByteOffsetOfIterator;
using xoj::text::getIteratorAtByteOffset;
using xoj::text::InlineStyle;

class UndoAction;

static constexpr auto MOVE_ICON_NAME = "xopp-move";
static constexpr auto EXTEND_ICON_NAME = "xopp-wrap";

/** GtkTextBuffer helper functions **/
static auto getIteratorAtCursor(GtkTextBuffer* buffer) -> GtkTextIter {
    GtkTextIter cursorIter = {nullptr};
    gtk_text_buffer_get_iter_at_mark(buffer, &cursorIter, gtk_text_buffer_get_insert(buffer));
    return cursorIter;
}

static auto getByteOffsetOfCursor(GtkTextBuffer* buffer) -> int {
    return getByteOffsetOfIterator(getIteratorAtCursor(buffer));
}

static auto cloneToCString(GtkTextBuffer* buf) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    return xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_text(&start, &end));
}

/**
 * @brief Clone the buffer's content
 *
 * This is pretty inefficient: the text gets copied twice
 */
static auto cloneToStdString(GtkTextBuffer* buf) -> std::string { return cloneToCString(buf).get(); }

/**
 * @brief Clone the buffer's content and insert a string into the clone
 * This makes one less copy operation over using cloneToStdString followed by insert()
 * (The text is copied "only" twice, and not three times)
 */
static auto cloneWithInsertToStdString(GtkTextBuffer* buf, std::string_view insertedStr) -> std::string {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    GtkTextIter insertionPoint = getIteratorAtCursor(buf);

    auto firstHalf = xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_text(&start, &insertionPoint));
    auto secondHalf = xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_text(&insertionPoint, &end));

    std::string_view str1(firstHalf);
    std::string_view str2(secondHalf);

    std::string res;
    res.reserve(str1.length() + str2.length() + insertedStr.length());
    res += str1;
    res += insertedStr;
    res += str2;

    return res;
}

TextEditor::TextEditor(Control* control, const PageRef& page, GtkWidget* xournalWidget, double x, double y):
        control(control),
        page(page),
        xournalWidget(xournalWidget),
        imContext(gtk_im_multicontext_new(), xoj::util::adopt),
        buffer(gtk_text_buffer_new(nullptr), xoj::util::adopt),
        viewPool(std::make_shared<xoj::util::DispatchPool<xoj::view::TextEditionView>>()) {
    // Informs the windowing system of the selection -- i.e. for accessibility purposes
    gtk_text_buffer_add_selection_clipboard(buffer.get(), gtk_clipboard_get(GDK_SELECTION_PRIMARY));

    this->initializeEditionAt(x, y);
    g_signal_connect(this->buffer.get(), "paste-done", G_CALLBACK(bufferPasteDoneCallback), this);
    /*
     * Every way text enters the buffer -- an Input Method commit, a paste, a line break, a
     * tabulation -- ends up emitting "insert-text", so the styling of inserted text is decided
     * in one place instead of at each call site. The first handler runs before the insertion,
     * while the character to the left of the insertion point is still the one to inherit from;
     * the second runs after it, when the inserted range is known.
     */
    g_signal_connect(this->buffer.get(), "insert-text", G_CALLBACK(bufferInsertTextCallback), this);
    g_signal_connect_after(this->buffer.get(), "insert-text", G_CALLBACK(bufferInsertedTextCallback), this);

    {  // Get cursor blinking settings
        GtkSettings* settings = gtk_widget_get_settings(this->xournalWidget);
        g_object_get(settings, "gtk-cursor-blink", &this->cursorBlink, nullptr);
        if (this->cursorBlink) {
            int tmp = 0;
            g_object_get(settings, "gtk-cursor-blink-time", &tmp, nullptr);
            xoj_assert(tmp >= 0);
            auto cursorBlinkingPeriod = static_cast<unsigned int>(tmp);
            this->cursorBlinkingTimeOn = cursorBlinkingPeriod * CURSOR_ON_MULTIPLIER / CURSOR_DIVIDER;
            this->cursorBlinkingTimeOff = cursorBlinkingPeriod - this->cursorBlinkingTimeOn;
        }
    }

    gtk_im_context_set_client_widget(this->imContext.get(), this->xournalWidget);
    gtk_im_context_focus_in(this->imContext.get());

    g_signal_connect(this->imContext.get(), "commit", G_CALLBACK(iMCommitCallback), this);
    g_signal_connect(this->imContext.get(), "preedit-changed", G_CALLBACK(iMPreeditChangedCallback), this);
    g_signal_connect(this->imContext.get(), "retrieve-surrounding", G_CALLBACK(iMRetrieveSurroundingCallback), this);
    g_signal_connect(this->imContext.get(), "delete-surrounding", G_CALLBACK(imDeleteSurroundingCallback), this);

    if (this->originalTextElement) {
        // If editing a preexisting text, put the cursor at the right location
        this->mousePressed(x, y);
    } else if (this->cursorBlink) {
        blinkCallback(this);
    } else {
        this->cursorVisible = true;
    }

    this->moveIcon = [&]() {
        auto icon = std::make_unique<FlyingClickableIcon>(control->getWindow(), MOVE_ICON_NAME,
                                                          FlyingClickableIcon::Anchor::SOUTH_EAST);

        GtkWidget* w = icon->getWidget();

#if GTK_MAJOR_VERSION == 3
        gtk_widget_add_css_class(gtk_bin_get_child(GTK_BIN(w)), "TL");
        GtkGesture* drag = gtk_gesture_drag_new(w);
#else
        gtk_widget_add_css_class(w, "TL");
        GtkGesture* drag = gtk_gesture_drag_new();
        gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(drag));
#endif

        icon->addSignal(
                G_OBJECT(drag),
                g_signal_connect(drag, "drag-update",
                                 G_CALLBACK(+[](GtkGestureDrag*, gdouble offsetX, gdouble offsetY, gpointer p) {
                                     // Warning: because the icon is moved, the parameters offsetX and offsetY
                                     // are NOT relative to the starting point, but rather to the last update's
                                     // position.
                                     auto* self = static_cast<TextEditor*>(p);
                                     if (!self->viewPool->empty()) {
                                         // We use the first view as the main view
                                         auto* text = self->getTextElement();
                                         const auto zoom = self->viewPool->front().getZoom();
                                         const xoj::util::Point<double> move(offsetX / zoom, offsetY / zoom);
                                         const auto newOrigin = text->getOrigin() + move;
                                         const double width = self->currentWrapWidth == Text::NO_WRAP ?
                                                                      self->getContentBoundingBox().getWidth() :
                                                                      self->currentWrapWidth;
                                         if (newOrigin.x > 0 && newOrigin.x + width < self->page->getWidth() &&
                                             newOrigin.y > 0 &&
                                             newOrigin.y + self->getContentBoundingBox().getHeight() <
                                                     self->page->getHeight()) {
                                             // The text stays entirely in the page
                                             text->move(move.x, move.y);
                                             self->repaintEditor(true);
                                         }
                                     }
                                 }),
                                 this));
        // Should we implement signals drag-end/cancel here?
        return icon;
    }();
    this->extendIcon = [&]() {
        auto icon = std::make_unique<FlyingClickableIcon>(control->getWindow(), EXTEND_ICON_NAME,
                                                          FlyingClickableIcon::Anchor::SOUTH_WEST);

        GtkWidget* w = icon->getWidget();

#if GTK_MAJOR_VERSION == 3
        gtk_widget_add_css_class(gtk_bin_get_child(GTK_BIN(w)), "TR");
        GtkGesture* drag = gtk_gesture_drag_new(w);
#else
        gtk_widget_add_css_class(w, "TR");
        GtkGesture* drag = gtk_gesture_drag_new();
        gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(drag));
#endif

        icon->addSignal(G_OBJECT(drag),
                        g_signal_connect(drag, "drag-begin",
                                         G_CALLBACK(+[](GtkGestureDrag*, gdouble startX, gdouble startY, gpointer p) {
                                             auto* self = static_cast<TextEditor*>(p);
                                             if (self->currentWrapWidth == Text::NO_WRAP) {
                                                 self->currentWrapWidth = self->getContentBoundingBox().getWidth();
                                             }
                                         }),
                                         this));
        icon->addSignal(G_OBJECT(drag),
                        g_signal_connect(drag, "drag-update",
                                         G_CALLBACK(+[](GtkGestureDrag*, gdouble offsetX, gdouble offsetY, gpointer p) {
                                             // Warning: because the icon is moved, the parameters offsetX and offsetY
                                             // are NOT relative to the starting point, but rather to the last update's
                                             // position.
                                             auto* self = static_cast<TextEditor*>(p);
                                             if (!self->viewPool->empty()) {
                                                 // We use the first view as the main view
                                                 if (double newVal = self->currentWrapWidth +
                                                                     offsetX / self->viewPool->front().getZoom();
                                                     newVal > 0 &&
                                                     newVal < self->page->getWidth() -
                                                                      self->getTextElement()->getOrigin().x) {
                                                     // The new width does not overflow out of the page
                                                     self->currentWrapWidth = newVal;
                                                     self->layoutStatus = LayoutStatus::NEEDS_PARAMETERS_UPDATE;
                                                     self->repaintEditor(true);
                                                 }
                                             }
                                         }),
                                         this));
        icon->addSignal(G_OBJECT(drag),
                        g_signal_connect(drag, "drag-end",
                                         G_CALLBACK(+[](GtkGestureDrag*, gdouble offsetX, gdouble offsetY, gpointer p) {
                                             auto* self = static_cast<TextEditor*>(p);
                                             self->textElement->setWrap(self->currentWrapWidth);
                                         }),
                                         this));
        icon->addSignal(G_OBJECT(drag),
                        g_signal_connect(drag, "cancel", G_CALLBACK(+[](GtkGesture*, GdkEventSequence*, gpointer p) {
                                             auto* self = static_cast<TextEditor*>(p);
                                             self->currentWrapWidth = self->textElement->getWrap();
                                             self->layoutStatus = LayoutStatus::NEEDS_PARAMETERS_UPDATE;
                                             self->repaintEditor(true);
                                         }),
                                         this));
        // Move both icons when scrolling/zooming
        auto cb = G_CALLBACK(+[](GtkAdjustment*, gpointer p) { static_cast<TextEditor*>(p)->updateDraggableIcons(); });
        auto* hadj = G_OBJECT(gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(xournalWidget)));
        icon->addSignal(hadj, g_signal_connect(hadj, "value-changed", cb, this));
        icon->addSignal(hadj, g_signal_connect(hadj, "changed", cb, this));
        auto* vadj = G_OBJECT(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(xournalWidget)));
        icon->addSignal(vadj, g_signal_connect(vadj, "value-changed", cb, this));
        icon->addSignal(vadj, g_signal_connect(vadj, "changed", cb, this));
        return icon;
    }();
}

TextEditor::~TextEditor() {
    gtk_im_context_focus_out(this->imContext.get());

    this->xournalWidget = nullptr;
    control->setCopyCutEnabled(false);

    this->contentsChanged(true);

    finalizeEdition();
}

auto TextEditor::getViewPool() const -> const std::shared_ptr<xoj::util::DispatchPool<xoj::view::TextEditionView>>& {
    return viewPool;
}

void TextEditor::onViewCreation() const {
    this->updateDraggableIcons();  // The icons are placed on the first view. The view must have been created for that
}

auto TextEditor::getTextElement() const -> Text* { return this->textElement.get(); }

bool TextEditor::bufferEmpty() const { return gtk_text_buffer_get_char_count(this->buffer.get()) == 0; }

void TextEditor::replaceBufferContent(const std::string& text) {
    gtk_text_buffer_set_text(this->buffer.get(), text.c_str(), -1);

    GtkTextIter first = {nullptr};
    gtk_text_buffer_get_iter_at_offset(this->buffer.get(), &first, 0);
    gtk_text_buffer_place_cursor(this->buffer.get(), &first);
    this->layoutStatus = LayoutStatus::NEEDS_COMPLETE_UPDATE;
    this->cursorBox = computeCursorBox();
}

void TextEditor::setColor(Color color) {
    GtkTextIter start;
    GtkTextIter end;
    if (gtk_text_buffer_get_selection_bounds(this->buffer.get(), &start, &end)) {
        /*
         * A color picked while part of the text is selected colors that part only. Picking the
         * element's own color back removes the override rather than recording a run that says
         * what the element already says.
         */
        const std::optional<Color> colorOverride =
                color == this->textElement->getColor() ? std::nullopt : std::optional<Color>(color);
        applyStyleToSelection([&](InlineStyle& s) { s.color = colorOverride; });
        return;
    }

    this->textElement->setColor(color);
    repaintEditor(false);
}

void TextEditor::applyStyleToSelection(const std::function<void(InlineStyle&)>& change) {
    GtkTextIter start;
    GtkTextIter end;
    if (!gtk_text_buffer_get_selection_bounds(this->buffer.get(), &start, &end)) {
        return;
    }

    /*
     * The selection is re-styled stretch by stretch: whatever styling it already carries is kept
     * except for the part `change` overwrites, so that italicizing a range that is partly red
     * leaves the red where it was.
     */
    GtkTextIter it = start;
    while (gtk_text_iter_compare(&it, &end) < 0) {
        GtkTextIter next = it;
        if (!gtk_text_iter_forward_to_tag_toggle(&next, nullptr) || gtk_text_iter_compare(&next, &end) > 0) {
            next = end;
        }

        InlineStyle style = xoj::text::styleAt(&it);
        change(style);
        xoj::text::applyStyle(this->buffer.get(), &it, &next, style);

        it = next;
    }

    // The glyphs change shape, so the box may grow or shrink
    this->layoutStatus = LayoutStatus::NEEDS_ATTRIBUTES_UPDATE;
    this->repaintEditor(true);
}

void TextEditor::setFont(XojFont font) {
    this->textElement->setFont(font);
    afterFontChange();
}

void TextEditor::setAlignment(TextAlignment al) {
    this->textElement->setAlignment(al);
    this->layoutStatus = LayoutStatus::NEEDS_PARAMETERS_UPDATE;
    repaintEditor(true);  // The size may change if the text overflows
}

void TextEditor::setJustify(bool justify) {
    this->textElement->setJustify(justify);
    this->layoutStatus = LayoutStatus::NEEDS_PARAMETERS_UPDATE;
    repaintEditor(true);
}

void TextEditor::afterFontChange() {
    /*
     * Ctrl+B, Ctrl+I and Ctrl+plus change the element's own font without going through the font
     * action, so the font button would otherwise keep showing the font as it was when the
     * edition started -- and the font dialog, as well as "save current font as preset", would
     * start from that stale font.
     */
    this->control->getActionDatabase()->setActionState(Action::FONT,
                                                       this->textElement->getFont().asString().c_str());

    this->textElement->updatePangoFont(this->layout.get());
    this->computeVirtualCursorPosition();
    this->repaintEditor();
}

void TextEditor::iMCommitCallback(GtkIMContext* context, const gchar* str, TextEditor* te) {
    gtk_text_buffer_begin_user_action(te->buffer.get());

    bool hadSelection = gtk_text_buffer_get_has_selection(te->buffer.get());
    if (hadSelection) {
        gtk_text_buffer_delete_selection(te->buffer.get(), true, true);
        te->control->setCopyCutEnabled(false);
    }

    if (!strcmp(str, "\n")) {
        if (!gtk_text_buffer_insert_interactive_at_cursor(te->buffer.get(), "\n", 1, true)) {
            gtk_widget_error_bell(te->xournalWidget);
        } else {
            te->contentsChanged(true);
        }
    } else {
        if (!hadSelection && te->cursorOverwrite) {
            auto insert = getIteratorAtCursor(te->buffer.get());
            if (!gtk_text_iter_ends_line(&insert)) {
                te->deleteFromCursor(GTK_DELETE_CHARS, 1);
            }
        }

        if (!gtk_text_buffer_insert_interactive_at_cursor(te->buffer.get(), str, -1, true)) {
            gtk_widget_error_bell(te->xournalWidget);
        }
    }

    gtk_text_buffer_end_user_action(te->buffer.get());
    te->contentsChanged();
    te->repaintEditor();
}

void TextEditor::iMPreeditChangedCallback(GtkIMContext* context, TextEditor* te) {
    xoj::util::OwnedCString str;
    gint cursor_pos = 0;
    GtkTextIter iter = getIteratorAtCursor(te->buffer.get());

    {
        PangoAttrList* attrs = nullptr;
        gtk_im_context_get_preedit_string(context, str.contentReplacer(), &attrs, &cursor_pos);
        if (attrs == nullptr) {
            attrs = pango_attr_list_new();
        }
        te->preeditAttrList.reset(attrs, xoj::util::adopt);
    }

    if (str && str[0] && !gtk_text_iter_can_insert(&iter, true)) {
        /*
         * Keypress events are passed to input method even if cursor position is
         * not editable; so beep here if it's multi-key input sequence, input
         * method will be reset in key-press-event handler.
         */
        gtk_widget_error_bell(te->xournalWidget);
        return;
    }

    te->preeditString = std::move(str);
    te->preeditCursor = cursor_pos;
    te->contentsChanged();
    te->repaintEditor();
}

auto TextEditor::iMRetrieveSurroundingCallback(GtkIMContext* context, TextEditor* te) -> bool {
    GtkTextIter start = getIteratorAtCursor(te->buffer.get());
    GtkTextIter end = start;

    gint pos = gtk_text_iter_get_line_index(&start);
    gtk_text_iter_set_line_offset(&start, 0);
    gtk_text_iter_forward_to_line_end(&end);

    auto text = xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_slice(&start, &end));
    gtk_im_context_set_surrounding(context, text.get(), -1, pos);

    return true;
}

auto TextEditor::imDeleteSurroundingCallback(GtkIMContext* context, gint offset, gint n_chars, TextEditor* te) -> bool {
    GtkTextIter start = getIteratorAtCursor(te->buffer.get());
    GtkTextIter end = start;

    gtk_text_iter_forward_chars(&start, offset);
    gtk_text_iter_forward_chars(&end, offset + n_chars);

    gtk_text_buffer_delete_interactive(te->buffer.get(), &start, &end, true);

    te->contentsChanged();
    te->repaintEditor();

    return true;
}

auto TextEditor::onKeyPressEvent(const KeyEvent& event) -> bool {

    // IME needs to handle the input first so the candidate window works correctly
    if (gtk_im_context_filter_keypress(this->imContext.get(), event.sourceEvent)) {
        this->needImReset = true;

        GtkTextIter iter = getIteratorAtCursor(this->buffer.get());
        bool canInsert = gtk_text_iter_can_insert(&iter, true);

        if (canInsert) {
            control->getCursor()->setInvisible(true);
        } else {
            this->resetImContext();
        }
        return true;
    }

    return keyBindings.processEvent(this, event);
}

auto TextEditor::onKeyReleaseEvent(const KeyEvent& event) -> bool {
    GtkTextIter iter = getIteratorAtCursor(this->buffer.get());

    if (gtk_text_iter_can_insert(&iter, true) &&
        gtk_im_context_filter_keypress(this->imContext.get(), event.sourceEvent)) {
        this->needImReset = true;
        return true;
    }
    return false;
}

void TextEditor::toggleOverwrite() {
    this->cursorOverwrite = !this->cursorOverwrite;
    repaintCursorAfterChange();
}

/**
 * I know it's a bit rough and duplicated
 * Improve that later on...
 */
void TextEditor::decreaseFontSize() {
    XojFont& font = textElement->getFont();
    if (double size = font.getSize(); size > 1) {
        font.setSize(font.getSize() - 1);
        afterFontChange();
    }
}

void TextEditor::increaseFontSize() {
    XojFont& font = textElement->getFont();
    font.setSize(font.getSize() + 1);
    afterFontChange();
}

/**
 * @brief Toggle bold or italic, over the selection if there is one and over what gets typed next
 * otherwise.
 *
 * Both are inline styles: they apply to a stretch of the text rather than to the element, so the
 * element's own font is left alone and the styling rides along as style runs.
 */
void TextEditor::toggleStyle(std::optional<bool> InlineStyle::* which, bool baseState) {
    /*
     * A run says bold, not bold, or nothing at all, and only the last of the three lets the
     * element's own font decide. So the toggle records what the user actually asked for --
     * unless that is what the font already says, in which case saying nothing is both smaller
     * and what keeps the range following a later font change.
     */
    auto asStored = [baseState](bool wanted) {
        return wanted == baseState ? std::optional<bool>{} : std::optional<bool>{wanted};
    };

    GtkTextIter start;
    GtkTextIter end;
    if (gtk_text_buffer_get_selection_bounds(this->buffer.get(), &start, &end)) {
        // A selection that is styled throughout gets the style taken away; anything else gets it
        const bool turnOn = !xoj::text::rangeIsFullyStyled(which, baseState, &start, &end);
        applyStyleToSelection([&](InlineStyle& s) { s.*which = asStored(turnOn); });
        return;
    }

    /*
     * Without a selection the toggle is about what comes next: it flips a pending style, which
     * the next insertion uses in place of what it would otherwise inherit from its left. The
     * pending style starts from that same inherited style, so pressing Ctrl+B in the middle of
     * red text and typing gives bold red text.
     */
    if (!this->pendingStyle) {
        const GtkTextIter cursor = getIteratorAtCursor(this->buffer.get());
        this->pendingStyle = xoj::text::styleLeftOf(&cursor);
    }
    (*this->pendingStyle).*which = asStored(!(((*this->pendingStyle).*which).value_or(baseState)));
}

auto TextEditor::baseFontIsBold() const -> bool {
    PangoFontDescription* desc = pango_font_description_from_string(this->textElement->getFontName().c_str());
    const bool bold = pango_font_description_get_weight(desc) >= PANGO_WEIGHT_BOLD;
    pango_font_description_free(desc);
    return bold;
}

auto TextEditor::baseFontIsItalic() const -> bool {
    PangoFontDescription* desc = pango_font_description_from_string(this->textElement->getFontName().c_str());
    const bool italic = pango_font_description_get_style(desc) != PANGO_STYLE_NORMAL;
    pango_font_description_free(desc);
    return italic;
}

void TextEditor::toggleBoldFace() { toggleStyle(&InlineStyle::bold, baseFontIsBold()); }

void TextEditor::toggleItalic() { toggleStyle(&InlineStyle::italic, baseFontIsItalic()); }

auto TextEditor::styleForInsertionAt(const GtkTextIter* location) const -> InlineStyle {
    if (this->pendingStyle) {
        return *this->pendingStyle;
    }
    // Text takes after the character it is typed behind
    return xoj::text::styleLeftOf(location);
}

void TextEditor::bufferInsertTextCallback(GtkTextBuffer* buffer, GtkTextIter* location, const gchar* text, gint len,
                                          TextEditor* te) {
    if (te->pasteInProgress && te->pasteRange) {
        // However many chunks GTK splits a paste into, it lands behind one character
        return;
    }
    // Decided before the insertion, while the character to the left of it is still the old one
    te->insertionStyle = te->styleForInsertionAt(location);
}

void TextEditor::bufferInsertedTextCallback(GtkTextBuffer* buffer, GtkTextIter* location, const gchar* text, gint len,
                                            TextEditor* te) {
    // GTK revalidated the iterator to the end of the inserted text
    const int end = getByteOffsetOfIterator(*location);
    const int start = end - len;
    if (start < 0) {
        return;  // Should not happen; better than tagging a bogus range
    }

    if (te->pasteInProgress) {
        // Whatever the clipboard brought is applied after this returns; see TextEditor::pasteRange
        te->pasteRange = {te->pasteRange ? te->pasteRange->first : start, end};
        return;
    }

    GtkTextIter startIter = getIteratorAtByteOffset(buffer, start);
    xoj::text::applyStyle(buffer, &startIter, location, te->insertionStyle);
}

void TextEditor::selectAtCursor(TextEditor::SelectType ty) {
    GtkTextIter startPos;
    GtkTextIter endPos;
    gtk_text_buffer_get_selection_bounds(this->buffer.get(), &startPos, &endPos);
    const auto searchFlag = GTK_TEXT_SEARCH_TEXT_ONLY;  // To be used to find double newlines

    switch (ty) {
        case TextEditor::SelectType::WORD: {
            auto currentPos = getIteratorAtCursor(this->buffer.get());
            if (!gtk_text_iter_inside_word(&currentPos)) {
                // Do nothing if cursor is over whitespace
                return;
            }

            if (!gtk_text_iter_starts_word(&currentPos)) {
                gtk_text_iter_backward_word_start(&startPos);
            }
            if (!gtk_text_iter_ends_word(&currentPos)) {
                gtk_text_iter_forward_word_end(&endPos);
            }
            break;
        }
        case TextEditor::SelectType::PARAGRAPH:
            // Note that a GTK "paragraph" is a line, so there's no nice one-liner.
            // We define a paragraph as text separated by double newlines.
            while (!gtk_text_iter_is_start(&startPos)) {
                // There's no GTK function to go to line start, so do it manually.
                while (!gtk_text_iter_starts_line(&startPos)) {
                    if (!gtk_text_iter_backward_word_start(&startPos)) {
                        break;
                    }
                }
                // Check for paragraph start
                GtkTextIter searchPos = startPos;
                gtk_text_iter_backward_chars(&searchPos, 2);
                if (gtk_text_iter_backward_search(&startPos, "\n\n", searchFlag, nullptr, nullptr, &searchPos)) {
                    break;
                }
                gtk_text_iter_backward_line(&startPos);
            }
            while (!gtk_text_iter_ends_line(&endPos)) {
                gtk_text_iter_forward_to_line_end(&endPos);
                // Check for paragraph end
                GtkTextIter searchPos = endPos;
                gtk_text_iter_forward_chars(&searchPos, 2);
                if (gtk_text_iter_forward_search(&endPos, "\n\n", searchFlag, nullptr, nullptr, &searchPos)) {
                    break;
                }
                gtk_text_iter_forward_line(&endPos);
            }
            break;
        case TextEditor::SelectType::ALL:
            gtk_text_buffer_get_bounds(this->buffer.get(), &startPos, &endPos);
            break;
    }

    gtk_text_buffer_select_range(this->buffer.get(), &startPos, &endPos);
    this->pendingStyle.reset();

    control->setCopyCutEnabled(gtk_text_buffer_get_has_selection(this->buffer.get()));

    // Selection highlighting is handled through Pango attributes
    this->layoutStatus = LayoutStatus::NEEDS_ATTRIBUTES_UPDATE;
    this->repaintEditor(false);
}

void TextEditor::moveCursor(GtkMovementStep step, int count, bool extendSelection) {
    resetImContext();

    GtkTextIter insert = getIteratorAtCursor(this->buffer.get());
    GtkTextIter newplace = insert;

    bool updateVirtualCursor = true;

    switch (step) {
        case GTK_MOVEMENT_LOGICAL_POSITIONS:  // not used!?
            gtk_text_iter_forward_visible_cursor_positions(&newplace, count);
            break;
        case GTK_MOVEMENT_VISUAL_POSITIONS:
            if (count < 0) {
                gtk_text_iter_backward_cursor_position(&newplace);
            } else {
                gtk_text_iter_forward_cursor_position(&newplace);
            }
            break;

        case GTK_MOVEMENT_WORDS:
            if (count < 0) {
                gtk_text_iter_backward_visible_word_starts(&newplace, -count);
            } else if (count > 0) {
                if (!gtk_text_iter_forward_visible_word_ends(&newplace, count)) {
                    gtk_text_iter_forward_to_line_end(&newplace);
                }
            }
            break;

        case GTK_MOVEMENT_DISPLAY_LINES:
            updateVirtualCursor = false;
            jumpALine(&newplace, count);
            break;

        case GTK_MOVEMENT_PARAGRAPHS:
            if (count > 0) {
                if (!gtk_text_iter_ends_line(&newplace)) {
                    gtk_text_iter_forward_to_line_end(&newplace);
                    --count;
                }
                gtk_text_iter_forward_visible_lines(&newplace, count);
                gtk_text_iter_forward_to_line_end(&newplace);
            } else if (count < 0) {
                if (gtk_text_iter_get_line_offset(&newplace) > 0) {
                    gtk_text_iter_set_line_offset(&newplace, 0);
                }
                gtk_text_iter_forward_visible_lines(&newplace, count);
                gtk_text_iter_set_line_offset(&newplace, 0);
            }
            break;

        case GTK_MOVEMENT_DISPLAY_LINE_ENDS:
        case GTK_MOVEMENT_PARAGRAPH_ENDS:
            if (count > 0) {
                if (!gtk_text_iter_ends_line(&newplace)) {
                    gtk_text_iter_forward_to_line_end(&newplace);
                }
            } else if (count < 0) {
                gtk_text_iter_set_line_offset(&newplace, 0);
            }
            break;

        case GTK_MOVEMENT_BUFFER_ENDS:
            if (count > 0) {
                gtk_text_buffer_get_end_iter(this->buffer.get(), &newplace);
            } else if (count < 0) {
                gtk_text_buffer_get_iter_at_offset(this->buffer.get(), &newplace, 0);
            }
            break;

        default:
            break;
    }

    // call moveCursorIterator() even if the cursor hasn't moved, since it cancels the selection
    moveCursorIterator(&newplace, extendSelection);

    if (updateVirtualCursor) {
        computeVirtualCursorPosition();
    }

    if (gtk_text_iter_equal(&insert, &newplace)) {
        gtk_widget_error_bell(this->xournalWidget);
    }
}

void TextEditor::findPos(GtkTextIter* iter, double xPos, double yPos) const {
    int index = 0;
    int trailing = 0;
    pango_layout_xy_to_index(this->getUpToDateLayout(), round_cast<int>(xPos * PANGO_SCALE),
                             round_cast<int>(yPos * PANGO_SCALE), &index, &trailing);
    /*
     * trailing is non-zero iff the abscissa is past the middle of the grapheme.
     * In this case, it contains the length of the grapheme in utf8 char count.
     * This way, we put the cursor after the grapheme when clicking past the middle of the grapheme.
     */
    *iter = getIteratorAtByteOffset(this->buffer.get(), index);
    gtk_text_iter_forward_chars(iter, trailing);
}

void TextEditor::updateTextElementContent() {
    this->textElement->setText(cloneToStdString(this->buffer.get()));
    // The tags GTK maintained through the whole edition become the element's style runs
    this->textElement->setStyleRuns(xoj::text::runsFromBuffer(this->buffer.get()));
}

void TextEditor::contentsChanged(bool forceCreateUndoAction) {
    // Todo: Reinstate text edition undo stack
    this->layoutStatus = LayoutStatus::NEEDS_COMPLETE_UPDATE;
    this->computeVirtualCursorPosition();
}

void TextEditor::markPos(double x, double y, bool extendSelection) {
    GtkTextIter newplace = getIteratorAtCursor(this->buffer.get());

    findPos(&newplace, x, y);

    // call moveCursorIterator() even if the cursor hasn't moved, since it cancels the selection
    moveCursorIterator(&newplace, extendSelection);
    computeVirtualCursorPosition();
}

void TextEditor::mousePressed(double x, double y) {
    this->mouseDown = true;
    // Todo select if SHIFT is pressed
    const auto& origin = textElement->getOrigin();
    this->markPos(x - origin.x, y - origin.y, false);
}

void TextEditor::mouseMoved(double x, double y) {
    if (this->mouseDown) {
        const auto& origin = textElement->getOrigin();
        this->markPos(x - origin.x, y - origin.y, true);
    }
}

void TextEditor::mouseReleased() { this->mouseDown = false; }

void TextEditor::jumpALine(GtkTextIter* textIter, int count) {
    count += this->virtualCursorPosition.pangoLineNumber;
    if (count < 0) {
        return;
    }

    PangoLayoutLine* line = pango_layout_get_line_readonly(this->layout.get(), count);
    if (line == nullptr) {
        return;
    }
    this->virtualCursorPosition.pangoLineNumber = count;

    int index = 0;
    int trailing = 0;
    pango_layout_line_x_to_index(line, this->virtualCursorPosition.abscissa, &index, &trailing);
    /*
     * trailing is non-zero iff the abscissa is past the middle of the grapheme.
     * In this case, it contains the length of the grapheme in utf8 char count.
     */
    *textIter = getIteratorAtByteOffset(this->buffer.get(), index);
    gtk_text_iter_forward_chars(textIter, trailing);
}

void TextEditor::computeVirtualCursorPosition() {
    int offset = getByteOffsetOfCursor(this->buffer.get());

    pango_layout_index_to_line_x(this->getUpToDateLayout(), offset, 0, &this->virtualCursorPosition.pangoLineNumber,
                                 &this->virtualCursorPosition.abscissa);
}

void TextEditor::moveCursorIterator(const GtkTextIter* newLocation, gboolean extendSelection) {
    // A pending Ctrl+B / Ctrl+I applies where it was pressed. Moving away goes back to inheriting
    // the style of whatever the cursor now sits after.
    this->pendingStyle.reset();

    bool selectionChanged = true;
    if (extendSelection) {
        if (auto oldLoc = getIteratorAtCursor(this->buffer.get()); gtk_text_iter_equal(newLocation, &oldLoc)) {
            // Nothing changed
            return;
        }
        gtk_text_buffer_move_mark_by_name(this->buffer.get(), "insert", newLocation);
        control->setCopyCutEnabled(gtk_text_buffer_get_has_selection(this->buffer.get()));
    } else {
        // if !extendSelection, we clear the selection even if the cursor does not move
        selectionChanged = gtk_text_buffer_get_has_selection(this->buffer.get());
        gtk_text_buffer_place_cursor(this->buffer.get(), newLocation);
        control->setCopyCutEnabled(false);
    }

    if (this->cursorBlink) {
        // Whenever the cursor moves, the blinking cycle restarts from the start (i.e. the cursor is first shown).
        this->cursorVisible = false;  // Will be toggled to true by BlinkTimer::callback before the repaint
        blinkCallback(this);
    }

    if (selectionChanged) {
        // The selection background color is set through Pango attributes
        this->layoutStatus = LayoutStatus::NEEDS_ATTRIBUTES_UPDATE;
        // Repaint the entire box. Computing the exact area that was (un)selected would be better but complicated
        this->repaintEditor(false);
    } else {
        repaintCursorAfterChange();
    }
}

void TextEditor::updateCursorBox() {
    this->cursorBox = computeCursorBox();

    if (!viewPool->empty()) {
        // Inform the IM of the cursor location (for word selection popup's location)
        // We use the first view as the main view, as far as the IM is concerned
        const auto& origin = textElement->getOrigin();
        auto box = viewPool->front().toWidgetCoordinates(
                xoj::util::Rectangle<double>(this->cursorBox).translated(origin.x, origin.y));

        GdkRectangle cursorRect;  // cursor position in window coordinates
        cursorRect.x = static_cast<int>(box.x);
        cursorRect.y = static_cast<int>(box.y);
        cursorRect.height = static_cast<int>(box.height);
        cursorRect.width = static_cast<int>(box.width);
        gtk_im_context_set_cursor_location(this->imContext.get(), &cursorRect);
    }
}

void TextEditor::updateDraggableIcons() const {
    if (!viewPool->empty()) {
        // We use the first view as the main view
        Range range = this->getContentBoundingBox();
        range.minX = textElement->getSnappedBounds().x;
        auto box = viewPool->front().toWidgetCoordinates(xoj::util::Rectangle<double>(range));
        auto zoom = viewPool->front().getZoom();
        double extendIconPos = this->currentWrapWidth == Text::NO_WRAP ? box.width : this->currentWrapWidth * zoom;
        moveIcon->setPosition({floor_cast<int>(box.x), floor_cast<int>(box.y)});
        extendIcon->setPosition({ceil_cast<int>(box.x + extendIconPos), floor_cast<int>(box.y)});
    }
}

static auto whitespace(gunichar ch, gpointer user_data) -> gboolean { return (ch == ' ' || ch == '\t'); }

static auto not_whitespace(gunichar ch, gpointer user_data) -> gboolean { return !whitespace(ch, user_data); }

static auto find_whitepace_region(const GtkTextIter* center, GtkTextIter* start, GtkTextIter* end) -> gboolean {
    *start = *center;
    *end = *center;

    if (gtk_text_iter_backward_find_char(start, not_whitespace, nullptr, nullptr)) {
        gtk_text_iter_forward_char(start); /* we want the first whitespace... */
    }
    if (whitespace(gtk_text_iter_get_char(end), nullptr)) {
        gtk_text_iter_forward_find_char(end, not_whitespace, nullptr, nullptr);
    }

    return !gtk_text_iter_equal(start, end);
}

void TextEditor::deleteFromCursor(GtkDeleteType type, int count) {

    this->resetImContext();

    if (type == GTK_DELETE_CHARS) {
        // Char delete deletes the selection, if one exists
        if (gtk_text_buffer_delete_selection(this->buffer.get(), true, true)) {
            control->setCopyCutEnabled(false);
            this->contentsChanged(true);
            this->repaintEditor();
            return;
        }
    }

    GtkTextIter insert = getIteratorAtCursor(this->buffer.get());

    GtkTextIter start = insert;
    GtkTextIter end = insert;

    switch (type) {
        case GTK_DELETE_CHARS:
            gtk_text_iter_forward_cursor_positions(&end, count);
            break;

        case GTK_DELETE_WORD_ENDS:
            if (count > 0) {
                gtk_text_iter_forward_word_ends(&end, count);
            } else if (count < 0) {
                gtk_text_iter_backward_word_starts(&start, 0 - count);
            }
            break;

        case GTK_DELETE_WORDS:
            break;

        case GTK_DELETE_DISPLAY_LINE_ENDS:
            break;

        case GTK_DELETE_DISPLAY_LINES:
            break;

        case GTK_DELETE_PARAGRAPH_ENDS:
            if (count > 0) {
                /* If we're already at a newline, we need to
                 * simply delete that newline, instead of
                 * moving to the next one.
                 */
                if (gtk_text_iter_ends_line(&end)) {
                    gtk_text_iter_forward_line(&end);
                    --count;
                }

                while (count > 0) {
                    if (!gtk_text_iter_forward_to_line_end(&end)) {
                        break;
                    }

                    --count;
                }
            } else if (count < 0) {
                if (gtk_text_iter_starts_line(&start)) {
                    gtk_text_iter_backward_line(&start);
                    if (!gtk_text_iter_ends_line(&end)) {
                        gtk_text_iter_forward_to_line_end(&start);
                    }
                } else {
                    gtk_text_iter_set_line_offset(&start, 0);
                }
                ++count;

                gtk_text_iter_backward_lines(&start, -count);
            }
            break;

        case GTK_DELETE_PARAGRAPHS:
            if (count > 0) {
                gtk_text_iter_set_line_offset(&start, 0);
                gtk_text_iter_forward_to_line_end(&end);

                /* Do the lines beyond the first. */
                while (count > 1) {
                    gtk_text_iter_forward_to_line_end(&end);
                    --count;
                }
            }

            break;

        case GTK_DELETE_WHITESPACE: {
            find_whitepace_region(&insert, &start, &end);
        } break;

        default:
            break;
    }

    if (!gtk_text_iter_equal(&start, &end)) {
        gtk_text_buffer_begin_user_action(this->buffer.get());

        if (!gtk_text_buffer_delete_interactive(this->buffer.get(), &start, &end, true)) {
            gtk_widget_error_bell(this->xournalWidget);
        }

        gtk_text_buffer_end_user_action(this->buffer.get());
    } else {
        gtk_widget_error_bell(this->xournalWidget);
    }

    this->contentsChanged();
    this->repaintEditor();
}

void TextEditor::backspace() {

    resetImContext();

    // Backspace deletes the selection, if one exists
    if (gtk_text_buffer_delete_selection(this->buffer.get(), true, true)) {
        control->setCopyCutEnabled(false);
        this->contentsChanged();
        this->repaintEditor();
        return;
    }

    GtkTextIter insert = getIteratorAtCursor(this->buffer.get());

    if (gtk_text_buffer_backspace(this->buffer.get(), &insert, true, true)) {
        this->contentsChanged();
        this->repaintEditor();
    } else {
        gtk_widget_error_bell(this->xournalWidget);
    }
}

void TextEditor::linebreak() {
    this->resetImContext();
    iMCommitCallback(nullptr, "\n", this);

    control->getCursor()->setInvisible(true);
}

void TextEditor::tabulation() {
    resetImContext();
    Settings* settings = control->getSettings();
    if (!settings->getUseSpacesAsTab()) {
        iMCommitCallback(nullptr, "\t", this);
    } else {
        std::string indent(static_cast<size_t>(settings->getNumberOfSpacesForTab()), ' ');
        iMCommitCallback(nullptr, indent.c_str(), this);
    }

    control->getCursor()->setInvisible(true);
}


void TextEditor::copyToClipboard() const {
    auto* clipboard = gtk_widget_get_clipboard(this->xournalWidget);
    gtk_text_buffer_copy_clipboard(this->buffer.get(), clipboard);
}

void TextEditor::cutToClipboard() {
    auto* clipboard = gtk_widget_get_clipboard(this->xournalWidget);
    gtk_text_buffer_cut_clipboard(this->buffer.get(), clipboard, true);

    this->contentsChanged(true);
    this->repaintEditor();
}

void TextEditor::pasteFromClipboard() {
    auto* clipboard = gtk_widget_get_clipboard(this->xournalWidget);
    this->pasteRange.reset();
    this->pasteInProgress = true;
    gtk_text_buffer_paste_clipboard(this->buffer.get(), clipboard, nullptr, true);
}

void TextEditor::bufferPasteDoneCallback(GtkTextBuffer* buffer, GtkClipboard* clipboard, TextEditor* te) {
    te->pasteInProgress = false;
    if (const auto range = std::exchange(te->pasteRange, std::nullopt)) {
        GtkTextIter start = getIteratorAtByteOffset(buffer, range->first);
        GtkTextIter end = getIteratorAtByteOffset(buffer, range->second);
        /*
         * Styled clipboard content arrives with its own tags and is left exactly as copied.
         * Plain content -- anything from outside this fork -- takes after the character it lands
         * behind, the same as typing there would.
         */
        if (!xoj::text::rangeCarriesStyle(&start, &end)) {
            xoj::text::applyStyle(buffer, &start, &end, te->insertionStyle);
        }
    }

    te->contentsChanged(true);
    te->repaintEditor();

    if (te->textElement->getWrap() == Text::NO_WRAP && te->getContentBoundingBox().maxX > te->page->getWidth()) {
        te->textElement->setWrap(te->page->getWidth() - te->getContentBoundingBox().minX);
        te->currentWrapWidth = te->textElement->getWrap();
        te->layoutStatus = LayoutStatus::NEEDS_PARAMETERS_UPDATE;
        te->repaintEditor(true);
    }
}

void TextEditor::resetImContext() {
    if (this->needImReset) {
        this->needImReset = false;
        gtk_im_context_reset(this->imContext.get());
    }
}

/*
 * Blink!
 */
void TextEditor::blinkCallback(TextEditor* te) {
    te->cursorVisible = !te->cursorVisible;
    auto time = te->cursorVisible ? te->cursorBlinkingTimeOn : te->cursorBlinkingTimeOff;
    te->blinkTimer = g_timeout_add(time, xoj::util::wrap_for_once_v<blinkCallback>, te);

    Range dirtyRange = te->cursorBox;
    const auto& origin = te->textElement->getOrigin();
    dirtyRange.translate(origin.x, origin.y);
    te->viewPool->dispatch(xoj::view::TextEditionView::FLAG_DIRTY_REGION, dirtyRange);
}

void TextEditor::setTextToPangoLayout(PangoLayout* pl) const {
    std::string_view preed(preeditString);

    if (!preed.empty()) {
        // When using an Input Method, we need to insert the preeditString into the text at the cursor location
        std::string txt = cloneWithInsertToStdString(this->buffer.get(), preed);

        int pos = getByteOffsetOfCursor(this->buffer.get());
        xoj::util::PangoAttrListSPtr attrlist(pango_attr_list_new(), xoj::util::adopt);
        pango_attr_list_splice(attrlist.get(), this->preeditAttrList.get(), pos, static_cast<int>(preed.length()));

        /*
         * The committed text keeps its styling around the string being composed, and the
         * composition itself is drawn with the style it will commit with -- the pending Ctrl+B
         * where there is one, and otherwise whatever it inherits from the character to its left.
         */
        std::optional<TextStyleRun> composing;
        if (this->pendingStyle) {
            composing.emplace();
            composing->bold = this->pendingStyle->bold;
            composing->italic = this->pendingStyle->italic;
            composing->color = this->pendingStyle->color;
        }
        xoj::text::appendStyleRunAttributes(attrlist.get(), xoj::text::runsFromBuffer(this->buffer.get()),
                                            static_cast<size_t>(pos), preed.length(),
                                            composing ? &*composing : nullptr);

        pango_layout_set_attributes(pl, attrlist.get());

        pango_layout_set_text(pl, txt.c_str(), static_cast<int>(txt.length()));
    } else {
        setSelectionAttributesToPangoLayout(pl);
        pango_layout_set_text(pl, cloneToCString(this->buffer.get()).get(), -1);
    }
}

Color TextEditor::getSelectionColor() const { return this->control->getSettings()->getSelectionColor(); }

void TextEditor::setSelectionAttributesToPangoLayout(PangoLayout* pl) const {
    xoj::util::PangoAttrListSPtr attrlist(pango_attr_list_new(), xoj::util::adopt);

    // The inline styling and the selection background live in the same attribute list
    xoj::text::appendStyleRunAttributes(attrlist.get(), xoj::text::runsFromBuffer(this->buffer.get()));

    GtkTextIter start;
    GtkTextIter end;
    bool hasSelection = gtk_text_buffer_get_selection_bounds(this->buffer.get(), &start, &end);

    if (hasSelection) {
        auto selectionColorU16 = Util::argb_to_ColorU16(this->getSelectionColor());
        PangoAttribute* attrib =
                pango_attr_background_new(selectionColorU16.red, selectionColorU16.green, selectionColorU16.blue);
        attrib->start_index = static_cast<unsigned int>(getByteOffsetOfIterator(start));
        attrib->end_index = static_cast<unsigned int>(getByteOffsetOfIterator(end));

        pango_attr_list_insert(attrlist.get(), attrib);  // attrlist takes ownership of attrib
    }

    pango_layout_set_attributes(pl, attrlist.get());
}

auto TextEditor::computeBoundingBox() const -> Range {
    /*
     * NB: we cannot rely on Text::calcSize directly, since it would not take the size changes due to the IM
     * preeditString into account.
     */
    auto boxes = Text::computeBoxesForLayout(getUpToDateLayout(), textElement->getOrigin(), this->currentWrapWidth);
    return Range(boxes.bounds);
}

auto TextEditor::getUpToDateLayout() const -> PangoLayout* {
    switch (layoutStatus) {
        case LayoutStatus::NEEDS_COMPLETE_UPDATE:
            setTextToPangoLayout(this->layout.get());
            break;
        case LayoutStatus::NEEDS_ATTRIBUTES_UPDATE:
            setSelectionAttributesToPangoLayout(this->layout.get());
            break;
        case LayoutStatus::NEEDS_PARAMETERS_UPDATE:
            pango_layout_set_width(this->layout.get(), round_cast<int>(this->currentWrapWidth * PANGO_SCALE));
            pango_layout_set_justify(layout.get(), this->textElement->getJustify());
            pango_layout_set_alignment(layout.get(), this->textElement->getAlign().toPango());
            break;
        case LayoutStatus::UP_TO_DATE:
            break;
    }
    layoutStatus = LayoutStatus::UP_TO_DATE;
    return this->layout.get();
}

auto TextEditor::getCursorBox() const -> const Range& { return this->cursorBox; }

auto TextEditor::getContentBoundingBox() const -> const Range& { return this->previousBoundingBox; }

bool TextEditor::isCursorVisible() const { return cursorVisible; }

auto TextEditor::computeCursorBox() const -> Range {
    // Compute the bounding box of the active grapheme (i.e. the one just after the cursor)
    int offset = getByteOffsetOfCursor(this->buffer.get());
    if (this->preeditString && this->preeditCursor != 0) {
        const gchar* preeditText = this->preeditString.get();
        offset += static_cast<int>(g_utf8_offset_to_pointer(preeditText, preeditCursor) - preeditText);
    }
    PangoRectangle rect = {0};
    pango_layout_index_to_pos(getUpToDateLayout(), offset, &rect);
    const double ratio = 1.0 / PANGO_SCALE;

    // Warning: rect.width could be negative (e.g. for languages written from right to left).
    Range res(rect.x * ratio, rect.y * ratio);
    res.addPoint((rect.x + (cursorOverwrite ? rect.width : 0.0)) * ratio, (rect.y + rect.height) * ratio);
    return res;
}

void TextEditor::repaintEditor(bool sizeChanged) {
    Range dirtyRange(this->previousBoundingBox);
    if (sizeChanged) {
        this->previousBoundingBox = this->computeBoundingBox();
        dirtyRange = dirtyRange.unite(this->previousBoundingBox);
    }
    this->updateCursorBox();
    this->updateDraggableIcons();
    this->viewPool->dispatch(xoj::view::TextEditionView::FLAG_DIRTY_REGION, dirtyRange);
}

void TextEditor::repaintCursorAfterChange() {
    Range dirtyRange = this->cursorBox;
    this->updateCursorBox();
    dirtyRange = dirtyRange.unite(this->cursorBox);
    const auto& origin = this->textElement->getOrigin();

    dirtyRange.translate(origin.x, origin.y);
    this->viewPool->dispatch(xoj::view::TextEditionView::FLAG_DIRTY_REGION, dirtyRange);
}

void TextEditor::finalizeEdition() {

    auto* db = this->control->getActionDatabase();
    auto* th = this->control->getToolHandler();
    db->setActionState(Action::FONT, this->control->getSettings()->getFont().asString().c_str());
    db->setActionState(Action::TEXT_ALIGNMENT, th->getTextAlignment());
    db->setActionState(Action::TEXT_JUSTIFY, th->getTextJustify());
    db->setActionState(Action::TOOL_COLOR, th->getColorMaskAlpha());

    auto* doc = this->control->getDocument();
    UndoRedoHandler* undo = this->control->getUndoRedoHandler();

    if (this->bufferEmpty()) {
        // Delete the edited element from layer
        if (originalTextElement) {
            auto eraseDeleteUndoAction = std::make_unique<DeleteUndoAction>(page, true);
            doc->lock();
            Layer* layer = this->page->getSelectedLayer();
            auto [orig, elementIndex] = layer->removeElement(originalTextElement);
            doc->unlock();
            if (elementIndex != Element::InvalidIndex) [[likely]] {
                eraseDeleteUndoAction->addElement(layer, std::move(orig), elementIndex);
                undo->addUndoAction(std::move(eraseDeleteUndoAction));
            }  // A warning has already been issued otherwise
            originalTextElement = nullptr;
        }
        this->viewPool->dispatchAndClear(xoj::view::TextEditionView::FINALIZATION_REQUEST, this->previousBoundingBox);
        return;
    }

    this->updateTextElementContent();
    if (originalTextElement) {
        // Modifying a preexisting element
        this->viewPool->dispatchAndClear(xoj::view::TextEditionView::FINALIZATION_REQUEST, this->previousBoundingBox);

        doc->lock();
        Layer* layer = this->page->getSelectedLayer();
        auto [orig, _] = layer->removeElement(this->originalTextElement);
        auto ptr = this->textElement.get();
        layer->addElement(std::move(this->textElement));
        doc->unlock();

        this->page->fireElementChanged(ptr);

        if (orig) [[likely]] {
            xoj_assert(orig.get() == this->originalTextElement);
            this->originalTextElement->setInEditing(false);
            undo->addUndoAction(std::make_unique<TextBoxUndoAction>(this->page, layer, ptr, std::move(orig)));
        } else {
            // A warning has already been issued
            undo->addUndoAction(std::make_unique<InsertUndoAction>(this->page, layer, ptr));
        }
        originalTextElement = nullptr;
    } else {
        // Creating a new element
        auto ptr = this->textElement.get();
        doc->lock();
        Layer* layer = this->page->getSelectedLayer();
        layer->addElement(std::move(this->textElement));
        doc->unlock();
        this->viewPool->dispatchAndClear(xoj::view::TextEditionView::FINALIZATION_REQUEST, this->previousBoundingBox);
        this->page->fireElementChanged(ptr);
        undo->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, ptr));
    }
}

void TextEditor::initializeEditionAt(double x, double y) {
    // Is there already a textfield?
    Text* text = nullptr;
    std::shared_lock lock(*this->control->getDocument());

    // Should we reverse this loop to select the most recent text rather than the oldest?
    for (auto&& e: this->page->getSelectedLayer()->getElements()) {
        if (e->getType() == ELEMENT_TEXT && e->hasBoundingBoxContaining(x, y)) {
            text = dynamic_cast<Text*>(e.get());
            break;
        }
    }

    if (text == nullptr) {
        lock.unlock();
        ToolHandler* h = this->control->getToolHandler();
        this->textElement = std::make_unique<Text>();
        this->textElement->setColor(h->getColor());
        this->textElement->setFont(control->getSettings()->getFont());
        this->textElement->setOrigin(x, y - this->textElement->getBoundingBox().height / 2);
        this->textElement->setAlignment(h->getTextAlignment());
        this->textElement->setJustify(h->getTextJustify());

#ifdef ENABLE_AUDIO
        if (auto audioController = control->getAudioController(); audioController && audioController->isRecording()) {
            fs::path audioFilename = audioController->getAudioFilename();
            size_t sttime = audioController->getStartTime();
            size_t milliseconds = (as_unsigned(g_get_monotonic_time() / 1000) - sttime);
            this->textElement->setTimestamp(milliseconds);
            this->textElement->setAudioFilename(audioFilename);
        }
#endif
        this->originalTextElement = nullptr;
    } else {
        this->originalTextElement = text;
        this->textElement = text->cloneText();
        text->setInEditing(true);
        lock.unlock();

        auto* db = this->control->getActionDatabase();
        db->setActionState(Action::FONT, this->textElement->getFont().asString().c_str());
        db->setActionState(Action::TEXT_ALIGNMENT, this->textElement->getAlign());
        db->setActionState(Action::TEXT_JUSTIFY, this->textElement->getJustify());
        Color c = this->textElement->getColor();
        c.alpha = 0xff;
        db->setActionState(Action::TOOL_COLOR, c);

        this->page->fireElementChanged(text);
    }
    this->currentWrapWidth = this->textElement->getWrap();
    this->layout = this->textElement->createPangoLayout();
    this->replaceBufferContent(this->textElement->getText());
    // Editing styled text starts from its styling, so that it survives the round trip untouched
    xoj::text::applyRunsToBuffer(this->buffer.get(), this->textElement->getStyleRuns());
    this->previousBoundingBox = this->computeBoundingBox();
}
