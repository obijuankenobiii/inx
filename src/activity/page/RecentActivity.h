#pragma once

/**
 * @file RecentActivity.h
 * @brief Public interface and types for RecentActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "../Menu.h"
#include "state/RecentBooks.h"

/**
 * Activity that displays recently opened books in either grid or list view.
 * Shows book covers, titles, authors, and reading progress.
 */
class RecentActivity final : public Activity, public Menu {
 public:
  static constexpr int MAX_RECENT_BOOKS = 8;
  static constexpr int GRID_COLS = 2;

  static constexpr int COVER_WIDTH = 200;
  static constexpr int COVER_HEIGHT = 250;

  static constexpr int GRID_SPACING = 20;
  static constexpr int GRID_ITEM_MARGIN = 10;

  static constexpr int GRID_ITEM_WIDTH = COVER_WIDTH;
  static constexpr int GRID_ITEM_HEIGHT = COVER_HEIGHT + GRID_ITEM_MARGIN * 2 + 26;

  static constexpr int LIST_VISIBLE_ITEMS = 5;

  /**
   * View mode enumeration for displaying recent books.
   */
  enum class ViewMode {
    Default,
    Grid,  /**< Display books in a grid with covers */
    Flow   /**< Display books in a list with thumbnails */
  };

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool taskRunning = false;
  bool halfRefreshOnLoadApplied_ = false;

  int selectorIndex = 0;
  bool updateRequired = false;
  bool bookSelected = false;
  bool statsSectionSelected = false; 
  int scrollOffset = 0;
  int scrollOffsetDefault = 0;

  std::vector<RecentBook> recentBooks;

  const std::function<void()> onLibraryOpen;
  const std::function<void(const std::string& path)> onSelectBook;
  const std::function<void()> onGoToStatistics;
  const std::function<void()> onGoToRecent;

  /**
   * Formats a time duration in milliseconds to a human-readable string.
   * Output formats: "Xd Xh" for days, "Xh Xm" for hours, "Xm" for minutes.
   */
  std::string formatTime(uint32_t milliseconds) const;

  /**
   * Loads recent books from persistent storage.
   * Filters out books that no longer exist on the SD card.
   */
  void loadRecentBooks(bool resetScroll = true);

  /**
   * Static trampoline function for FreeRTOS task creation.
   * 
   * @param param Pointer to RecentActivity instance
   */
  static void taskTrampoline(void* param);

  /**
   * Display task loop that runs in a separate FreeRTOS task.
   * Monitors for state changes and triggers rendering when needed.
   */
  void displayTaskLoop();

  /**
   * Renders a single grid item with cover, title, author and progress.
   * 
   * @param gridX Grid column index
   * @param gridY Grid row index
   * @param startY Starting Y coordinate for the grid
   * @param book Book information to render
   * @param selected Whether this item is selected
   */
  void renderGridItem(int gridX, int gridY, int startY, const RecentBook& book, bool selected);

  /**
   * Renders the default view showing the most recent book with cover and stats.
   * Displays a single book with cover image on the left and reading statistics on the right.
   */
  void renderDefault();

  /**
   * Renders the complete grid view including all visible books.
   * 
   * @param startY Starting Y coordinate for the grid
   */
  void renderGrid(int startY);

  /**
   * Renders the complete grid view including all visible books.
   * 
   * @param startY Starting Y coordinate for the grid
   */
  void renderFlow();
  
  /**
   * Renders a single list item with thumbnail on left, title, author, and progress bar.
   * 
   * @param index Index within the visible list (0-4)
   * @param startY Starting Y coordinate for the list
   * @param book Book information to render
   * @param selected Whether this item is selected
   * @param next Whether to render next book (unused parameter, kept for compatibility)
   */
  void renderListItem(int index, int startY, const RecentBook& book, bool selected, bool next = false);

  struct ThumbnailGrayscaleJob {
    std::string cacheDir;
    int drawX = 0;
    int drawY = 0;
    int drawW = 0;
    int drawH = 0;
  };
  std::vector<ThumbnailGrayscaleJob> thumbnailGrayscaleJobs_;

  void noteThumbnailGrayscaleJob(const std::string& cacheDir, int drawX, int drawY, int drawW, int drawH);
  void runThumbnailGrayscalePassIfNeeded();
  /** Bounding box covering all thumbnails recorded for this frame (reader image grayscale pass). */
  bool getImageScreenRect(int& x, int& y, int& w, int& h) const;

  /**
   * Calculates the number of rows that can be displayed on screen at once.
   * 
   * @return Number of visible rows based on current view mode
   */
  int getVisibleRows() const;

  /**
   * Navigates to the selected tab when tab selector is used.
   * Overridden from Menu.
   */
  void navigateToSelectedMenu() override {
    if (tabSelectorIndex == 1) onLibraryOpen(); 
    if (tabSelectorIndex == 4) onGoToStatistics(); 
  }

  ViewMode currentViewMode = ViewMode::Flow;

 public:
  /**
   * Constructs a new RecentActivity.
   * 
   * @param renderer Graphics renderer for display output
   * @param mappedInput Input manager for handling button presses
   * @param onLibraryOpen Callback for opening library tab
   * @param onGoToStatistics Callback for opening statistics tab
   * @param onSelectBook Callback when a book is selected to open
   * @param onGoToRecent Callback for returning to home screen
   */
  explicit RecentActivity(GfxRenderer& renderer, 
                         MappedInputManager& mappedInput,
                         const std::function<void()>& onLibraryOpen,
                         const std::function<void()>& onGoToStatistics,
                         const std::function<void(const std::string& path)>& onSelectBook,
                         const std::function<void()>& onGoToRecent)
      : Activity("Recent", renderer, mappedInput),
        Menu(),
        onLibraryOpen(onLibraryOpen),
        onSelectBook(onSelectBook),
        onGoToStatistics(onGoToStatistics),
        onGoToRecent(onGoToRecent),
        hasRandomFavorite(false)
     {}

 private:
  bool firstRender = true;

  void onEnter() override;
  void onExit() override;
  void loop() override;

  RecentBook randomFavoriteBook;
  bool hasRandomFavorite;
};