/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @license GNU GPLv2 or later
 */

#include <cstdint>

#include <glib.h>
#include <gtest/gtest.h>

#include "control/VideoRecorder.h"
#include "gui/CanvasFrame.h"

TEST(VideoRecorderScheduling, RenderSourceRunsBelowInputAndOrdinaryUiWork) {
    // GLib priorities increase as urgency decreases. GDK input is serviced at ordinary/default
    // priority, while an idle source yields to both it and GTK's queued redraws.
    EXPECT_EQ(G_PRIORITY_DEFAULT_IDLE, xoj::video::FRAME_RENDER_SOURCE_PRIORITY);
    EXPECT_GT(xoj::video::FRAME_RENDER_SOURCE_PRIORITY, G_PRIORITY_DEFAULT);
}

TEST(VideoRecorderScheduling, UnchangedActivitySkipsUntilTheWatchdog) {
    xoj::video::FrameRenderGate gate;
    const xoj::video::FrameActivitySnapshot activity{11, 22, 33, 44, 4};
    constexpr gint64 renderedAt = 1'000'000;

    EXPECT_TRUE(gate.shouldRender(activity, renderedAt));
    gate.markRendered(activity, renderedAt);

    EXPECT_FALSE(gate.shouldRender(activity, renderedAt + xoj::video::UNREPORTED_ACTIVITY_WATCHDOG - 1));
    EXPECT_TRUE(gate.shouldRender(activity, renderedAt + xoj::video::UNREPORTED_ACTIVITY_WATCHDOG));
}

TEST(VideoRecorderScheduling, EveryReportedFrameInputOpensTheGate) {
    xoj::video::FrameRenderGate gate;
    const xoj::video::FrameActivitySnapshot rendered{11, 22, 33, 44, 4};
    gate.markRendered(rendered, 1'000'000);

    EXPECT_TRUE(gate.shouldRender({12, 22, 33, 44, 4}, 1'000'001));  // pointer/overlay/repaint
    EXPECT_TRUE(gate.shouldRender({11, 23, 33, 44, 4}, 1'000'001));  // settled canvas
    EXPECT_TRUE(gate.shouldRender({11, 22, 34, 44, 4}, 1'000'001));  // cache pixels refreshed/patched
    EXPECT_TRUE(gate.shouldRender({11, 22, 33, 45, 4}, 1'000'001));  // stale refresh rejected
    EXPECT_TRUE(gate.shouldRender({11, 22, 33, 44, 5}, 1'000'001));  // page selection
}

TEST(VideoRecorderScheduling, SixtyHertzStillCanvasNeedsOnlyFourSafetyRendersPerSecond) {
    xoj::video::FrameRenderGate gate;
    const xoj::video::FrameActivitySnapshot activity{11, 22, 33, 44, 4};
    gate.markRendered(activity, 0);

    int renders = 0;
    for (int tick = 1; tick <= 60; tick++) {
        const gint64 now = static_cast<gint64>(tick) * 1'000'000 / 60;
        if (gate.shouldRender(activity, now)) {
            renders++;
            gate.markRendered(activity, now);
        }
    }

    EXPECT_EQ(4, renders);
}

TEST(VideoRecorderScheduling, RejectedOldRefreshImmediatelyReopensGateForLatestRevision) {
    xoj::video::FrameRenderGate gate;

    // R2 was already drawn while the R1 refresh was in flight, so the gate has consumed the live
    // canvas revision but kickRefresh() could not start R2's replacement yet.
    const xoj::video::FrameActivitySnapshot r2WhileR1InFlight{12, 2, 33, 44, 4};
    gate.markRendered(r2WhileR1InFlight, 1'000'000);
    EXPECT_FALSE(gate.shouldRender(r2WhileR1InFlight, 1'000'001));

    // Rejecting R1 leaves pixels alone but advances the cache refresh generation. The next tick
    // must run immediately instead of waiting for the 250 ms watchdog, so it can kick R2.
    const xoj::video::FrameActivitySnapshot afterR1Rejected{12, 2, 33, 45, 4};
    EXPECT_TRUE(gate.shouldRender(afterR1Rejected, 1'000'001));
}

TEST(CanvasFrameRefresh, AcceptsOnlyAResultThatStillMatchesTheLiveCache) {
    xoj::canvas::FrameRefreshValidity valid{true, true, true, 7, 7, 19, 19};
    EXPECT_TRUE(xoj::canvas::canAdoptFrameRefresh(valid));

    valid.cacheHasSurface = false;
    EXPECT_FALSE(xoj::canvas::canAdoptFrameRefresh(valid));
    valid.cacheHasSurface = true;

    valid.pageMatches = false;
    EXPECT_FALSE(xoj::canvas::canAdoptFrameRefresh(valid));
    valid.pageMatches = true;

    valid.sizeMatches = false;
    EXPECT_FALSE(xoj::canvas::canAdoptFrameRefresh(valid));
    valid.sizeMatches = true;

    valid.renderedEpoch++;
    EXPECT_FALSE(xoj::canvas::canAdoptFrameRefresh(valid));
    valid.renderedEpoch = valid.cacheEpoch;

    valid.currentCanvasRevision++;
    EXPECT_FALSE(xoj::canvas::canAdoptFrameRefresh(valid));
}

TEST(CanvasFrameRefresh, RejectsAResultWhenRevisionAdvancesBeforeUiCompletion) {
    const xoj::canvas::FrameRefreshValidity stale{
            true, true, true, 7, 7,
            20,  // live canvas advanced after the worker released its document lock
            19   // revision the worker actually rendered
    };

    EXPECT_FALSE(xoj::canvas::canAdoptFrameRefresh(stale));
}
