#include "PointerMarker.h"

#include <algorithm>  // for clamp, max
#include <cmath>      // for isfinite, M_PI

#include "util/Color.h"  // for cairo_set_source_rgbi

namespace xoj::gui {

double scalePointerMarkerDiameter(double configuredDiameter, double areaHeight, int configuredFrameHeight) {
    if (!std::isfinite(configuredDiameter) || !std::isfinite(areaHeight) || configuredDiameter <= 0.0 ||
        areaHeight <= 0.0 || configuredFrameHeight <= 0) {
        return 0.0;
    }
    return configuredDiameter * areaHeight / configuredFrameHeight;
}

bool drawPointerMarker(cairo_t* cr, const xoj::util::Point<double>& center, const PointerMarkerStyle& style) {
    if (cr == nullptr || !std::isfinite(style.diameter) || style.diameter <= 0.0) {
        return false;
    }

    const double radius = style.diameter / 2.0;

    cairo_save(cr);
    // A Cairo save/restore does not include the current path. Never let a path left by an earlier
    // overlay become part of the marker's fill or stroke.
    cairo_new_path(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    switch (style.shape) {
        case POINTER_MARKER_RING:
            // Put a dark, wider stroke underneath the colored one. The ring remains legible over
            // both white paper and ink of the current tool color without covering the page.
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
            cairo_set_line_width(cr, radius * 0.34);
            cairo_arc(cr, center.x, center.y, radius, 0.0, 2.0 * M_PI);
            cairo_stroke(cr);

            Util::cairo_set_source_rgbi(cr, style.color, 0.85);
            cairo_set_line_width(cr, radius * 0.22);
            cairo_arc(cr, center.x, center.y, radius, 0.0, 2.0 * M_PI);
            cairo_stroke(cr);
            break;

        case POINTER_MARKER_DOT:
            Util::cairo_set_source_rgbi(cr, style.color, 1.0);
            cairo_arc(cr, center.x, center.y, radius, 0.0, 2.0 * M_PI);
            cairo_fill(cr);

            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.30);
            cairo_set_line_width(cr, std::max(radius * 0.09, 0.4));
            cairo_arc(cr, center.x, center.y, radius, 0.0, 2.0 * M_PI);
            cairo_stroke(cr);
            break;

        case POINTER_MARKER_DISK:
        default:
            Util::cairo_set_source_rgbi(cr, style.color, 0.35);
            cairo_arc(cr, center.x, center.y, radius, 0.0, 2.0 * M_PI);
            cairo_fill(cr);

            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.30);
            cairo_set_line_width(cr, std::max(radius * 0.09, 0.4));
            cairo_arc(cr, center.x, center.y, radius, 0.0, 2.0 * M_PI);
            cairo_stroke(cr);
            break;
    }

    // Disk and ring carry a solid tip mark at the current tool width. A solid dot is already its
    // own tip and a second mark would only discolor its center.
    if (style.shape != POINTER_MARKER_DOT) {
        const double tip = std::clamp(style.tipDiameter / 2.0, radius * 0.12, radius * 0.4);
        Util::cairo_set_source_rgbi(cr, style.color, 0.95);
        cairo_arc(cr, center.x, center.y, tip, 0.0, 2.0 * M_PI);
        cairo_fill(cr);
    }

    cairo_restore(cr);
    return true;
}

bool pointerMarkerDrawsOnLiveCanvas(ToolType tool) {
    // The eraser outline communicates the actual erasing area. Preview/projector/video frames
    // cannot capture that native cursor and still use drawPointerMarker() with neutral gray.
    return tool != TOOL_ERASER;
}

bool drawLivePointerMarker(cairo_t* cr, const xoj::util::Point<double>& center, const PointerMarkerStyle& style,
                           ToolType tool) {
    return pointerMarkerDrawsOnLiveCanvas(tool) && drawPointerMarker(cr, center, style);
}

bool pointerMarkerKeepsNativeCursor(ToolType tool, CursorSelectionType selectionType, bool drawDirectionAffordance) {
    if (selectionType != CURSOR_SELECTION_NONE) {
        return true;
    }

    // BaseShapeHandler temporarily replaces the pen/highlighter cursor with Shift/Ctrl glyphs
    // while the drag direction determines modifier locking. Those glyphs communicate state that
    // the marker itself cannot; a stale flag on another tool must not bring its crosshair back.
    if (drawDirectionAffordance &&
        (tool == TOOL_PEN || tool == TOOL_HIGHLIGHTER || tool == TOOL_LASER_POINTER_PEN ||
         tool == TOOL_LASER_POINTER_HIGHLIGHTER)) {
        return true;
    }

    switch (tool) {
        case TOOL_ERASER:
        case TOOL_HAND:
        case TOOL_TEXT:
        case TOOL_LATEX:
        case TOOL_LINK:
        case TOOL_PLAY_OBJECT:
        case TOOL_VERTICAL_SPACE:
        case TOOL_SELECT_PDF_TEXT_LINEAR:
            return true;
        default:
            return false;
    }
}

}  // namespace xoj::gui
