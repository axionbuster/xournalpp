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

#include <memory>
#include <string>

#include <config-test.h>
#include <gtest/gtest.h>

#include "control/xojfile/LoadHandler.h"
#include "control/xojfile/SaveHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageRef.h"
#include "model/Text.h"
#include "model/TextStyleRuns.h"
#include "model/XojPage.h"
#include "util/PathUtil.h"

#include "config.h"  // for FILE_FORMAT_VERSION
#include "filesystem.h"

namespace {

constexpr double PAGE_WIDTH = 300.0;
constexpr double PAGE_HEIGHT = 400.0;

/// Text with an ampersand and a multi-byte character, so escaping and UTF-8 are both exercised
const std::string TEXT = "Solve 3x & 4y for 测试";

/// A document holding one page, one layer, and one text element
std::unique_ptr<Document> makeDocumentWithOneText(const TextStyleRuns& runs) {
    auto doc = std::make_unique<Document>(nullptr);
    auto page = std::make_shared<XojPage>(PAGE_WIDTH, PAGE_HEIGHT);

    auto text = std::make_unique<Text>();
    text->setText(TEXT);
    text->setOrigin(20.0, 30.0);
    text->setColor(Colors::black);
    text->setStyleRuns(runs);
    page->getLayers()[0]->addElement(std::move(text));

    doc->addPage(page);
    return doc;
}

/// Save the document and load it back, so that the whole XML round trip is exercised
std::unique_ptr<Document> saveAndReload(const Document* doc, const std::string& name) {
    const auto tmp = Util::getTmpDirSubfolder() / name;
    SaveHandler handler;
    handler.prepareSave(doc, tmp);
    handler.saveTo(tmp);
    EXPECT_EQ("", handler.getErrorMessage());

    std::unique_ptr<Document> reloaded;
    EXPECT_NO_THROW(reloaded = LoadHandler{}.loadDocument(tmp));
    return reloaded;
}

const Text* firstText(const Document* doc) {
    EXPECT_EQ(size_t(1), doc->getPageCount());
    ConstPageRef page = doc->getPage(0);
    EXPECT_EQ(size_t(1), page->getLayerCount());
    const Layer* layer = page->getLayersView()[0];
    EXPECT_EQ(size_t(1), layer->getElementsView().size());
    return dynamic_cast<const Text*>(layer->getElementsView()[0]);
}

TextStyleRuns someRuns() {
    TextStyleRun italic;
    italic.start = 6;
    italic.end = 7;
    italic.italic = true;

    TextStyleRun boldRed;
    boldRed.start = 11;
    boldRed.end = 12;
    boldRed.bold = true;
    boldRed.color = Colors::red;

    return {italic, boldRed};
}

TEST(ControlSaveHandlerTextRuns, unstyledTextDeclaresTheStockVersion) {
    const auto doc = makeDocumentWithOneText({});

    EXPECT_FALSE(SaveHandler::hasForkFormatExtensions(doc.get()));
    EXPECT_EQ(SaveHandler::STOCK_FILE_FORMAT_VERSION, SaveHandler::fileFormatVersion(doc.get()));
}

TEST(ControlSaveHandlerTextRuns, styledTextDeclaresTheForkVersion) {
    const auto doc = makeDocumentWithOneText(someRuns());

    EXPECT_TRUE(SaveHandler::hasForkFormatExtensions(doc.get()));
    EXPECT_EQ(FILE_FORMAT_VERSION, SaveHandler::fileFormatVersion(doc.get()));
}

TEST(ControlSaveHandlerTextRuns, unstyledTextRoundTripsWithoutRuns) {
    const auto doc = makeDocumentWithOneText({});
    const auto reloaded = saveAndReload(doc.get(), "textruns-plain.xopp");
    ASSERT_TRUE(reloaded);

    const Text* text = firstText(reloaded.get());
    ASSERT_NE(nullptr, text);
    EXPECT_EQ(TEXT, text->getText());
    EXPECT_TRUE(text->getStyleRuns().empty());
}

TEST(ControlSaveHandlerTextRuns, styledTextRoundTrips) {
    const auto runs = someRuns();
    const auto doc = makeDocumentWithOneText(runs);
    const auto reloaded = saveAndReload(doc.get(), "textruns-styled.xopp");
    ASSERT_TRUE(reloaded);

    const Text* text = firstText(reloaded.get());
    ASSERT_NE(nullptr, text);
    // The byte offsets only mean anything if the text itself comes back byte for byte
    EXPECT_EQ(TEXT, text->getText());
    ASSERT_EQ(runs.size(), text->getStyleRuns().size());
    for (size_t i = 0; i < runs.size(); i++) {
        EXPECT_EQ(runs[i], text->getStyleRuns()[i]) << "run " << i;
        EXPECT_EQ(runs[i].color, text->getStyleRuns()[i].color) << "run " << i;
    }
}

TEST(ControlSaveHandlerTextRuns, runsPastTheEndOfTheTextAreCutBack) {
    TextStyleRun run;
    run.start = 3;
    run.end = 5000;
    run.bold = true;

    const auto doc = makeDocumentWithOneText({run});
    const auto reloaded = saveAndReload(doc.get(), "textruns-overlong.xopp");
    ASSERT_TRUE(reloaded);

    const Text* text = firstText(reloaded.get());
    ASSERT_NE(nullptr, text);
    ASSERT_EQ(size_t(1), text->getStyleRuns().size());
    EXPECT_EQ(size_t(3), text->getStyleRuns()[0].start);
    EXPECT_EQ(TEXT.size(), text->getStyleRuns()[0].end);
}

TEST(ControlSaveHandlerTextRuns, textWithCarriageReturnsRoundTripsByteForByte) {
    /*
     * A run is a pair of byte offsets into the saved text, so the text has to come back exactly
     * as it went out. XML parsers fold a CR, and the CR of a CRLF pair, into a single line feed,
     * which would shorten every line of a passage pasted in from a Windows program and slide the
     * styling off the words it was applied to.
     */
    const std::string crText = "ab\r\ncd\ref";  // 9 bytes

    auto doc = std::make_unique<Document>(nullptr);
    auto page = std::make_shared<XojPage>(PAGE_WIDTH, PAGE_HEIGHT);
    auto text = std::make_unique<Text>();
    text->setText(crText);
    text->setOrigin(20.0, 30.0);
    text->setColor(Colors::black);

    TextStyleRun bold;
    bold.start = 6;  // the "\ref" at the end, CR included
    bold.end = 9;
    bold.bold = true;
    text->setStyleRuns({bold});
    page->getLayers()[0]->addElement(std::move(text));
    doc->addPage(page);

    const auto reloaded = saveAndReload(doc.get(), "textruns-carriage-return.xopp");
    ASSERT_TRUE(reloaded);

    const Text* back = firstText(reloaded.get());
    ASSERT_NE(nullptr, back);
    EXPECT_EQ(crText, back->getText());
    ASSERT_EQ(size_t(1), back->getStyleRuns().size());
    EXPECT_EQ(size_t(6), back->getStyleRuns()[0].start);
    EXPECT_EQ(size_t(9), back->getStyleRuns()[0].end);
}

TEST(ControlSaveHandlerTextRuns, replacingTheTextKeepsTheRunsInsideIt) {
    // The one setter that can invalidate the offsets on its own: nothing may be left pointing
    // past the end of the text, or serialize() would write offsets it would refuse to read back
    auto text = std::make_unique<Text>();
    text->setText(TEXT);

    TextStyleRun run;
    run.start = 5;
    run.end = 20;
    run.bold = true;
    text->setStyleRuns({run});
    ASSERT_EQ(size_t(1), text->getStyleRuns().size());

    // A shorter text cuts the run back to what is left of it ...
    text->setText("abcdefgh");
    ASSERT_EQ(size_t(1), text->getStyleRuns().size());
    EXPECT_EQ(size_t(5), text->getStyleRuns()[0].start);
    EXPECT_EQ(size_t(8), text->getStyleRuns()[0].end);

    // ... and a text with nothing left of it at all drops the run
    text->setText("ab");
    EXPECT_TRUE(text->getStyleRuns().empty());
}

TEST(ControlSaveHandlerTextRuns, aRunSplittingACharacterIsCutBackWhenTheTextArrives) {
    // What a hand-edited "runs" attribute can name, and what GTK aborts the process over as soon
    // as the element is opened for editing
    auto text = std::make_unique<Text>();
    text->setText(TEXT);  // ends in "测试"

    const size_t insideTheLastCharacter = TEXT.size() - 1;
    TextStyleRun run;
    run.start = insideTheLastCharacter;
    run.end = TEXT.size();
    run.italic = true;
    text->setStyleRuns({run});

    EXPECT_TRUE(text->getStyleRuns().empty());
}

TEST(ControlSaveHandlerTextRuns, cloningCarriesTheRuns) {
    // Undo and the clipboard both go through cloneText, and a clone that drops the styling would
    // silently unstyle text on every edit session
    const auto doc = makeDocumentWithOneText(someRuns());
    const Text* text = firstText(doc.get());
    ASSERT_NE(nullptr, text);

    const auto clone = text->cloneText();
    EXPECT_EQ(text->getStyleRuns(), clone->getStyleRuns());
}

}  // namespace
