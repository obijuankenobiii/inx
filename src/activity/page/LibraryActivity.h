#pragma once

/**
 * @file LibraryActivity.h
 * @brief Public interface and types for LibraryActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Activity.h"
#include "../Menu.h"
#include "state/BookTags.h"
#include "state/RecentBooks.h"
#include "system/UiTheme.h"

/**
 * @brief Forward declaration of temporary book entry structure
 */
struct TempBookEntry;

/**
 * @brief Activity for browsing and managing the book library
 *
 * Supports folder navigation and book list views with sorting options.
 * Provides pagination for efficient handling of large libraries.
 */
class LibraryActivity final : public Activity, public Menu {
 public:
  /**
   * @brief Represents an item in the library (folder or book)
   */
  struct LibraryItem {
    enum class Type { FOLDER, BOOK };

    Type type;                ///< Type of the item (folder or book)
    std::string name;         ///< Raw name of the item
    std::string displayName;  ///< Formatted display name
    std::string path;         ///< Full filesystem path
    std::string folderPath;   ///< Parent folder path (for books)
  };

  /**
   * @brief Display mode for the library
   */
  enum class ViewMode {
    FOLDER_VIEW,     ///< Hierarchical folder navigation
    BOOK_LIST_VIEW,  ///< Flat list of all books
    TAG_VIEW,        ///< Indexed books grouped into user tag collections
    SHELF_VIEW       ///< Cover-first shelf view
  };

  /**
   * @brief Sorting modes for library items
   */
  enum class SortMode {
    TITLE_AZ,    ///< Title ascending A-Z (favorites first)
    TITLE_ZA,    ///< Title descending Z-A (favorites first)
    GROUP_AZ,    ///< Group ascending A-Z, then title A-Z (favorites first)
    GROUP_ZA,    ///< Group descending Z-A, then title A-Z (favorites first)
    READING_AZ,  ///< Reading status first (reading > unfinished > completed), then title A-Z
    READING_ZA,  ///< Reading status first (reading > unfinished > completed), then title Z-A
    TAG_AZ,      ///< User tag/category A-Z, then title A-Z
    TAG_ZA       ///< User tag/category Z-A, then title A-Z
  };

  static constexpr int LIST_ITEM_HEIGHT = UiTheme::DRAWER_LIST_ITEM_HEIGHT;  ///< Height of list items in folder view
  static constexpr int FOLDER_ICON_WIDTH = 16;                               ///< Width of folder icon
  static constexpr int FOLDER_ICON_SPACING = 20;                             ///< Spacing for folder icons
  static constexpr int BOOK_ITEMS_PER_PAGE = 9;                              ///< Items per page for book view
  static constexpr int FOLDER_ITEMS_PER_PAGE = 9;                            ///< Items per page for folder view
  static constexpr int GRID_ITEMS_PER_PAGE = 12;                             ///< Items per page for grid folder view
  static constexpr int SHELF_ITEMS_PER_PAGE = 9;  ///< Items per page for shelf view (3x3 grid)
  static constexpr int GRID_ICON_SIZE = 150;      ///< Icon frame size for grid folders

  /**
   * @brief Construct a new Library Activity
   *
   * @param renderer Graphics renderer for display output
   * @param mappedInput Input manager for handling button presses
   * @param onGoToRecent Callback for returning to home screen
   * @param onSelectBook Callback for when a book is selected to open
   * @param onRecentOpen Callback for opening recent books tab
   * @param onSettingsOpen Callback for opening settings tab
   * @param initialPath Starting directory path (default: "/")
   */
  explicit LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                           const std::function<void()>& onGoToRecent,
                           const std::function<void(const std::string& path)>& onSelectBook,
                           const std::function<void()>& onRecentOpen, const std::function<void()>& onSettingsOpen,
                           const std::string& initialPath = "/");
  /**
   * @brief Destroy the Library Activity, stopping the display task and releasing resources
   */
  ~LibraryActivity() override;

  /**
   * @brief Pixel size of a shelf-mode cover slot for the current screen (matches renderShelfCard's
   * geometry). Lets ThumbnailGeneratorActivity pre-populate the on-disk display cache for a book's
   * thumbnail at the exact size the shelf grid will request, so the first shelf render after
   * "Generate Thumbnails" is a cache hit instead of a fresh decode+dither.
   */
  static void getShelfCoverSize(GfxRenderer& renderer, int& outCoverW, int& outCoverH);

  /**
   * @brief Called when entering the activity
   */
  void onEnter() override;

  /**
   * @brief Called when exiting the activity
   */
  void onExit() override;

  /**
   * @brief Main loop for handling user input
   */
  void loop() override;

  /**
   * @brief Load library items from index file (optimized mode)
   */
  void loadLibraryFromIndex();
  /** Loads cached tag entries into cachedTagEntries_ if not already loaded. */
  void ensureTagEntriesLoaded();
  /** Returns the cached tag key for a book path, or an empty string if none is cached. */
  std::string findCachedTag(const std::string& path) const;

  /**
   * @brief Get book comparator for reading status sorting
   * @param ascending Whether to sort titles A-Z or Z-A
   * @return Comparator function for TempBookEntry
   */
  std::function<bool(const TempBookEntry&, const TempBookEntry&)> getReadingStatusComparator(bool ascending) const;

 private:
  TaskHandle_t displayTaskHandle = nullptr;  ///< Handle for display update task
  TaskHandle_t initialLoadTaskHandle_ = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;  ///< Mutex for render thread safety
  bool halfRefreshOnLoadApplied_ = false;
  volatile bool displayTaskStopRequested_ = false;
  volatile bool isIndexing_ = false;
  volatile bool libraryIndexReloadRequested_ = false;
  volatile int indexingProgress_ = 0;
  volatile int indexingTotal_ = 0;
  /** True from onEnter() until the initial book scan/index load finishes; render() shows a lightweight
   * "Loading library" placeholder instead of blocking with a blank screen while that load runs. */
  volatile bool isInitialLoading_ = false;

  std::string savedFolderPath;  ///< Saved path when switching views
  std::string basepath;         ///< Current browsing path
  std::string selectedTagKey_;  ///< Current tag collection key in tag view; empty means tag list
  int selectorIndex = 0;        ///< Currently selected item index
  int listScrollOffset = 0;     ///< Scroll offset for list rendering
  bool updateRequired = false;  ///< Flag to trigger re-render
  bool favoritesPromoted = true;

  bool isHeaderButtonSelected = false;      ///< Whether header button is selected
  bool isIndexButtonSelected = false;       ///< Whether library index refresh button is selected
  bool isSortButtonSelected = false;        ///< Whether sort button is selected
  bool favoriteLongPressProcessed = false;  ///< Flag to prevent duplicate favorite toggles
  bool backLongPressProcessed_ = false;     ///< Prevents Back long-press filter picker from firing twice
  bool letterPickerVisible_ = false;        ///< Whether the library letter filter picker is open
  bool letterPickerIgnoreBackRelease_ = false;
  int letterPickerPage_ = 0;      ///< 0=A-I, 1=J-R, 2=S-Z
  int letterPickerIndex_ = 0;     ///< Selected cell in the picker; 9 is centered All
  char libraryLetterFilter_ = 0;  ///< 0 = no letter filter

  /** Millis deadline for next Down repeat while held (0 = not repeating). */
  unsigned long libraryListDownNextMs = 0;
  /** Millis deadline for next Up repeat while held (0 = not repeating). */
  unsigned long libraryListUpNextMs = 0;

  int itemsPerPage;                           ///< Dynamic items per page based on view mode
  int currentPage;                            ///< Current page index (0-based)
  int totalPages;                             ///< Total number of pages
  std::vector<LibraryItem> currentPageItems;  ///< Items for current page
  std::vector<LibraryItem> cachedLibraryItems_;
  bool cachedLibraryItemsValid_ = false;
  mutable std::unordered_map<std::string, uint8_t> bookStateCache_;
  std::unordered_map<std::string, bool> directoryHasBooksCache_;
  mutable std::unordered_map<std::string, std::string> shelfImagePathCache_;

  // Shelf mode decodes up to SHELF_ITEMS_PER_PAGE real cover thumbnails, each a full JPEG/PNG decode
  // costing well over a second - re-decoding all of them on every selection move (just moving the
  // cursor, not changing page/folder) would make the screen feel frozen. Same fix as
  // RecentActivity's recentPageBuffer_: snapshot the fully-rendered (unselected) framebuffer once,
  // then on a plain selection change, restore that snapshot (a memcpy, no decoding) and redraw only
  // the newly-selected card on top instead of the whole page.
  mutable uint8_t* libraryShelfPageBuffer_ = nullptr;
  mutable bool libraryShelfPageBufferStored_ = false;
  mutable int libraryShelfPageBufferPage_ = -1;
  mutable int libraryShelfPageBufferItemCount_ = -1;
  mutable std::string libraryShelfPageBufferKey_;  ///< basepath or selectedTagKey_, whichever is active
  mutable bool suppressShelfSelectionHighlight_ = false;
  /** Set when leaving shelf mode; render() follows its next normal refresh with a half refresh to
   * clear ghosting left behind by the dithered cover thumbnails. */
  mutable bool pendingShelfExitHalfRefresh_ = false;

  std::vector<BookTags::Entry> cachedTagEntries_;
  bool cachedTagEntriesLoaded_ = false;

  const std::function<void()> onGoToRecent;                         ///< Callback to go to recent books
  const std::function<void(const std::string& path)> onSelectBook;  ///< Callback to open a book
  const std::function<void()> onRecentOpen;                         ///< Callback to open recent tab
  const std::function<void()> onSettingsOpen;                       ///< Callback to open settings tab

  ViewMode currentViewMode;  ///< Current display mode
  SortMode currentSortMode;  ///< Current sorting mode

  /**
   * @brief Navigate to selected menu item (Recent or Settings)
   */
  void navigateToSelectedMenu() override {
    if (tabSelectorIndex == 0) onRecentOpen();
    if (tabSelectorIndex == 2) onSettingsOpen();
  }

  /**
   * @brief Toggle between folder view and book list view
   */
  void toggleViewMode();
  /** Switches the current view mode to the flat book list view. */
  void switchToBookListView();

  /**
   * @brief Switch to folder view mode
   */
  void switchToFolderView();
  /** Switches the current view mode to the tag collection view. */
  void switchToTagView();
  /** Switches the current view mode to the cover shelf view. */
  void switchToShelfView();
  /** Frees the shelf page buffer and flags a cleanup half refresh if currently in shelf mode; call
   * before reassigning currentViewMode away from SHELF_VIEW. */
  void leaveShelfViewIfNeeded();
  /** Starts a background library indexing pass. */
  void startLibraryIndexing();
  /** Returns whether the index refresh button should be shown for the current view. */
  bool shouldShowIndexButton() const;
  /** Restores the current selection to the item matching the given path, if present. */
  bool restoreSelectionToPath(const std::string& path);
  /** Restores the current selection to the item matching the given tag key, if present. */
  bool restoreSelectionToTag(const std::string& tagKey);

  /**
   * @brief Reset navigation state (selection, scroll, page)
   */
  void resetNavigation();

  /**
   * @brief Go to the next page
   */
  void goToNextPage();

  /**
   * @brief Go to the previous page
   */
  void goToPreviousPage();

  /**
   * @brief Handle page up/down navigation
   * @param upPressed Whether up button was pressed
   * @param downPressed Whether down button was pressed
   * @param itemCount Number of items in current list
   * @return true if page was changed
   */
  bool handlePageNavigation(bool wantUpStep, bool wantDownStep, int itemCount);

  /**
   * @brief Handle favorite marking on long press
   * @param itemCount Number of items in current list
   */
  void handleFavoriteLongPress(int itemCount);

  /**
   * @brief Handle selection navigation (up/down) in the list
   * @param upPressed Whether up button was pressed
   * @param downPressed Whether down button was pressed
   * @param itemCount Number of items in current list
   */
  void handleSelectionNavigation(bool wantUpStep, bool wantDownStep, int itemCount);

  /**
   * @brief Handle button selection navigation (left/right)
   * @param leftPressed Whether left button was pressed
   * @param rightPressed Whether right button was pressed
   */
  void handleButtonSelectionNavigation(bool leftPressed, bool rightPressed);

  /**
   * @brief Handle confirm action (select item or button)
   * @param itemCount Number of items in current list
   */
  void handleConfirmAction(int itemCount);

  /**
   * @brief Handle back button navigation
   */
  void handleBackNavigation();
  /** Opens the 3x3 letter filter picker. */
  void openLetterFilterPicker();
  /** Closes the letter filter picker without changing the active filter. */
  void closeLetterFilterPicker();
  /** Handles input while the letter filter picker is visible. */
  void handleLetterFilterPickerInput();
  /** Applies the selected letter filter and reloads the current library mode. */
  void applyLetterFilterSelection();
  /** Returns the picker cell's letter; '*' means all letters/no filter. */
  char letterForPickerCell(int page, int index) const;
  /** First ASCII letter in a display name, or 0 when none exists. */
  char leadingLibraryLetter(const std::string& value) const;
  /** Whether a rendered library item passes the active letter filter. */
  bool itemMatchesLetterFilter(const LibraryItem& item) const;
  /** Whether a temporary book entry passes the active letter filter. */
  bool tempBookMatchesLetterFilter(const TempBookEntry& entry) const;

  /**
   * @brief Check if a file is a valid book file
   * @param filename The filename to check
   * @return true if valid book file extension
   */
  bool isValidBookFile(const std::string& filename) const;

  /**
   * @brief Check if a file should be skipped during scanning
   * @param name File or directory name
   * @return true if should be skipped
   */
  bool shouldSkipFile(const char* name) const;

  /**
   * @brief Check if a directory contains any books
   * @param path Directory path to check
   * @return true if directory contains books
   */
  bool directoryHasBooks(const std::string& path);

  /**
   * @brief Count total books without storing them (fast scan)
   * @param path Starting directory path
   * @return Total number of books found
   */
  int countTotalBooks(const std::string& path);

  /**
   * @brief Find books with pagination - stops when enough books are found
   * @param path Starting directory path
   * @param books Vector to populate with found books
   * @param startIndex Skip this many books before adding
   * @param count Maximum number of books to add
   * @param foundCount Running count of books found (updated during scan)
   * @param stop Flag to stop scanning when enough books are found
   */
  void findBooksPaginated(const std::string& path, std::vector<LibraryItem>& books, int startIndex, int count,
                          int& foundCount, bool& stop);

  /**
   * @brief Check if a book is marked as favorite
   * @param path Path to the book
   * @return true if book is favorite
   */
  bool isBookMarked(const std::string& path) const;

  /**
   * @brief Check if a book is currently being read
   * @param path Path to the book
   * @return true if book is opened/reading
   */
  bool isBookOpened(const std::string& path) const;

  /**
   * @brief Check if a book is marked as finished
   * @param path Path to the book
   * @return true if book is finished
   */
  bool isBookFinished(const std::string& path) const;

  /**
   * @brief Create a book item from file information
   * @param fullPath Full path to the book
   * @param filename Name of the book file
   * @param parentPath Parent directory path
   * @return LibraryItem representing the book
   */
  LibraryItem createBookItem(const std::string& fullPath, const std::string& filename,
                             const std::string& parentPath) const;

  /**
   * @brief Create a folder item from directory information
   * @param name Folder name
   * @param fullPath Full path to the folder
   * @return LibraryItem representing the folder
   */
  LibraryItem createFolderItem(const std::string& name, const std::string& fullPath) const;

  /**
   * @brief Create a temporary book entry for sorting
   * @param fullPath Full path to the book
   * @param filename Name of the book file
   * @param parentPath Parent directory path
   * @return TempBookEntry structure
   */
  TempBookEntry createTempBookEntry(const std::string& fullPath, const std::string& filename,
                                    const std::string& parentPath) const;

  /**
   * @brief Load all books recursively with pagination
   */
  void loadAllBooksRecursive();
  /** Loads all books recursively; assumes the caller already holds renderingMutex. */
  void loadAllBooksRecursiveLocked();
  /**
   * @brief Show a "Loading library" placeholder, then run loadAllBooksRecursive() on a background
   * task instead of blocking the caller - used by onEnter() and switchToShelfView() so a full
   * folder/index scan (and shelf's cover thumbnail decodes right after) doesn't leave the screen
   * frozen with no feedback.
   */
  void beginLibraryLoadWithLoadingScreen();
  /** Reapplies pagination bounds and refreshes currentPageItems from cachedLibraryItems_. */
  void applyPaginationToCachedItems();
  /** Clears the cached library items so they are reloaded on next access. */
  void invalidateLibraryCache();
  /** Returns the cached reading/favorite state flags for a book path. */
  uint8_t getBookStateFlags(const std::string& path) const;
  /** Returns the on-disk display cache path for a book's shelf thumbnail. */
  std::string getShelfImagePath(const std::string& bookPath) const;

  /**
   * @brief Load books using recursive scan for book list view
   */
  void loadBooksRecursiveScan();

  /**
   * @brief Load folders and books for current directory view
   */
  void loadFoldersAndBooksCurrentDirectory();

  /**
   * @brief Load books from index file for book list view
   * @param idxFile Open index file
   * @param cleanBase Cleaned base path
   */
  void loadBooksFromIndex(FsFile& idxFile, const std::string& cleanBase);

  /**
   * @brief Load folders from index file for folder view
   * @param idxFile Open index file
   * @param cleanBase Cleaned base path
   */
  void loadFoldersFromIndex(FsFile& idxFile, const std::string& cleanBase);

  /**
   * @brief Read a book entry from the index file
   * @param idxFile Open index file
   * @return TempBookEntry structure
   */
  TempBookEntry readBookEntryFromIndex(FsFile& idxFile);

  /**
   * @brief Read a directory entry from the index file
   * @param idxFile Open index file
   * @return LibraryItem representing the directory
   */
  LibraryItem readDirectoryEntryFromIndex(FsFile& idxFile);

  /**
   * @brief Skip a directory marker in the index file
   * @param idxFile Open index file
   */
  void skipDirectoryMarker(FsFile& idxFile);

  /**
   * @brief Check if a folder should be included in the current view
   * @param folderPath Path to the folder
   * @param cleanBase Cleaned base path
   * @return true if folder should be included
   */
  bool shouldIncludeFolder(const std::string& folderPath, const std::string& cleanBase) const;

  /**
   * @brief Sort temporary books based on current sort mode
   * @param tempBooks Vector of temporary book entries to sort
   */
  void sortTempBooks(std::vector<TempBookEntry>& tempBooks);

  /**
   * @brief Get the appropriate book comparator for current sort mode
   * @return Comparator function for TempBookEntry
   */
  std::function<bool(const TempBookEntry&, const TempBookEntry&)> getBookComparator() const;

  /**
   * @brief Get folder comparator for current sort mode
   * @return Comparator function for LibraryItem folders
   */
  std::function<bool(const LibraryItem&, const LibraryItem&)> getFolderComparator() const;

  /**
   * @brief Sort folders and books for directory view
   * @param tempFolders Vector of folder items to sort
   * @param tempBooks Vector of temporary book entries to sort
   */
  void sortFoldersAndBooks(std::vector<LibraryItem>& tempFolders, std::vector<TempBookEntry>& tempBooks);

  /**
   * @brief Combine folders and books and apply pagination
   * @param tempFolders Sorted vector of folder items
   * @param tempBooks Sorted vector of temporary book entries
   */
  void combineAndPaginateItems(const std::vector<LibraryItem>& tempFolders,
                               const std::vector<TempBookEntry>& tempBooks);

  /**
   * @brief Apply pagination to sorted books and load current page
   * @param tempBooks Sorted vector of temporary book entries
   */
  void applyPaginationToBooks(const std::vector<TempBookEntry>& tempBooks);

  /**
   * @brief Cycle through sort modes
   */
  void cycleSortMode();

  /**
   * @brief Reset the library view state
   */
  void resetLibraryView();

  /**
   * @brief Format a folder name by replacing underscores with spaces and capitalizing words
   * @param name The raw folder name
   * @return Formatted folder name
   */
  std::string formatFolderName(const std::string& name) const;

  /**
   * @brief Extract base filename without extension and remove leading numbers
   * @param filename The full filename
   * @return Cleaned base filename
   */
  std::string getBaseFilename(const std::string& filename) const;

  /**
   * @brief Extract folder name from a path
   * @param path The full path
   * @return The last component of the path
   */
  std::string extractFolderName(const std::string& path) const;

  /**
   * @brief Truncate text with ellipsis if it exceeds max length
   * @param text The text to truncate
   * @param maxLength Maximum allowed length
   * @return Truncated text
   */
  std::string truncateTextIfNeeded(const std::string& text, size_t maxLength) const;

  /**
   * @brief Get the header text based on current path and view mode
   * @return Header text string
   */
  std::string getHeaderText() const;

  /**
   * @brief Get the text for the sort button based on current sort mode
   * @return Sort button text
   */
  std::string getSortButtonText() const;

  /**
   * @brief Task trampoline for display task
   * @param param Pointer to LibraryActivity instance
   */
  static void taskTrampoline(void* param);

  /**
   * @brief Display task loop that handles periodic rendering
   */
  void displayTaskLoop();

  /**
   * @brief Render the library screen
   */
  void render() const;
  /** Draws the 3x3 letter filter picker overlay. */
  void renderLetterFilterPicker() const;

  /**
   * @brief Render the library list
   * @param startY Starting Y position for the list
   */
  void renderLibraryList(int startY) const;
  int librarySubheadingHeight() const;
  int renderLibrarySubheading(int startY) const;
  /** Renders the cover shelf grid view. */
  void renderLibraryShelf(int startY) const;
  /**
   * @brief Render a single shelf card (cover thumbnail + selection state), no other cards touched.
   * Shared by the full renderLibraryShelf() pass and the fast selection-only overlay redraw.
   */
  void renderShelfCard(int index, int startY, bool selected) const;
  /** Returns whether the stored shelf page buffer can be reused for the current page/selection. */
  bool canUseLibraryShelfBuffer() const;
  /** Snapshots the current framebuffer into the shelf page buffer. */
  bool storeLibraryShelfBuffer() const;
  /** Restores the framebuffer from the stored shelf page buffer. */
  bool restoreLibraryShelfBuffer() const;
  /** Frees the stored shelf page buffer, if any. */
  void freeLibraryShelfBuffer() const;
  /** Draws the selection highlight overlay on top of the restored shelf buffer. */
  void drawShelfSelectionOverlay(int startY) const;

  /**
   * @brief Render the folder browser as a 3x4 icon grid
   * @param startY Starting Y position for the grid
   */
  void renderLibraryGrid(int startY) const;

  /**
   * @brief Whether the current folder browser should use the 3x4 grid layout
   */
  bool isLibraryGridMode() const;
  /** Returns whether the current view mode is the tag collection view. */
  bool isTagViewMode() const;

  /**
   * @brief Render a large built-in icon for a grid item
   */
  void renderGridItemIcon(const LibraryItem& item, int x, int y, int w, int h, bool isSelected, bool isLarge) const;

  /**
   * @brief Render the icon for a list item
   * @param item The library item
   * @param drawY Y position to draw at
   * @param itemHeight Height of the item
   * @param isSelected Whether the item is selected
   */
  void renderItemIcon(const LibraryItem& item, int drawY, int itemHeight, bool isSelected) const;
  void renderBookListBadges(const LibraryItem& item, int drawY, int itemHeight, bool isSelected, int screenWidth) const;

  /**
   * @brief Render the text for a list item
   * @param item The library item
   * @param drawY Y position to draw at
   * @param itemHeight Height of the item
   * @param isSelected Whether the item is selected
   * @param screenWidth Width of the screen
   */
  void renderItemText(const LibraryItem& item, int drawY, int itemHeight, bool isSelected, int screenWidth) const;

  /**
   * @brief Draw the header button
   * @param text Button text
   * @param headerY Y position of header
   * @param headerHeight Height of header
   * @param rightX Right X boundary
   * @param isSelected Whether button is selected
   * @return Next button X position
   */
  int drawHeaderButton(const std::string& text, int headerY, int headerHeight, int rightX, bool isSelected) const;
  /** Draws the library index refresh button and returns the next available X position. */
  int drawIndexButton(int headerY, int headerHeight, int x, bool isSelected) const;

  /**
   * @brief Draw the sort button
   * @param headerY Y position of header
   * @param headerHeight Height of header
   * @param rightX Right X boundary
   * @return Next button X position
   */
  int drawSortButton(int headerY, int headerHeight, int rightX) const;

  /**
   * @brief Draw button hints at the bottom of the screen
   */
  void drawButtonHints() const;

  /**
   * @brief Get the height of a list item based on its type
   * @param item The library item
   * @return Height in pixels
   */
  int getItemHeight(const LibraryItem& item) const;
};
