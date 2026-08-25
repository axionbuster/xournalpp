/*
 * Xournal++
 *
 * Saves a document
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>  // for unique_ptr
#include <optional>
#include <string>  // for string
#include <vector>  // for vector

#include "control/xml/XmlNode.h"    // for XmlNode
#include "model/BackgroundImage.h"  // for BackgroundImage
#include "model/PageRef.h"          // for PageRef
#include "util/Color.h"             // for Color

#include "filesystem.h"  // for path

class XmlPointNode;
class ProgressListener;
class AudioElement;
class Document;
class Element;
class Layer;
class OutputStream;
class Stroke;
class Text;
class XmlAudioNode;
class XmlTextNode;

class SaveHandler {
public:
    SaveHandler();

public:
    /// Prepare an XML tree corresponding to the document - Needs read-only access to the Document
    void prepareSave(const Document* doc, const fs::path& target);
    /// Writes the XML to the given path. Does not access the Document instance
    void saveTo(const fs::path& filepath, ProgressListener* listener = nullptr);
    /**
     * Writes the XML to the given stream. Does not access the Document instance with the following exception:
     * attached background images are written to disk. This is safe as long as we do not modify the background images
     * anywhere
     */
    void saveTo(OutputStream* out, const fs::path& filepath, ProgressListener* listener = nullptr);
    /// Update document information. Requires write access to the Document.
    void updateDocumentInfo(Document* doc);

    const std::string& getErrorMessage();

    /// The file format version stock Xournal++ writes and understands.
    static constexpr int STOCK_FILE_FORMAT_VERSION = 5;

    /**
     * @brief Does the document use any of this fork's file format extensions?
     *
     * The fork's extensions are extra attributes on existing elements, which stock Xournal++
     * ignores; a document carrying them is nonetheless tagged with the fork's file format
     * version so that a stock build warns before opening it. Today that means: does any stroke
     * carry line shape metadata, or any text carry inline style runs. This is the single choke
     * point for the decision — a later fork-only construct ORs its own test in here.
     */
    static bool hasForkFormatExtensions(const Document* doc);

    /// The file format version to declare for this document
    static int fileFormatVersion(const Document* doc);

protected:
    static std::string getColorStr(Color c, unsigned char alpha = 0xff);

    virtual void visitPage(XmlNode* root, ConstPageRef p, const Document* doc, int id, const fs::path& target);
    virtual void visitLayer(XmlNode* page, const Layer* l);
    virtual void visitStroke(XmlPointNode* stroke, const Stroke* s);

    /**
     * Export the fill attributes
     */
    virtual void visitStrokeExtended(XmlPointNode* stroke, const Stroke* s);

    /**
     * Export the inline style runs, which only this fork's format has
     */
    virtual void visitTextExtended(XmlTextNode* text, const Text* t);

    /**
     * Export the attributes any element type can carry, which only this fork's format has
     * (today: the editor-only flag). Called for every element, whatever its type.
     */
    virtual void visitElementExtended(XmlNode* node, const Element* e);

    virtual void writeHeader(const Document* doc);
    virtual void writeSolidBackground(XmlNode* background, ConstPageRef p);
    virtual void writeTimestamp(XmlAudioNode* xmlAudioNode, const AudioElement* audioElement);
    virtual void writeBackgroundName(XmlNode* background, ConstPageRef p);

protected:
    std::unique_ptr<XmlNode> root{};
    bool firstPdfPageVisited;
    int attachBgId;

    std::string errorMessage;

    struct ImageInfo {
        BackgroundImage image;  ///< This is a wrapped shared pointer
        std::optional<fs::path> newPath;
        int newId;
    };
    std::vector<ImageInfo> backgroundImages{};
};
