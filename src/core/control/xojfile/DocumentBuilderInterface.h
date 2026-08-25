/*
 * Xournal++
 *
 * Abstract interface for usage by the file parsers to be implemented by classes
 * constructing a Document.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/TextStyleRuns.h"  // for TextStyleRuns
#include "util/Color.h"

#include "filesystem.h"

class LineStyle;
class PageType;
struct LineShape;
class Point;
class StrokeCapStyle;
class StrokeTool;
class TextAlignment;

class DocumentBuilderInterface {
public:
    DocumentBuilderInterface() = default;
    virtual ~DocumentBuilderInterface() = default;

    virtual void addDocument(std::u8string creator, int fileVersion) = 0;
    virtual void finalizeDocument() = 0;
    virtual void addPage(double width, double height) = 0;
    virtual void finalizePage() = 0;
    virtual void addAudioAttachment(const fs::path& filename) = 0;
    virtual void setBgName(const std::string& name) = 0;
    virtual void setBgSolid(const PageType& bg, Color color) = 0;
    virtual void setBgPixmap(bool attach, const fs::path& filename) = 0;
    virtual void setBgPixmapCloned(size_t pageNr) = 0;
    virtual void setBgPdf(size_t pageno) = 0;
    virtual void loadBgPdf(bool attach, const fs::path& filename) = 0;
    virtual void addLayer(const std::optional<std::string_view>& name) = 0;
    virtual void finalizeLayer() = 0;
    virtual void addStroke(StrokeTool tool, Color color, double width, int fill, StrokeCapStyle capStyle,
                           const LineStyle& lineStyle, fs::path filename, size_t timestamp) = 0;
    /**
     * Attach line shape metadata to the stroke currently being built.
     *
     * Fork-only; called between addStroke() and finalizeStroke(), and only for the strokes
     * that carry the fork's "shape" and "anchors" attributes.
     */
    virtual void setStrokeLineShape(const LineShape& shape) = 0;
    virtual void setStrokePoints(std::vector<Point> pointVector, bool hasPressure) = 0;
    virtual void finalizeStroke() = 0;
    virtual void addText(std::string font, double size, double x, double y, Color color, std::optional<double> wrap,
                         std::optional<TextAlignment> align, bool justify, fs::path filename, size_t timestamp) = 0;
    /**
     * Attach inline style runs to the text currently being built.
     *
     * Fork-only; called between addText() and finalizeText(), and only for the texts that carry
     * the fork's "runs" attribute.
     */
    virtual void setTextStyleRuns(TextStyleRuns runs) = 0;
    virtual void setTextContents(std::string contents) = 0;
    virtual void finalizeText() = 0;
    virtual void addImage(double left, double top, double right, double bottom) = 0;
    virtual void setImageData(std::string data) = 0;
    virtual void setImageAttachment(const fs::path& filename) = 0;
    virtual void finalizeImage() = 0;
    virtual void addTexImage(double left, double top, double right, double bottom, std::string text) = 0;
    virtual void setTexImageData(std::string data) = 0;
    virtual void setTexImageAttachment(const fs::path& filename) = 0;
    virtual void finalizeTexImage() = 0;
    virtual void addLink(TextAlignment align, std::string font, double size, double x, double y, Color color,
                         std::string url) = 0;
    virtual void setLinkContent(std::string contents) = 0;
    virtual void finalizeLink() = 0;

    /**
     * Mark the element currently being built -- whatever its type -- as editor-only.
     *
     * Fork-only; called between the element's add*() and finalize*(), and only for elements
     * that carry the fork's "editorOnly" attribute.
     */
    virtual void setElementEditorOnly() = 0;

    /**
     * Store an error for retrieval through `getErrorMessages()` and print it
     * to the console as a warning.
     */
    virtual void logError(const std::string& error) = 0;
};
