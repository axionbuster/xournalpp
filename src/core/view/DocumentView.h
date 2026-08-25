/*
 * Xournal++
 *
 * Paints a page to a cairo context
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cairo.h>  // for cairo_t

#include "model/PageRef.h"  // for ConstPageRef
#include "util/ElementRange.h"
#include "view/background/BackgroundFlags.h"

class PdfCache;

namespace xoj::view {
struct BackgroundFlags;
};

class DocumentView {
public:
    DocumentView() = default;
    virtual ~DocumentView() = default;

public:
    /**
     * Draw the full page, usually you would like to call this method
     * @param page The page to draw
     * @param cr Draw to thgis context
     * @param dontRenderEditingStroke false to draw currently drawing stroke
     * @param flags show/hide various background components
     */
    void drawPage(ConstPageRef page, cairo_t* cr, bool dontRenderEditingStroke,
                  xoj::view::BackgroundFlags flags = xoj::view::BACKGROUND_SHOW_ALL);

    /**
     * Only draws the prescribed layers of the given page, regardless of the layer's current visibility.
     * @param layerRange Range of layers to draw
     * @param page The page to draw
     * @param cr Draw to this context
     * @param dontRenderEditingStroke false to draw currently drawing stroke
     * @param flags show/hide various background components
     */
    void drawLayersOfPage(const LayerRangeVector& layerRange, ConstPageRef page, cairo_t* cr,
                          bool dontRenderEditingStroke,
                          xoj::view::BackgroundFlags flags = xoj::view::BACKGROUND_SHOW_ALL);

    /**
     * Mark stroke with Audio
     */
    void setMarkAudioStroke(bool markAudioStroke);

    /**
     * Leave out editor-only elements entirely, instead of drawing them faded.
     * Set by every rendering meant for an audience: exports, prints, recorded and
     * projected frames, and the file preview thumbnail.
     */
    void setHideEditorOnlyElements(bool hideEditorOnlyElements);

    // API for special drawing, usually you won't call this methods
public:
    void setPdfCache(PdfCache* cache);

    /**
     * Drawing first step
     * @param page The page to draw
     * @param cr Draw to thgis context
     * @param dontRenderEditingStroke false to draw currently drawing stroke
     */
    void initDrawing(ConstPageRef page, cairo_t* cr, bool dontRenderEditingStroke);

    /**
     * Draw the background
     */
    void drawBackground(xoj::view::BackgroundFlags bgFlags) const;

    /**
     * Draw background if there is no background shown, like in GIMP etc.
     */
    void drawTransparentBackgroundPattern();

    /**
     * Last step in drawing
     */
    void finializeDrawing();

private:
    cairo_t* cr = nullptr;
    ConstPageRef page = nullptr;
    PdfCache* pdfCache = nullptr;
    bool dontRenderEditingStroke = false;
    bool markAudioStroke = false;
    bool hideEditorOnlyElements = false;

};
