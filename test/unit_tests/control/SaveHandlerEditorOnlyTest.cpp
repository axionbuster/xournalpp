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
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "util/PathUtil.h"

#include "config.h"  // for FILE_FORMAT_VERSION
#include "filesystem.h"

namespace {

constexpr double PAGE_WIDTH = 300.0;
constexpr double PAGE_HEIGHT = 400.0;

/// A document holding one page, one layer, one two-point stroke and one text element
std::unique_ptr<Document> makeDocument(bool strokeEditorOnly, bool textEditorOnly) {
    auto doc = std::make_unique<Document>(nullptr);
    auto page = std::make_shared<XojPage>(PAGE_WIDTH, PAGE_HEIGHT);

    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(2.5);
    stroke->setColor(Color(0xff0000U));
    stroke->addPoint(Point(20.0, 30.0));
    stroke->addPoint(Point(120.0, 130.0));
    stroke->setEditorOnly(strokeEditorOnly);
    page->getLayers()[0]->addElement(std::move(stroke));

    auto text = std::make_unique<Text>();
    text->setText("construction line");
    text->setOrigin(40.0, 50.0);
    text->setEditorOnly(textEditorOnly);
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

const Layer* firstLayer(const Document* doc) {
    EXPECT_EQ(size_t(1), doc->getPageCount());
    ConstPageRef page = doc->getPage(0);
    EXPECT_EQ(size_t(1), page->getLayerCount());
    return page->getLayersView()[0];
}

TEST(ControlSaveHandlerEditorOnly, ordinaryDocumentsDeclareTheStockVersion) {
    const auto doc = makeDocument(false, false);

    EXPECT_FALSE(SaveHandler::hasForkFormatExtensions(doc.get()));
    EXPECT_EQ(SaveHandler::STOCK_FILE_FORMAT_VERSION, SaveHandler::fileFormatVersion(doc.get()));
}

TEST(ControlSaveHandlerEditorOnly, editorOnlyElementsDeclareTheForkVersion) {
    const auto doc = makeDocument(true, false);

    EXPECT_TRUE(SaveHandler::hasForkFormatExtensions(doc.get()));
    EXPECT_EQ(FILE_FORMAT_VERSION, SaveHandler::fileFormatVersion(doc.get()));
}

TEST(ControlSaveHandlerEditorOnly, ordinaryElementsRoundTripWithoutTheFlag) {
    const auto doc = makeDocument(false, false);
    const auto reloaded = saveAndReload(doc.get(), "editoronly-plain.xopp");
    ASSERT_TRUE(reloaded);

    const Layer* layer = firstLayer(reloaded.get());
    ASSERT_EQ(size_t(2), layer->getElementsView().size());
    for (const auto& e: layer->getElementsView()) {
        EXPECT_FALSE(e->isEditorOnly());
    }
}

TEST(ControlSaveHandlerEditorOnly, editorOnlyElementsRoundTrip) {
    const auto doc = makeDocument(true, true);
    const auto reloaded = saveAndReload(doc.get(), "editoronly-both.xopp");
    ASSERT_TRUE(reloaded);

    const Layer* layer = firstLayer(reloaded.get());
    ASSERT_EQ(size_t(2), layer->getElementsView().size());
    for (const auto& e: layer->getElementsView()) {
        EXPECT_TRUE(e->isEditorOnly());
    }
}

TEST(ControlSaveHandlerEditorOnly, mixedDocumentsKeepTheFlagPerElement) {
    const auto doc = makeDocument(true, false);
    const auto reloaded = saveAndReload(doc.get(), "editoronly-mixed.xopp");
    ASSERT_TRUE(reloaded);

    const Layer* layer = firstLayer(reloaded.get());
    ASSERT_EQ(size_t(2), layer->getElementsView().size());
    EXPECT_EQ(ELEMENT_STROKE, layer->getElementsView()[0]->getType());
    EXPECT_TRUE(layer->getElementsView()[0]->isEditorOnly());
    EXPECT_EQ(ELEMENT_TEXT, layer->getElementsView()[1]->getType());
    EXPECT_FALSE(layer->getElementsView()[1]->isEditorOnly());
}

}  // namespace
