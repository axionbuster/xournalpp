/*
 * Xournal++
 *
 * Inline style runs inside a text element
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>      // for size_t
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <pango/pango.h>  // for PangoAttrList

#include "util/Color.h"  // for Color

/**
 * @brief A stretch of a text element's bytes that is styled differently from the element itself.
 *
 * The offsets are byte offsets into the element's UTF-8 text, `start` inclusive and `end`
 * exclusive, and they always fall on character boundaries — xoj::text::alignStyleRunsToText() is
 * what makes that true of a run list coming from outside.
 *
 * `bold` and `italic` each *override* what the element's own font says: nothing means the font
 * decides, `true` forces the weight or the slant on, and `false` forces it off. The distinction
 * is what lets a range of an "Arial Bold" element be un-bolded, which a flag that could only ever
 * add would not express. The color, when set, replaces the element's color for those bytes.
 */
struct TextStyleRun {
    size_t start = 0;
    size_t end = 0;
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<Color> color;

    /// Does this run leave the element's own style alone? Such a run is never stored.
    bool isPlain() const { return !bold.has_value() && !italic.has_value() && !color.has_value(); }

    /// Same styling, whatever the two runs cover
    bool sameStyleAs(const TextStyleRun& o) const { return bold == o.bold && italic == o.italic && color == o.color; }

    bool operator==(const TextStyleRun& o) const { return start == o.start && end == o.end && sameStyleAs(o); }
    bool operator!=(const TextStyleRun& o) const { return !(*this == o); }
};

/**
 * @brief The styled stretches of a text element, ordered and non-overlapping.
 *
 * An empty list means the element is styled exactly as a stock Xournal++ text element is: one
 * font, one color, no exceptions. Every code path treats the empty list as "nothing to do", so
 * unstyled text keeps behaving — and serializing — exactly as it always has.
 */
using TextStyleRuns = std::vector<TextStyleRun>;

namespace xoj::text {

/**
 * @brief Serialize runs into the value of the fork-only "runs" attribute of a <text> element.
 *
 * The grammar is
 *
 *     runs  := run (";" run)*
 *     run   := start "-" end ":" flags
 *     flags := ("b" | "B")? ("i" | "I")? color?
 *     color := "c#" 8 hexadecimal digits, RRGGBBAA
 *
 * where `start` and `end` are decimal byte offsets into the element's text, `end` strictly
 * greater than `start`, and the runs appear in increasing order without overlapping. `b` forces
 * bold and `B` forces the weight back to normal; `i` and `I` do the same for the slant; and the
 * color replaces the element's color over those bytes. A flag that is absent leaves the
 * element's own font in charge. A run always carries at least one of the three, since a run
 * styled exactly like its element is dropped rather than written. For example
 *
 *     runs="0-5:i;7-12:bc#ff0000ff"
 *
 * italicizes the first five bytes and makes bytes 7 through 11 bold and red, and
 *
 *     runs="0-5:B"
 *
 * takes the weight of the first five bytes of a bold element back to normal.
 *
 * The offsets are offsets into the text as it is saved. The text node round-trips
 * byte-identically — the writer escapes `&`, `<`, `>` and the carriage return that XML parsers
 * would otherwise fold into a line feed, and the parser hands the text back unescaped — so the
 * offsets a document is saved with are the offsets it is loaded with.
 */
std::string serializeStyleRuns(const TextStyleRuns& runs);

/**
 * @brief Parse the "runs" attribute, or nothing if it does not follow the grammar.
 *
 * Anything the serializer above could not have produced is rejected wholesale rather than
 * partially honored: a caller that cannot make sense of the styling is better off showing the
 * text plainly than showing it with half its runs.
 *
 * @note The grammar says nothing about the text the offsets point into, which this never sees.
 *       Whether they are in range and land on character boundaries is decided by
 *       alignStyleRunsToText(), which every path that pairs runs with a text goes through.
 */
std::optional<TextStyleRuns> parseStyleRuns(std::string_view attribute);

/**
 * @brief Put a run list into the canonical form the rest of the code assumes.
 *
 * Sorts by offset, drops empty and unstyled runs, and merges neighbors that carry the same
 * style, so that a given styling has exactly one representation.
 *
 * @note Overlapping runs are not resolved; the producers here never build any.
 */
void normalizeStyleRuns(TextStyleRuns& runs);

/// Drop and shorten the runs that reach past the end of a text of `textLength` bytes
void clampStyleRuns(TextStyleRuns& runs, size_t textLength);

/**
 * @brief Make a run list safe to use with `text`: in range, and on character boundaries.
 *
 * Runs reaching past the end of the text are cut back as clampStyleRuns() does, and an offset
 * landing inside a multi-byte character is moved to the boundary *inside* the run, so a run only
 * ever covers whole characters. A run left empty by either is dropped.
 *
 * A file written by this fork never needs any of this. A hand-edited one does, and a
 * mid-character offset is not a cosmetic problem: GTK aborts the process the moment a text
 * buffer is asked for an iterator inside a character, which is what happens as soon as such an
 * element is opened for editing.
 */
void alignStyleRunsToText(TextStyleRuns& runs, std::string_view text);

/**
 * @brief Add the Pango attributes describing `runs` to `attrs`.
 *
 * Bold and italic override the layout's font description where a run says so and leave it alone
 * where the run does not, so a range of a bold element can be drawn bold, non-bold, or however
 * the element's font has it. The color attribute has no alpha channel, which matches how a text
 * element's own color is drawn: Xournal++ draws text opaque and ignores the color's alpha, so a
 * run's color is drawn opaque too.
 *
 * `insertLength`, when non-zero, describes a string of that many bytes inserted into the text at
 * byte `insertPosition` without being part of it — the Input Method preedit string. The
 * attributes are shifted around it. What the composition itself is drawn with follows
 * `insertStyle`: when it is null the run covering the insertion point is stretched over the
 * composition, which is the styling the committed text will inherit from its left; when it is
 * given — a Ctrl+B pressed just before composing, say — the runs go around the composition and
 * it is drawn with that style instead. Either way the preview matches what commits.
 */
void appendStyleRunAttributes(PangoAttrList* attrs, const TextStyleRuns& runs, size_t insertPosition = 0,
                              size_t insertLength = 0, const TextStyleRun* insertStyle = nullptr);

}  // namespace xoj::text
