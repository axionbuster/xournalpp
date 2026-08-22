/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @license GNU GPLv2 or later
 */

#include <cstdint>

#include <cairo.h>
#include <gtest/gtest.h>

#include "gui/PointerMarker.h"
#include "gui/XournalppCursor.h"

namespace {

class MarkerSurface {
public:
    MarkerSurface(): surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100)), cr(cairo_create(surface)) {
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    }

    ~MarkerSurface() {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }

    auto alphaAt(int x, int y) -> std::uint8_t {
        cairo_surface_flush(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        const auto* row = reinterpret_cast<const std::uint32_t*>(cairo_image_surface_get_data(surface) + y * stride);
        return static_cast<std::uint8_t>(row[x] >> 24U);
    }

    cairo_surface_t* surface;
    cairo_t* cr;
};

}  // namespace

TEST(PointerMarker, ScalesToEveryDestinationByTheSameFrameRatio) {
    EXPECT_DOUBLE_EQ(20.0, xoj::gui::scalePointerMarkerDiameter(24.0, 900.0, 1080));
    EXPECT_DOUBLE_EQ(48.0, xoj::gui::scalePointerMarkerDiameter(24.0, 2160.0, 1080));
    EXPECT_DOUBLE_EQ(0.0, xoj::gui::scalePointerMarkerDiameter(0.0, 900.0, 1080));
    EXPECT_DOUBLE_EQ(0.0, xoj::gui::scalePointerMarkerDiameter(24.0, 900.0, 0));
}

TEST(PointerMarker, NonPositiveDiameterDrawsNothing) {
    MarkerSurface target;
    EXPECT_FALSE(
            xoj::gui::drawPointerMarker(target.cr, {50.0, 50.0}, {0.0, 4.0, POINTER_MARKER_DISK, Color(0xFFFF0000U)}));
    EXPECT_EQ(0, target.alphaAt(50, 50));
}

TEST(PointerMarker, DoesNotPaintAPathLeftByTheCaller) {
    MarkerSurface target;
    cairo_rectangle(target.cr, 10.0, 10.0, 5.0, 5.0);

    ASSERT_TRUE(xoj::gui::drawPointerMarker(
            target.cr, {50.0, 50.0}, {20.0, 4.0, POINTER_MARKER_DISK, Color(0xFFFF0000U)}));
    EXPECT_EQ(0, target.alphaAt(12, 12));
    EXPECT_GT(target.alphaAt(50, 50), 0);
}

TEST(PointerMarker, DiskRingAndDotRetainTheirConfiguredOcclusion) {
    constexpr xoj::util::Point<double> center{50.0, 50.0};
    constexpr Color red(0xFFFF0000U);

    MarkerSurface disk;
    ASSERT_TRUE(xoj::gui::drawPointerMarker(disk.cr, center, {40.0, 4.0, POINTER_MARKER_DISK, red}));
    EXPECT_GT(disk.alphaAt(50, 50), 200);  // solid tip
    EXPECT_GT(disk.alphaAt(60, 50), 0);    // translucent pool
    EXPECT_LT(disk.alphaAt(60, 50), 200);

    MarkerSurface ring;
    ASSERT_TRUE(xoj::gui::drawPointerMarker(ring.cr, center, {40.0, 4.0, POINTER_MARKER_RING, red}));
    EXPECT_GT(ring.alphaAt(50, 50), 200);  // solid tip
    EXPECT_EQ(ring.alphaAt(60, 50), 0);    // clear interior
    EXPECT_GT(ring.alphaAt(70, 50), 0);    // outlined edge

    MarkerSurface dot;
    ASSERT_TRUE(xoj::gui::drawPointerMarker(dot.cr, center, {40.0, 4.0, POINTER_MARKER_DOT, red}));
    EXPECT_EQ(dot.alphaAt(50, 50), 255);
    EXPECT_EQ(dot.alphaAt(60, 50), 255);
}

TEST(PointerMarker, EraserKeepsFrameMarkerButNotLiveCanvasMarker) {
    constexpr xoj::util::Point<double> center{50.0, 50.0};
    constexpr xoj::gui::PointerMarkerStyle eraserStyle{40.0, 4.0, POINTER_MARKER_DISK, Color(0xFF808080U)};

    MarkerSurface liveCanvas;
    EXPECT_FALSE(xoj::gui::drawLivePointerMarker(liveCanvas.cr, center, eraserStyle, TOOL_ERASER));
    EXPECT_EQ(0, liveCanvas.alphaAt(50, 50));

    MarkerSurface recordedFrame;
    EXPECT_TRUE(xoj::gui::drawPointerMarker(recordedFrame.cr, center, eraserStyle));
    EXPECT_GT(recordedFrame.alphaAt(50, 50), 0);

    MarkerSurface livePen;
    EXPECT_TRUE(xoj::gui::drawLivePointerMarker(livePen.cr, center, eraserStyle, TOOL_PEN));
    EXPECT_GT(livePen.alphaAt(50, 50), 0);
}

TEST(PointerMarker, LiveCanvasMarkerIsGatedByTool) {
    for (int value = TOOL_NONE; value < TOOL_END_ENTRY; value++) {
        const auto tool = static_cast<ToolType>(value);
        SCOPED_TRACE(toolTypeToString(tool));
        EXPECT_EQ(tool != TOOL_ERASER, xoj::gui::pointerMarkerDrawsOnLiveCanvas(tool));
    }
}

TEST(PointerMarker, NativeCursorRemainsOnlyForInteractionAffordances) {
    for (int value = TOOL_NONE; value < TOOL_END_ENTRY; value++) {
        const auto tool = static_cast<ToolType>(value);
        const bool expected = tool == TOOL_ERASER || tool == TOOL_HAND || tool == TOOL_TEXT || tool == TOOL_LATEX ||
                              tool == TOOL_LINK || tool == TOOL_PLAY_OBJECT || tool == TOOL_VERTICAL_SPACE ||
                              tool == TOOL_SELECT_PDF_TEXT_LINEAR;
        SCOPED_TRACE(toolTypeToString(tool));
        EXPECT_EQ(expected, xoj::gui::pointerMarkerKeepsNativeCursor(tool, CURSOR_SELECTION_NONE));
    }

    for (int value = CURSOR_SELECTION_MOVE; value <= CURSOR_SELECTION_DELETE; value++) {
        const auto selection = static_cast<CursorSelectionType>(value);
        EXPECT_TRUE(xoj::gui::pointerMarkerKeepsNativeCursor(TOOL_PEN, selection));
        EXPECT_TRUE(xoj::gui::pointerMarkerKeepsNativeCursor(TOOL_ERASER, selection));
    }

    EXPECT_TRUE(xoj::gui::pointerMarkerKeepsNativeCursor(TOOL_PEN, CURSOR_SELECTION_NONE, true));
    EXPECT_TRUE(xoj::gui::pointerMarkerKeepsNativeCursor(TOOL_HIGHLIGHTER, CURSOR_SELECTION_NONE, true));
    EXPECT_FALSE(xoj::gui::pointerMarkerKeepsNativeCursor(TOOL_DRAW_RECT, CURSOR_SELECTION_NONE, true));
}

TEST(XournalppCursorPolicy, DeviceClassTransitionReappliesCursor) {
    for (int current = INPUT_DEVICE_MOUSE; current <= INPUT_DEVICE_IGNORE; current++) {
        for (int next = INPUT_DEVICE_MOUSE; next <= INPUT_DEVICE_IGNORE; next++) {
            const auto currentDevice = static_cast<InputDeviceClass>(current);
            const auto nextDevice = static_cast<InputDeviceClass>(next);
            EXPECT_EQ(currentDevice != nextDevice,
                      xoj::gui::inputDeviceTransitionRequiresCursorReapply(currentDevice, nextDevice));
        }
    }
}

TEST(XournalppCursorPolicy, EraserVisibilitySettingsRemainAuthoritativeForStylusDevices) {
    for (auto device: {INPUT_DEVICE_PEN, INPUT_DEVICE_ERASER}) {
        EXPECT_FALSE(xoj::gui::eraserCursorIsVisible(device, ERASER_VISIBILITY_NEVER, false));
        EXPECT_FALSE(xoj::gui::eraserCursorIsVisible(device, ERASER_VISIBILITY_NEVER, true));

        EXPECT_TRUE(xoj::gui::eraserCursorIsVisible(device, ERASER_VISIBILITY_ALWAYS, false));
        EXPECT_TRUE(xoj::gui::eraserCursorIsVisible(device, ERASER_VISIBILITY_ALWAYS, true));

        EXPECT_TRUE(xoj::gui::eraserCursorIsVisible(device, ERASER_VISIBILITY_HOVER, false));
        EXPECT_FALSE(xoj::gui::eraserCursorIsVisible(device, ERASER_VISIBILITY_HOVER, true));

        EXPECT_FALSE(xoj::gui::eraserCursorIsVisible(device, ERASER_VISIBILITY_TOUCH, false));
        EXPECT_TRUE(xoj::gui::eraserCursorIsVisible(device, ERASER_VISIBILITY_TOUCH, true));
    }
}

TEST(XournalppCursorPolicy, MouseLikeDevicesKeepTheEraserOutline) {
    for (auto device: {INPUT_DEVICE_MOUSE, INPUT_DEVICE_TOUCHSCREEN, INPUT_DEVICE_IGNORE}) {
        for (int value = ERASER_VISIBILITY_NEVER; value <= ERASER_VISIBILITY_TOUCH; value++) {
            const auto visibility = static_cast<EraserVisibility>(value);
            EXPECT_TRUE(xoj::gui::eraserCursorIsVisible(device, visibility, false));
            EXPECT_TRUE(xoj::gui::eraserCursorIsVisible(device, visibility, true));
        }
    }
}
