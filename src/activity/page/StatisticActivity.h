#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "../Menu.h"
#include "state/Statistics.h"

// Forward declaration for Bitmap
class Bitmap;

/**
 * Activity for displaying reading statistics.
 * Shows a grid of books with their reading statistics including:
 * - Reading time
 * - Pages read
 * - Chapters completed
 * - Average time per page
 * - Reading progress with visual progress bar
 */
class StatisticActivity final : public Activity, public Menu {
private:
    TaskHandle_t displayTaskHandle = nullptr;
    SemaphoreHandle_t renderingMutex = nullptr;

    int bookSelectorIndex = 0;        // Current page index (2 books per page)
    bool updateRequired = false;

    std::vector<BookReadingStats> allBooksStats;

    const std::function<void()> onGoToRecent;
    const std::function<void()> onSyncOpen;

    /**
     * Loads and sorts reading statistics for all books by most recently read.
     */
    void loadStats();

    /**
     * Formats a time duration in milliseconds to a human-readable string.
     */
    std::string formatTime(uint32_t milliseconds) const;

    /**
     * Renders a book cover or placeholder at the specified position.
     */
    void renderCover(const std::string& bookPath, int x, int y, int width, int height, const std::string& title, const std::string& author) const;

    /**
     * Static task trampoline for the display update task.
     */
    static void taskTrampoline(void* param);

    /**
     * Background task loop that handles display updates.
     */
    void displayTaskLoop();

    /**
     * Renders the complete statistics view.
     */
    void render() const;

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
        bookSelectorIndex(0),
        updateRequired(false) {
        tabSelectorIndex = 4;  // Statistics tab index
    };

    void onEnter() override;
    void onExit() override;
    void loop() override;
};