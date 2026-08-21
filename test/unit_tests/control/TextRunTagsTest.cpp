/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <string>

#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "control/tools/TextRunTags.h"
#include "model/TextStyleRuns.h"
#include "util/Color.h"

namespace {

/*
 * A GtkTextBuffer is a plain GObject: it holds text and tags, and needs no display, no toolkit
 * initialization and no widget. That is what makes the tag/run conversion — the piece of the
 * text editor that decides what styling gets committed — testable on its own.
 */
struct Buffer {
    Buffer(const std::string& text) {
        /*
         * GtkTextTag declares properties of types that GTK would normally have registered while
         * initializing itself. Nothing here needs a display, so register just those types rather
         * than initialize a toolkit; otherwise GObject complains about every one of them the
         * first time a tag class is created.
         */
        g_type_ensure(GDK_TYPE_RGBA);
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        g_type_ensure(GDK_TYPE_COLOR);  // GtkTextTag still carries its deprecated GdkColor properties
        G_GNUC_END_IGNORE_DEPRECATIONS
        g_type_ensure(PANGO_TYPE_FONT_DESCRIPTION);
        g_type_ensure(PANGO_TYPE_TAB_ARRAY);

        buffer = gtk_text_buffer_new(nullptr);
        gtk_text_buffer_set_text(buffer, text.c_str(), -1);
    }
    ~Buffer() { g_object_unref(buffer); }

    GtkTextIter iterAt(int byteOffset) { return xoj::text::getIteratorAtByteOffset(buffer, byteOffset); }

    GtkTextBuffer* buffer;
};

/// `bold` and `italic` here mean "forced on"; a run forcing one off is built by hand
TextStyleRun run(size_t start, size_t end, bool bold, bool italic, std::optional<Color> color = std::nullopt) {
    TextStyleRun r;
    r.start = start;
    r.end = end;
    if (bold) {
        r.bold = true;
    }
    if (italic) {
        r.italic = true;
    }
    r.color = color;
    return r;
}

TEST(ControlTextRunTags, anUntaggedBufferHasNoRuns) {
    Buffer b("plain text");
    EXPECT_TRUE(xoj::text::runsFromBuffer(b.buffer).empty());
}

TEST(ControlTextRunTags, runsRoundTripThroughTags) {
    const TextStyleRuns runs = {run(0, 5, false, true), run(6, 10, true, false, Colors::red)};

    Buffer b("Solve 3x for x");
    xoj::text::applyRunsToBuffer(b.buffer, runs);

    const TextStyleRuns back = xoj::text::runsFromBuffer(b.buffer);
    ASSERT_EQ(runs.size(), back.size());
    for (size_t i = 0; i < runs.size(); i++) {
        EXPECT_EQ(runs[i], back[i]) << "run " << i;
        EXPECT_EQ(runs[i].color, back[i].color) << "run " << i;
    }
}

TEST(ControlTextRunTags, multibyteOffsetsAreBytesNotCharacters) {
    // "测" is three bytes, so a run over the second character starts at byte 3
    Buffer b("测试x");
    const TextStyleRuns runs = {run(3, 6, true, false)};
    xoj::text::applyRunsToBuffer(b.buffer, runs);

    const TextStyleRuns back = xoj::text::runsFromBuffer(b.buffer);
    ASSERT_EQ(size_t(1), back.size());
    EXPECT_EQ(runs[0], back[0]);
}

TEST(ControlTextRunTags, adjacentIdenticalRunsComeBackMerged) {
    Buffer b("abcdef");
    xoj::text::applyRunsToBuffer(b.buffer, {run(0, 3, true, false), run(3, 6, true, false)});

    const TextStyleRuns back = xoj::text::runsFromBuffer(b.buffer);
    ASSERT_EQ(size_t(1), back.size());
    EXPECT_EQ(run(0, 6, true, false), back[0]);
}

TEST(ControlTextRunTags, applyingAStyleReplacesWhateverWasThere) {
    Buffer b("abcdef");
    xoj::text::applyRunsToBuffer(b.buffer, {run(0, 6, true, true, Colors::red)});

    GtkTextIter start = b.iterAt(2);
    GtkTextIter end = b.iterAt(4);
    xoj::text::applyStyle(b.buffer, &start, &end, xoj::text::InlineStyle{std::nullopt, true, std::nullopt});

    const TextStyleRuns back = xoj::text::runsFromBuffer(b.buffer);
    ASSERT_EQ(size_t(3), back.size());
    EXPECT_EQ(run(0, 2, true, true, Colors::red), back[0]);
    EXPECT_EQ(run(2, 4, false, true), back[1]);
    EXPECT_FALSE(back[1].color.has_value());
    EXPECT_EQ(run(4, 6, true, true, Colors::red), back[2]);
}

TEST(ControlTextRunTags, styleIsReadFromTheCharacterLeftOfAnIterator) {
    Buffer b("abcdef");
    xoj::text::applyRunsToBuffer(b.buffer, {run(0, 3, true, false)});

    const GtkTextIter start = b.iterAt(0);
    const GtkTextIter insideBold = b.iterAt(3);
    const GtkTextIter afterBold = b.iterAt(4);

    // Typing right after the bold stretch continues it; one character further does not
    EXPECT_TRUE(xoj::text::styleLeftOf(&insideBold).bold.value_or(false));
    EXPECT_FALSE(xoj::text::styleLeftOf(&afterBold).bold.value_or(false));
    // With nothing to the left, the character to the right decides, so that typing at the very
    // start of a styled text does not produce the one unstyled character in it
    EXPECT_TRUE(xoj::text::styleLeftOf(&start).bold.value_or(false));
}

TEST(ControlTextRunTags, aFullyBoldRangeIsRecognized) {
    Buffer b("abcdef");
    xoj::text::applyRunsToBuffer(b.buffer, {run(1, 4, true, false)});

    GtkTextIter one = b.iterAt(1);
    GtkTextIter three = b.iterAt(3);
    GtkTextIter four = b.iterAt(4);
    GtkTextIter five = b.iterAt(5);

    constexpr auto which = &xoj::text::InlineStyle::bold;
    EXPECT_TRUE(xoj::text::rangeIsFullyStyled(which, false, &one, &four));
    EXPECT_TRUE(xoj::text::rangeIsFullyStyled(which, false, &one, &three));
    EXPECT_FALSE(xoj::text::rangeIsFullyStyled(which, false, &one, &five));
    EXPECT_FALSE(xoj::text::rangeIsFullyStyled(which, false, &four, &five));
}

TEST(ControlTextRunTags, anElementsOwnFontCountsTowardsWhatIsBold) {
    /*
     * Ctrl+B has to mean something in a text element whose font is already "Arial Bold": the
     * range is bold there without carrying any run, and pressing Ctrl+B has to be able to take
     * the weight back to normal rather than adding a bold on top of a bold.
     */
    Buffer b("abcdef");
    TextStyleRun notBold;
    notBold.start = 2;
    notBold.end = 4;
    notBold.bold = false;
    xoj::text::applyRunsToBuffer(b.buffer, {notBold});

    GtkTextIter zero = b.iterAt(0);
    GtkTextIter two = b.iterAt(2);
    GtkTextIter four = b.iterAt(4);
    GtkTextIter six = b.iterAt(6);

    constexpr auto which = &xoj::text::InlineStyle::bold;
    // The untagged stretches follow the element's font; the tagged one overrides it
    EXPECT_TRUE(xoj::text::rangeIsFullyStyled(which, true, &zero, &two));
    EXPECT_FALSE(xoj::text::rangeIsFullyStyled(which, true, &two, &four));
    EXPECT_FALSE(xoj::text::rangeIsFullyStyled(which, true, &zero, &six));
    EXPECT_TRUE(xoj::text::rangeIsFullyStyled(which, true, &four, &six));

    // ... and the override survives the trip back out to a run list
    const TextStyleRuns back = xoj::text::runsFromBuffer(b.buffer);
    ASSERT_EQ(size_t(1), back.size());
    ASSERT_TRUE(back[0].bold.has_value());
    EXPECT_FALSE(*back[0].bold);
}

TEST(ControlTextRunTags, aRangeReportsWhetherItCarriesAnyStyling) {
    // What tells a styled paste from a plain one, so that copied styling is not merged with the
    // styling of wherever it lands
    Buffer b("abcdef");
    xoj::text::applyRunsToBuffer(b.buffer, {run(2, 4, false, true)});

    GtkTextIter zero = b.iterAt(0);
    GtkTextIter two = b.iterAt(2);
    GtkTextIter four = b.iterAt(4);
    GtkTextIter six = b.iterAt(6);

    EXPECT_TRUE(xoj::text::rangeCarriesStyle(&zero, &six));
    EXPECT_TRUE(xoj::text::rangeCarriesStyle(&two, &four));
    EXPECT_FALSE(xoj::text::rangeCarriesStyle(&zero, &two));
    EXPECT_FALSE(xoj::text::rangeCarriesStyle(&four, &six));
}

TEST(ControlTextRunTags, anOffsetInsideACharacterNeverReachesTheBuffer) {
    /*
     * GTK does not merely misbehave on an iterator pointing into the middle of a UTF-8
     * character: it warns that this "will crash the text buffer" and then aborts the process on
     * the next tagging. A run list out of a hand-edited document can name such an offset, so the
     * iterator is snapped back to the start of the character it landed in.
     */
    Buffer b("a测试b");  // 1 + 3 + 3 + 1 bytes

    for (const int inside: {2, 3, 5, 6}) {
        GtkTextIter it = b.iterAt(inside);
        EXPECT_TRUE(gtk_text_iter_is_cursor_position(&it)) << "byte " << inside;
    }
    // The snapped offsets are the character starts at or before each one
    EXPECT_EQ(1, xoj::text::getByteOffsetOfIterator(b.iterAt(2)));
    EXPECT_EQ(1, xoj::text::getByteOffsetOfIterator(b.iterAt(3)));
    EXPECT_EQ(4, xoj::text::getByteOffsetOfIterator(b.iterAt(5)));
    EXPECT_EQ(4, xoj::text::getByteOffsetOfIterator(b.iterAt(6)));
    // ... and an offset that is already on a boundary is left where it is
    for (const int boundary: {0, 1, 4, 7, 8}) {
        EXPECT_EQ(boundary, xoj::text::getByteOffsetOfIterator(b.iterAt(boundary))) << "byte " << boundary;
    }
}

TEST(ControlTextRunTags, taggingSurvivesOffsetsInsideACharacter) {
    // The abort this fork used to be one double-click away from: applying a run whose offsets
    // split a character. Nothing here may crash, and the styling lands on whole characters.
    Buffer b("a测试b");
    xoj::text::applyRunsToBuffer(b.buffer, {run(2, 6, true, false)});

    const TextStyleRuns back = xoj::text::runsFromBuffer(b.buffer);
    ASSERT_EQ(size_t(1), back.size());
    EXPECT_EQ(size_t(1), back[0].start);
    EXPECT_EQ(size_t(4), back[0].end);
}

}  // namespace
