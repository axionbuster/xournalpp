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

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "model/TextStyleRuns.h"
#include "util/Color.h"

namespace {

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

TEST(ModelTextStyleRuns, emptyRunsSerializeToNothing) {
    EXPECT_EQ("", xoj::text::serializeStyleRuns({}));

    // ... and an absent or empty attribute means unstyled text, not a malformed one
    const auto parsed = xoj::text::parseStyleRuns("");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->empty());
}

TEST(ModelTextStyleRuns, everyStyleRoundTrips) {
    const TextStyleRuns runs = {run(0, 5, false, true), run(7, 12, true, false, Colors::red),
                                run(20, 21, true, true, Color(0xff0000ffU))};

    const std::string serialized = xoj::text::serializeStyleRuns(runs);
    EXPECT_EQ("0-5:i;7-12:bc#ff0000ff;20-21:bic#0000ffff", serialized);

    const auto parsed = xoj::text::parseStyleRuns(serialized);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(runs.size(), parsed->size());
    for (size_t i = 0; i < runs.size(); i++) {
        EXPECT_EQ(runs[i], (*parsed)[i]) << "run " << i;
        EXPECT_EQ(runs[i].color, (*parsed)[i].color) << "run " << i;
    }
}

TEST(ModelTextStyleRuns, colorAlphaSurvivesTheRoundTrip) {
    const TextStyleRuns runs = {run(0, 1, false, false, Color(0x80123456U))};

    const auto parsed = xoj::text::parseStyleRuns(xoj::text::serializeStyleRuns(runs));
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(size_t(1), parsed->size());
    ASSERT_TRUE((*parsed)[0].color.has_value());
    EXPECT_EQ(Color(0x80123456U), *(*parsed)[0].color);
}

TEST(ModelTextStyleRuns, malformedAttributesAreRejectedWholesale) {
    const char* const malformed[] = {
            "0-5",               // no styling
            "0-5:",              // empty styling
            "0-5:x",             // unknown flag
            "5-5:b",             // empty range
            "6-5:b",             // backwards range
            "0-5:b;3-8:i",       // overlapping
            "7-12:b;0-5:i",      // out of order
            "0-5:bb",            // repeated flag
            "0-5:bB",            // bold both forced on and forced off
            "0-5:Ii",            // italic both forced off and forced on
            "0-5:c#ff00",        // short color code
            "0-5:c#ff0000ffff",  // long color code
            "0-5:c#gggggggg",    // not hexadecimal
            "0-5:cff0000ff",     // color without the '#'
            "0-5:ic#ff0000ff;",  // trailing separator
            "0-5:i;;7-12:b",     // empty item
            "-5:i",              // no start
            "0-:i",              // no end
            "0:5:i",             // wrong separator
            "0-5i",              // no ':'
    };

    for (const char* attribute: malformed) {
        EXPECT_FALSE(xoj::text::parseStyleRuns(attribute).has_value()) << "accepted \"" << attribute << "\"";
    }
}

TEST(ModelTextStyleRuns, normalizationGivesOneRepresentationPerStyling) {
    TextStyleRuns runs = {run(10, 20, true, false), run(0, 5, false, true), run(5, 10, true, false),
                          run(30, 30, true, true), run(40, 50, false, false)};

    xoj::text::normalizeStyleRuns(runs);

    // Sorted, the empty and the unstyled runs dropped, and the two adjacent bold ones merged
    ASSERT_EQ(size_t(2), runs.size());
    EXPECT_EQ(run(0, 5, false, true), runs[0]);
    EXPECT_EQ(run(5, 20, true, false), runs[1]);
}

TEST(ModelTextStyleRuns, adjacentRunsOfDifferentStylesStaySeparate) {
    TextStyleRuns runs = {run(0, 5, true, false), run(5, 10, true, false, Colors::red)};
    xoj::text::normalizeStyleRuns(runs);
    EXPECT_EQ(size_t(2), runs.size());
}

TEST(ModelTextStyleRuns, aRunCanTakeBoldAndItalicAwayFromTheElementsOwnFont) {
    /*
     * An element whose font is "Arial Bold" is bold everywhere a run does not say otherwise, so
     * a run has to be able to say otherwise -- a flag that could only ever add would leave
     * Ctrl+B with nothing to do in exactly the documents this fork's font presets produce.
     */
    TextStyleRun forcedOff;
    forcedOff.start = 0;
    forcedOff.end = 5;
    forcedOff.bold = false;
    forcedOff.italic = false;

    TextStyleRun mixed;
    mixed.start = 7;
    mixed.end = 12;
    mixed.bold = true;
    mixed.italic = false;

    const std::string serialized = xoj::text::serializeStyleRuns({forcedOff, mixed});
    EXPECT_EQ("0-5:BI;7-12:bI", serialized);

    const auto parsed = xoj::text::parseStyleRuns(serialized);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(size_t(2), parsed->size());
    EXPECT_EQ(forcedOff, (*parsed)[0]);
    EXPECT_EQ(mixed, (*parsed)[1]);

    // A run that only forces things off is still a run: it says something the element does not
    EXPECT_FALSE(forcedOff.isPlain());
}

TEST(ModelTextStyleRuns, offsetsAreAlignedToCharacterBoundaries) {
    /*
     * A hand-edited document can put an offset in the middle of a multi-byte character. Pango
     * tolerates it, but the editor's GtkTextBuffer aborts the process over it, so the offsets are
     * moved inwards to the boundaries of the characters the run fully covers.
     */
    const std::string text = "ab测试cd";  // bytes: a(0) b(1) 测(2-4) 试(5-7) c(8) d(9)

    // A start inside a character moves forward to the next one; an end inside it moves back
    TextStyleRuns runs = {run(3, 9, true, false)};
    xoj::text::alignStyleRunsToText(runs, text);
    ASSERT_EQ(size_t(1), runs.size());
    EXPECT_EQ(run(5, 9, true, false), runs[0]);

    // A run covering no whole character is dropped rather than kept as an empty one
    for (const auto& [start, end]: {std::pair<size_t, size_t>{3, 7}, std::pair<size_t, size_t>{3, 4}}) {
        TextStyleRuns tiny = {run(start, end, true, false)};
        xoj::text::alignStyleRunsToText(tiny, text);
        EXPECT_TRUE(tiny.empty()) << "run " << start << "-" << end;
    }

    // Runs already on boundaries, and runs past the end, behave as clamping alone would
    TextStyleRuns fine = {run(0, 2, false, true), run(8, 40, true, false)};
    xoj::text::alignStyleRunsToText(fine, text);
    ASSERT_EQ(size_t(2), fine.size());
    EXPECT_EQ(run(0, 2, false, true), fine[0]);
    EXPECT_EQ(run(8, 10, true, false), fine[1]);
}

using Ranges = std::vector<std::pair<unsigned int, unsigned int>>;

/// The byte ranges the attributes in the list cover, in the order Pango hands them back
Ranges attributeRanges(PangoAttrList* attrs) {
    Ranges ranges;
    pango_attr_list_filter(
            attrs,
            [](PangoAttribute* attribute, gpointer data) -> gboolean {
                static_cast<Ranges*>(data)->emplace_back(attribute->start_index, attribute->end_index);
                return FALSE;  // keep it in the list
            },
            &ranges);
    return ranges;
}

TEST(ModelTextStyleRuns, aCompositionIsPreviewedWithTheStyleItWillCommitWith) {
    /*
     * While an Input Method composition is on screen it is not part of the text, so the runs are
     * shifted around it. What the composition itself is drawn with has to match what the text
     * will look like once it commits, or the glyphs change weight the moment composing ends.
     */
    const size_t at = 4;
    const size_t length = 3;

    {  // Composing right after a bold stretch: the composition is drawn bold, as it will commit
        PangoAttrList* attrs = pango_attr_list_new();
        const TextStyleRuns runs = {run(0, 4, true, false)};
        xoj::text::appendStyleRunAttributes(attrs, runs, at, length);
        EXPECT_EQ(Ranges({{0, 7}}), attributeRanges(attrs));
        pango_attr_list_unref(attrs);
    }

    {  // Composing right before one: the bold moves aside rather than swallowing the composition
        PangoAttrList* attrs = pango_attr_list_new();
        const TextStyleRuns runs = {run(4, 8, true, false)};
        xoj::text::appendStyleRunAttributes(attrs, runs, at, length);
        EXPECT_EQ(Ranges({{7, 11}}), attributeRanges(attrs));
        pango_attr_list_unref(attrs);
    }

    {  // A pending Ctrl+B decides instead, and the run it interrupts is split around it
        PangoAttrList* attrs = pango_attr_list_new();
        const TextStyleRuns runs = {run(2, 8, false, true)};
        const TextStyleRun composing = run(0, 0, true, false);
        xoj::text::appendStyleRunAttributes(attrs, runs, at, length, &composing);
        EXPECT_EQ(Ranges({{2, 4}, {4, 7}, {7, 11}}), attributeRanges(attrs));
        pango_attr_list_unref(attrs);
    }
}

TEST(ModelTextStyleRuns, clampingCutsRunsBackToTheText) {
    TextStyleRuns runs = {run(0, 5, true, false), run(8, 20, false, true), run(30, 40, true, true)};

    xoj::text::clampStyleRuns(runs, 12);

    ASSERT_EQ(size_t(2), runs.size());
    EXPECT_EQ(run(0, 5, true, false), runs[0]);
    EXPECT_EQ(run(8, 12, false, true), runs[1]);
}

}  // namespace
