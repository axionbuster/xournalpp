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

#include <fstream>

#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "util/Color.h"

/// A settings file that was never written by a version knowing about presets has no slots set
TEST(FontPresetSettingsTest, testUnsetByDefault) {
    Settings settings{"xournalpp-test-units_FontPresets_unused.xml"};

    for (size_t i = 0; i < FONT_PRESET_COUNT; i++) {
        EXPECT_FALSE(settings.getFontPreset(i).has_value());
    }
}

/// Font, size and color of every slot survive a save/load round trip; untouched slots stay unset
TEST(FontPresetSettingsTest, testReadWrite) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_FontPresets_testReadWrite.xml";
    if (fs::exists(outPath)) {
        fs::remove(outPath);
    }

    Settings settings(outPath);
    settings.transactionStart();
    settings.setFontPreset(0, FontPreset{XojFont{"myfontname Bold", 24}, Color(0xff123456U)});
    settings.setFontPreset(2, FontPreset{XojFont{"myfontname Bold Italic", 13.5}, Color(0xffabcdefU)});
    settings.transactionEnd();  // calls save()

    Settings loaded(outPath);
    loaded.load();

    ASSERT_TRUE(loaded.getFontPreset(0).has_value());
    EXPECT_EQ("myfontname Bold", loaded.getFontPreset(0)->font.getName());
    EXPECT_EQ(24, loaded.getFontPreset(0)->font.getSize());
    EXPECT_EQ(Color(0xff123456U), loaded.getFontPreset(0)->color);

    EXPECT_FALSE(loaded.getFontPreset(1).has_value());

    ASSERT_TRUE(loaded.getFontPreset(2).has_value());
    EXPECT_EQ("myfontname Bold Italic", loaded.getFontPreset(2)->font.getName());
    EXPECT_EQ(13.5, loaded.getFontPreset(2)->font.getSize());
    EXPECT_EQ(Color(0xffabcdefU), loaded.getFontPreset(2)->color);

    EXPECT_FALSE(loaded.getFontPreset(3).has_value());

    fs::remove(outPath);
}

/**
 * A preset's color ends up on document content, so a value the parser cannot read must not turn
 * into Color(0) -- that is fully transparent, and would make the text disappear instead of
 * leaving it black.
 */
TEST(FontPresetSettingsTest, testUnreadableColorStaysVisible) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_FontPresets_badColor.xml";
    {
        std::ofstream out(outPath);
        out << "<?xml version=\"1.0\" standalone=\"no\"?>\n"
               "<settings>\n"
               "  <property name=\"fontPreset1\" font=\"myfontname Bold\" size=\"24\" color=\"#ff0000\"/>\n"
               "  <property name=\"fontPreset2\" font=\"myfontname\" size=\"11\" color=\"255\"/>\n"
               "</settings>\n";
    }

    Settings settings(outPath);
    settings.load();

    ASSERT_TRUE(settings.getFontPreset(0).has_value());
    EXPECT_EQ("myfontname Bold", settings.getFontPreset(0)->font.getName());
    // "#ff0000" is not a decimal number, so the color keeps its opaque default
    EXPECT_EQ(Colors::black, settings.getFontPreset(0)->color);

    ASSERT_TRUE(settings.getFontPreset(1).has_value());
    // A readable value without an alpha channel is forced opaque rather than left transparent
    EXPECT_EQ(0xffU, settings.getFontPreset(1)->color.alpha);
    EXPECT_EQ(Color(0xff0000ffU), settings.getFontPreset(1)->color);

    fs::remove(outPath);
}
