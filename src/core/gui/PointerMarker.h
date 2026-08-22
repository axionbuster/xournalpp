/*
 * Xournal++
 *
 * Draws the pointer marker shared by the canvas, projector and video recorder.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cairo.h>  // for cairo_t

#include "control/ToolEnums.h"                  // for ToolType
#include "control/settings/SettingsEnums.h"     // for PointerMarkerShape
#include "control/tools/CursorSelectionType.h"  // for CursorSelectionType
#include "util/Color.h"                         // for Color
#include "util/Point.h"                         // for Point

namespace xoj::gui {

struct PointerMarkerStyle {
    double diameter{};
    double tipDiameter{};
    PointerMarkerShape shape{POINTER_MARKER_DISK};
    Color color{};
};

/**
 * Scale a configured video-frame marker diameter to another rendered area.
 *
 * The configured diameter is measured against @p configuredFrameHeight. Keeping the same ratio
 * makes the marker read at the same size on the canvas, in the projector and in the recording.
 */
double scalePointerMarkerDiameter(double configuredDiameter, double areaHeight, int configuredFrameHeight);

/** Draw one marker at @p center. Returns false when the requested diameter draws nothing. */
bool drawPointerMarker(cairo_t* cr, const xoj::util::Point<double>& center, const PointerMarkerStyle& style);

/**
 * Whether a native cursor conveys an affordance worth retaining underneath the shared marker.
 *
 * Ordinary pointing and drawing use only the marker, which avoids platform cursor-size limits and
 * a native arrow leaking through. Selection handles and mode-specific interaction cursors remain.
 */
bool pointerMarkerKeepsNativeCursor(ToolType tool, CursorSelectionType selectionType,
                                    bool drawDirectionAffordance = false);

}  // namespace xoj::gui
