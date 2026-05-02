#pragma once

/**
 * @file StatisticActivity.h
 * @brief Public interface and types for StatisticActivity.
 */

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../Activity.h"
#include "../Menu.h"
#include "state/Statistics.h"


class Bitmap;

/**
 * Activity for displaying reading statistics.
 * First view is a global reading-stats summary; Up/Down steps through one book at a time.
 * On enter, per-book stats are scanned from SD and global totals load from `/.system/statistics.bin`
 * when present. Confirm (Refresh) rescans, recomputes aggregates, and writes that snapshot.
 */
class StatisticActivity final : public Activity, public Menu {
private:
    /** 0 = aggregated global overview; 1..N = one book (index N-1 in allBooksStats). */
    int viewIndex = 0;
    bool updateRequired = false;

    std::vector<BookReadingStats> allBooksStats;
    GlobalReadingStats globalStats;

    const std::function<void()> onGoToRecent;
    const std::function<void()> onSyncOpen;

    /**
     * Loads and sorts reading statistics for all books by most recently read.
     * Refreshes aggregates from disk, updates the global snapshot file, and redraws progress UI.
     */
    void loadStats();

    /** SD scan for per-book stats + load saved global totals (or aggregate if no snapshot yet). */
    void hydrateFromStorage();

    /**
     * Renders a book cover or placeholder at the specified position.
     */
    void renderCover(const std::string& bookPath, int x, int y, int width, int height, const std::string& title, const std::string& author) const;

    /** Global view: draw recent cover with frame sized to bitmap (max 165×182); returns {frameW, frameH}. */
    std::pair<int, int> drawGlobalRecentThumbBlock(int x, int y, const std::string& bookPath,
                                                   const std::string& title) const;

    void render();

    void renderSingleBookView(int bookIdx, int contentTop, int contentBottom) const;

    /** Global overview (view 0): each returns next Y after band height + Margin. */
    int renderHeader(int y, int innerLeft, int innerRight, int innerW, int Margin) const;
    int renderRecent(int y, int innerLeft, int innerRight, int innerW, int Margin) const;
    int renderFirstGrid(int y, int innerLeft, int innerW, int Margin) const;
    int renderGuage(int y, int innerLeft, int innerRight, int Margin) const;
    void renderSecondGrid(int y, int innerLeft, int innerRight, int contentBottom) const;

    /**
     * Navigates to the selected tab.
     * Tab indices: 0 = Home, 3 = Sync
     */
    void navigateToSelectedMenu() override {
        if (tabSelectorIndex == 0 && onGoToRecent) {
            onGoToRecent();
        }
        if (tabSelectorIndex == 3 && onSyncOpen) {
            onSyncOpen();
        }
    }

public:
    /**
     * Constructs a new StatisticActivity.
     *
     * @param renderer Graphics renderer for display output
     * @param mappedInput Input manager for handling user input
     * @param onGoToRecent Callback function for navigating to the home activity
     * @param onSyncOpen Callback function for opening the sync activity (optional)
     */
    explicit StatisticActivity(
        GfxRenderer& renderer,
        MappedInputManager& mappedInput,
        const std::function<void()>& onGoToRecent,
        const std::function<void()>& onSyncOpen = nullptr
    ) :
        Activity("Statistics", renderer, mappedInput),
        Menu(),
        onGoToRecent(onGoToRecent),
        onSyncOpen(onSyncOpen),
        viewIndex(0),
        updateRequired(false) {
        tabSelectorIndex = 4;  
    };

    void onEnter() override;
    void onExit() override;
    void loop() override;
};