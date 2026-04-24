#pragma once

/**
 * @file RecentActivity.h
 * @brief Public interface and types for RecentActivity.
 */

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "../Menu.h"
#include "state/BookState.h"
#include "state/RecentBooks.h"

/**
 * Activity that displays recently opened books in either grid or list view.
 * Shows book covers, titles, authors, and reading progress.
 */
class RecentActivity final : public Activity, public Menu {
 public:
  static constexpr int MAX_RECENT_BOOKS = 8;
  static constexpr int GRID_COLS = 2;

  static constexpr int COVER_WIDTH = 170;
  static constexpr int COVER_HEIGHT = 250;

  static constexpr int GRID_SPACING = 20;
  static constexpr int GRID_ITEM_MARGIN = 10;

  static constexpr int GRID_ITEM_WIDTH = COVER_WIDTH;
  static constexpr int GRID_ITEM_HEIGHT = COVER_HEIGHT + GRID_ITEM_MARGIN * 2 + 26;

  static constexpr int LIST_VISIBLE_ITEMS = 5;

  bool skipLoopDelay() override { return true; }

  /**
   * View mode enumeration for displaying recent books.
   */
  enum class ViewMode {
    Default,
    Grid,  /**< Display books in a grid with covers */
    Flow,  /**< Flow carousel */
    SimpleUi /**< Recent cover on gray band, favorites list below */
  };

 private:
  bool halfRefreshOnLoadApplied_ = false;

  int selectorIndex = 0;
  bool updateRequired = false;
  bool bookSelected = false;
  int scrollOffset = 0;
  int scrollOffsetDefault = 0;
  /** Horizontal window for list-stats top carousel (index of leftmost full tile). */
  int listStatsRecentHScroll = 0;
  int listStatsFavHScroll = 0;
  std::vector<BookState::Book> listStatsFavoriteOnly_;

  std::vector<BookState::Book> simpleUiFavorites_;
  int simpleUiFavScroll_ = 0;

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
  void rebuildListStatsFavorites();
  void rebuildSimpleUiFavorites();

  /** Full redraw when updateRequired; clears flag (same work as former display task). */
  void pumpDisplayFromLoop();

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

  void renderSimpleUi();

  void drawRecentThumbnailAt(int x, int y, int w, int h, const std::string& cacheDir, const std::string& placeholderTitle,
                             int placeholderFontId);
  /** Default list: 2×3 stats grid (vs other visible strip book when both have stats); includes Session + Progress. */
  void renderDefaultStatsGrid(int gridStartY, int screenW);

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
  void drawListStatsStrip(int bandX, int bandY, int bandW, int bandH, int hScroll, int count,
                          const std::function<std::string(int)>& cacheDirAt,
                          const std::function<std::string(int)>& titleAt,
                          const std::function<bool(int)>& selectedAt);

  bool firstRender = true;

  void onEnter() override;
  void onExit() override;
  void loop() override;

  RecentBook randomFavoriteBook;
  bool hasRandomFavorite;

  void clampSimpleUiFavoriteScroll(int maxVisibleFavs);
};