/*
 * Xournal++
 *
 * Displays the content of a selection
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cairo.h>

#include "util/Rectangle.h"

class ElementContainer;

namespace xoj::view {
class Context;
enum EditorOnlyTreatment : bool;

class ElementContainerView {
public:
    ElementContainerView(const ElementContainer* container);

    /**
     * @brief Draws the container to the given context
     */
    void draw(const Context& ctx) const;

    /**
     * Draw the container directly into @p cr while mapping @p sourceBounds onto
     * @p targetBounds.
     *
     * Unlike callers which rasterize into an intermediate surface, this keeps the
     * elements sharp at the destination's own scale. Negative target dimensions
     * mirror the content in the same way as a resized edit selection.
     *
     * @p editorOnlyTreatment decides whether editor-only elements appear (faded, for the
     * editing canvas) or are left out (for recorded / projected frames).
     */
    void drawTransformed(cairo_t* cr, const xoj::util::Rectangle<double>& sourceBounds,
                         const xoj::util::Rectangle<double>& targetBounds,
                         EditorOnlyTreatment editorOnlyTreatment) const;

private:
    const ElementContainer* container;
};
};  // namespace xoj::view
