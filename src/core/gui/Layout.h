/*
 * Xournal++
 *
 * Handles the layout of the pages within a Xournal document
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>     // for size_t
#include <functional>  // for function
#include <mutex>       // for mutex
#include <optional>    // for optional
#include <vector>      // for vector

#include <gtk/gtk.h>  // for GtkAdjustment

#include "LayoutMapper.h"  // for LayoutMapper

class XojPageView;
class XournalView;
class ScrollHandling;
class Range;

namespace xoj::util {
template <typename T>
struct Point;
template <typename T>
class Rectangle;
};  // namespace xoj::util

/**
 * @brief The Layout manager for the XournalWidget
 *
 * This class manages the layout of the XojPageView's contained
 * in the XournalWidget
 */
class Layout final {
public:
    Layout(XournalView* view, ScrollHandling* scrollHandling);

public:
    // Todo(Fabian): move to ScrollHandling also it must not depend on Layout
    /**
     * Increases the adjustments by the given amounts
     */
    void scrollRelative(double x, double y);

    // Todo(Fabian): move to ScrollHandling also it must not depend on Layout
    /**
     * Changes the adjustments by absolute amounts (for pinch-to-zoom)
     */
    void scrollAbs(double x, double y);

    // Todo(Fabian): move to XournalView
    /**
     * Changes the adjustments in such a way as to make sure that
     * the given Rectangle is visible
     *
     * @remark If the given Rectangle won't fit into the scrolled window
     *         then only its top left corner will be visible
     */
    void ensureRectIsVisible(int x, int y, int width, int height);

    /**
     * Scrolls so the given Rectangle is visible with its top edge always at the same place in the
     * viewport, whatever the view was showing beforehand.
     *
     * ensureRectIsVisible() scrolls the smallest distance that works, which makes the result depend
     * on the direction of travel: gtk_adjustment_clamp_page() aligns the rectangle's bottom when it
     * lies below the viewport and its top when it lies above. For a rectangle taller than the
     * viewport both rules fire and the top wins, so paging through a document normally looks
     * stable. But once a page is shorter than the viewport - a fullscreen window, a zoomed out view
     * - the very same page settles (viewport height - rect height - 15) pixels apart depending on
     * whether it was reached from an earlier or a later page.
     *
     * Page navigation should land in the same place no matter the route taken, so it uses this.
     * Dragging a selection past the edge of the window still wants minimal scrolling, and keeps
     * using ensureRectIsVisible().
     */
    void scrollRectToTop(int x, int y, int width);

    /// Returns the height of the entire Layout - including centering padding and bottom scroll reserve
    int getTotalPixelHeight() const;

    /// Returns the height of the entire Layout - excluding centering padding
    int getMinimalPixelHeight() const;

    /// Returns the width of the entire Layout - including centering padding
    int getTotalPixelWidth() const;

    /// Returns the width of the entire Layout - excluding centering padding
    int getMinimalPixelWidth() const;

    // Todo(Fabian): move to XournalView this must not depend on Layout directly
    /**
     * Returns the Rectangle which is currently visible - in pixel coordinates
     */
    xoj::util::Rectangle<double> getVisibleRect();


    /// recalculate and resize Layout
    void recalculate();

    /**
     * Recompute the centering paddings (to center the content if the allocation is too big)
     * @params the size of the GtkAllocation of the GtkXournal instance - or -1 for computation from the GtkAdjustments
     */
    void recomputeCenteringPadding(int allocWidth = -1, int allocHeight = -1);

    // Todo(Fabian): move to View:
    /**
     * Updates the current XojPageView. The XojPageView is selected based on
     * the percentage of the visible area of the XojPageView relative
     * to its total area.
     */
    void updateVisibility();

    /**
     * Return the pageview containing coordinates (in pixel coordinates)
     */
    XojPageView* getPageViewAt(int x, int y) const;

    /**
     * Return the page index found (or std::nullopt if not found) when moving by the given offsets from the ref page
     * Assumes refPageNumber is a valid page index.
     */
    std::optional<size_t> getPageWithRelativePosition(size_t refPageNumber, int columnOffset, int rowOffset) const;

    /**
     * Get the fixed padding (in pixels) on the left and above the given point.
     * The return values do not take the centering padding into account
     *
     * @param ref The reference point, in pixel coordinates
     */
    xoj::util::Point<int> getFixedPaddingBeforePoint(const xoj::util::Point<double>& ref) const;

    /// Get the zoom-dependent padding, added to center the page when zoomed out
    xoj::util::Point<int> getCenteringPadding() const;

    /// Returns a list of the indices of the visible pages
    std::vector<size_t> getVisiblePages() const;

    xoj::util::Point<int> getPixelCoordinatesOfEntry(xoj::util::Point<int> gridCoords) const;
    xoj::util::Point<int> getPixelCoordinatesOfEntry(size_t n) const;

    /**
     * Execute the given function for each entry that intersects the range. entryIndex is the entry, intersection is the
     * intersection (in pixel coordinates) and pixelPosition is the entry's upper left corner in pixel coordinates
     */
    void forEachEntriesIntersectingRange(
            const Range& rg,
            std::function<void(size_t entryIndex, const Range& intersection, xoj::util::Point<int> pixelPosition)> fun)
            const;

protected:
    /// Same as above but does not lock the mutex
    xoj::util::Point<int> getPixelCoordinatesOfEntryUnsafe(xoj::util::Point<int> gridCoords) const;
    /// Same as above but does not lock the mutex
    xoj::util::Point<int> getPixelCoordinatesOfEntryUnsafe(size_t n) const;

    /// Same as above but does not lock the mutex
    int getTotalPixelWidthUnsafe() const;
    /// Same as above but does not lock the mutex
    int getMinimalPixelWidthUnsafe() const;
    /// Same as above but does not lock the mutex
    int getTotalPixelHeightUnsafe() const;
    /// Same as above but does not lock the mutex
    int getMinimalPixelHeightUnsafe() const;
    /// Same as above but does not lock the mutex
    void recomputeCenteringPaddingUnsafe(int allocWidth, int allocHeight);

    /**
     * How many pixels of scrolling room the layout needs past its last row, so that scrollRectToTop()
     * can put the last page where it puts every other page.
     *
     * A row taller than the viewport is followed by enough of itself for the scroll position that
     * aligns its top to be reachable, so this is 0 - the usual case, and the layout is untouched.
     * A shorter last row is not: GTK stops the scroll at the bottom of the layout, and the last page
     * comes to rest that much lower in the viewport than any other page. Reserving the difference is
     * what lets it travel the whole way.
     *
     * @param allocHeight The height of the viewport, in pixels
     */
    int computeBottomScrollReserveUnsafe(int allocHeight) const;

    /// Convert pixel-coordinates to the grid position containing them
    GridPosition getGridPositionAtUnsafe(const xoj::util::Point<double>& p) const;

    // Todo(Fabian): move to ScrollHandling also it must not depend on Layout
    static void horizontalScrollChanged(GtkAdjustment* adjustment, Layout* layout);
    static void verticalScrollChanged(GtkAdjustment* adjustment, Layout* layout);

    /**
     * The adjustment was reconfigured: bounds or page size, not the value. The scrolled window
     * does this on every allocation, and "value-changed" stays silent for it. Page visibility is
     * computed from the page size, so it has to be recomputed here too -- otherwise a view whose
     * value never changes afterwards (presentation mode pins it) keeps the visibility that was
     * computed against a zero-sized viewport during setup, every page stays flagged invisible,
     * and the in-progress stroke draws into a mask sized to that empty rectangle: ink that only
     * appears on pen lift.
     */
    static void adjustmentReconfigured(GtkAdjustment* adjustment, Layout* layout);

private:
    void computePrecalculated();

    void maybeAddLastPage(Layout* layout);

public:
    struct PreCalculated {
        mutable std::mutex m;

        LayoutMapper mapper;

        std::vector<double> widthCols;   ///< In page coordinates - multiply by zoom to get pixels
        std::vector<double> heightRows;  ///< In page coordinates - multiply by zoom to get pixels

        std::vector<double> stretchableHorizontalPixelsAfterColumn;  ///< Stretchable - multiply by zoom to get pixels
        std::vector<double> stretchableVerticalPixelsAfterRow;       ///< Stretchable - multiply by zoom to get pixels
        int paddingLeft;                                             ///< in pixels
        int paddingRight;                                            ///< in pixels
        int paddingTop;                                              ///< in pixels
        int paddingBottom;                                           ///< in pixels

        int horizontalCenteringPadding;  ///< Added before and after if the allocation is too big
        int verticalCenteringPadding;    ///< Added before and after if the allocation is too big

        /// Scrolling room after the last row - see computeBottomScrollReserveUnsafe(). In pixels
        int bottomScrollReserve;
    };

private:
    struct PixelCounter;  ///< Used to get the pixel coordinates of entries

    XournalView* view = nullptr;
    ScrollHandling* scrollHandling = nullptr;

    std::vector<size_t> previouslyVisiblePages;  ///< indexes of pages with XojPageView::isVisible() == true

    PreCalculated pc{};

    /// Used to have only one call when zooming in/out
    enum class DelayStatus { NO_DELAY, DELAY, MUST_RUN_AFTER } delayUpdate = DelayStatus::NO_DELAY;
};
