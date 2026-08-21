/*
 * Xournal++
 *
 * Inline text styling held as tags on the text editor's GtkTextBuffer
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>  // for optional

#include <gtk/gtk.h>  // for GtkTextBuffer, GtkTextIter, GtkTextTag

#include "model/TextStyleRuns.h"  // for TextStyleRuns
#include "util/Color.h"           // for Color

namespace xoj::text {

/**
 * @brief The styling of one stretch of text, without saying which stretch.
 *
 * This is what a text element's run list holds per run, and what the editor tracks while typing.
 * `bold` and `italic` are the same tri-state as TextStyleRun's: nothing leaves the edited
 * element's own font in charge, and a value overrides it either way.
 */
struct InlineStyle {
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<Color> color;  ///< overrides the edited element's color

    bool isPlain() const { return !bold.has_value() && !italic.has_value() && !color.has_value(); }
    bool operator==(const InlineStyle& o) const { return bold == o.bold && italic == o.italic && color == o.color; }
    bool operator!=(const InlineStyle& o) const { return !(*this == o); }
};

/**
 * @brief Compute the byte offset of an iterator in its GtkTextBuffer
 *
 * NB: This is much faster than relying on g_utf8_offset_to_pointer
 */
int getByteOffsetOfIterator(GtkTextIter it);

/**
 * @brief Get an iterator at the prescribed byte index.
 *
 * An index that falls inside a multi-byte character is moved back to the start of it: GTK aborts
 * the process rather than tolerating such an iterator, and the index can come from a run list
 * that a hand-edited document put there.
 *
 * NB: This is much faster than relying on g_utf8_pointer_to_offset for long texts
 */
GtkTextIter getIteratorAtByteOffset(GtkTextBuffer* buf, int byteIndex);

/**
 * @brief The tags standing for bold, italic and a color override.
 *
 * The tags carry no visual properties: nothing ever renders the buffer through a GtkTextView.
 * They are markers, and GTK maintains their ranges across insertions and deletions for free,
 * which is the entire reason the editor tracks styling with them rather than with offsets of its
 * own. They are created in the buffer's tag table the first time they are asked for.
 *
 * Bold and italic each need two tags, because "bold here" and "not bold here" are both things a
 * range can say that the element's own font does not.
 */
GtkTextTag* boldTag(GtkTextBuffer* buffer, bool bold);
GtkTextTag* italicTag(GtkTextBuffer* buffer, bool italic);
GtkTextTag* colorTag(GtkTextBuffer* buffer, Color color);

/// The style of the character the iterator points at
InlineStyle styleAt(const GtkTextIter* it);

/**
 * @brief The style the text typed at this iterator takes on.
 *
 * That is the style of the character to the left, which is what makes typing continue whatever
 * it is typed behind. At the very start of the buffer, where there is nothing to the left, it is
 * the style of the character to the right instead — otherwise the first character of a fully
 * styled text would be the one place where typing came out unstyled.
 */
InlineStyle styleLeftOf(const GtkTextIter* it);

/// Remove every styling tag from the range, whatever it was
void clearStyle(GtkTextBuffer* buffer, const GtkTextIter* start, const GtkTextIter* end);

/// Give the range exactly this style, dropping whatever styling it had
void applyStyle(GtkTextBuffer* buffer, const GtkTextIter* start, const GtkTextIter* end, const InlineStyle& style);

/**
 * @brief Is every character of the range bold (or italic), counting the element's own font?
 *
 * `which` picks the flag and `baseState` says what the edited element's font description has to
 * say about it, which is what a range with no tag of its own inherits. This is what Ctrl+B and
 * Ctrl+I toggle against, so that they mean the same thing whether the weight comes from the
 * element's font or from a run.
 */
bool rangeIsFullyStyled(std::optional<bool> InlineStyle::* which, bool baseState, const GtkTextIter* start,
                        const GtkTextIter* end);

/// Does any character of the range carry inline styling of its own?
bool rangeCarriesStyle(const GtkTextIter* start, const GtkTextIter* end);

/**
 * @brief Read the buffer's tags back into a run list, in the canonical form.
 *
 * This and applyRunsToBuffer() are inverses of each other, up to the normalization every run
 * list goes through: they are what carries styling between an element and an edition session.
 */
TextStyleRuns runsFromBuffer(GtkTextBuffer* buffer);

/// Tag the buffer's text according to a run list
void applyRunsToBuffer(GtkTextBuffer* buffer, const TextStyleRuns& runs);

}  // namespace xoj::text
