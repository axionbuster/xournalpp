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
#include <optional>
#include <string>
#include <vector>

#include <config-test.h>
#include <gtest/gtest.h>

#include "control/xojfile/LoadHandler.h"
#include "control/xojfile/SaveHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/LineShape.h"
#include "model/PageRef.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "util/PathUtil.h"

#include "config.h"  // for FILE_FORMAT_VERSION
#include "filesystem.h"

namespace {

constexpr double PAGE_WIDTH = 300.0;
constexpr double PAGE_HEIGHT = 400.0;

/// A document holding one page, one layer, and one straight two-point stroke
std::unique_ptr<Document> makeDocumentWithOneStroke(std::optional<LineShape> shape) {
    auto doc = std::make_unique<Document>(nullptr);
    auto page = std::make_shared<XojPage>(PAGE_WIDTH, PAGE_HEIGHT);

    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(2.5);
    stroke->setColor(Color(0xff0000U));
    stroke->addPoint(Point(20.0, 30.0));
    stroke->addPoint(Point(120.0, 130.0));
    if (shape) {
        stroke->setLineShape(*shape);
    }
    page->getLayers()[0]->addElement(std::move(stroke));

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

const Stroke* firstStroke(const Document* doc) {
    EXPECT_EQ(size_t(1), doc->getPageCount());
    ConstPageRef page = doc->getPage(0);
    EXPECT_EQ(size_t(1), page->getLayerCount());
    const Layer* layer = page->getLayersView()[0];
    EXPECT_EQ(size_t(1), layer->getElementsView().size());
    return dynamic_cast<const Stroke*>(layer->getElementsView()[0]);
}

TEST(ControlSaveHandlerLineShape, plainDocumentsDeclareTheStockVersion) {
    const auto doc = makeDocumentWithOneStroke(std::nullopt);

    EXPECT_FALSE(SaveHandler::hasForkFormatExtensions(doc.get()));
    EXPECT_EQ(SaveHandler::STOCK_FILE_FORMAT_VERSION, SaveHandler::fileFormatVersion(doc.get()));
}

TEST(ControlSaveHandlerLineShape, documentsWithShapedStrokesDeclareTheForkVersion) {
    const auto doc =
            makeDocumentWithOneStroke(LineShape{LineShapeType::RAY, Point(20.0, 30.0), Point(100.0, 110.0)});

    EXPECT_TRUE(SaveHandler::hasForkFormatExtensions(doc.get()));
    EXPECT_EQ(FILE_FORMAT_VERSION, SaveHandler::fileFormatVersion(doc.get()));
    // The fork's version must be ahead of the stock one, or the whole tagging scheme is moot
    EXPECT_GT(FILE_FORMAT_VERSION, SaveHandler::STOCK_FILE_FORMAT_VERSION);
}

TEST(ControlSaveHandlerLineShape, plainStrokesRoundTripWithoutAShape) {
    const auto doc = makeDocumentWithOneStroke(std::nullopt);
    const auto reloaded = saveAndReload(doc.get(), "lineshape-plain.xopp");
    ASSERT_TRUE(reloaded);

    const Stroke* stroke = firstStroke(reloaded.get());
    ASSERT_NE(nullptr, stroke);
    EXPECT_FALSE(stroke->getLineShape().has_value());
}

TEST(ControlSaveHandlerLineShape, everyShapeTypeRoundTrips) {
    const LineShapeType::Value types[] = {LineShapeType::RAY, LineShapeType::INFINITE_LINE, LineShapeType::ARROW,
                                          LineShapeType::DOUBLE_ARROW};

    for (const auto type: types) {
        const LineShape shape{type, Point(20.5, 30.25), Point(100.125, 110.75)};
        const auto doc = makeDocumentWithOneStroke(shape);
        const auto reloaded =
                saveAndReload(doc.get(), "lineshape-" + std::to_string(static_cast<int>(type)) + ".xopp");
        ASSERT_TRUE(reloaded);

        const Stroke* stroke = firstStroke(reloaded.get());
        ASSERT_NE(nullptr, stroke);
        ASSERT_TRUE(stroke->getLineShape().has_value());
        EXPECT_EQ(type, stroke->getLineShape()->type);
        EXPECT_DOUBLE_EQ(shape.anchorA.x, stroke->getLineShape()->anchorA.x);
        EXPECT_DOUBLE_EQ(shape.anchorA.y, stroke->getLineShape()->anchorA.y);
        EXPECT_DOUBLE_EQ(shape.anchorB.x, stroke->getLineShape()->anchorB.x);
        EXPECT_DOUBLE_EQ(shape.anchorB.y, stroke->getLineShape()->anchorB.y);
    }
}

}  // namespace
