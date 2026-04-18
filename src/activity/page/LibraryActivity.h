#pragma once

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
    FOLDER_VIEW,    ///< Hierarchical folder navigation
    BOOK_LIST_VIEW  ///< Flat list of all books
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
    READING_ZA   ///< Reading status first (reading > unfinished > completed), then title Z-A
  };

  static constexpr int LIST_ITEM_HEIGHT = 60;       ///< Height of list items in folder view
  static constexpr int FOLDER_ICON_WIDTH = 16;      ///< Width of folder icon
  static constexpr int FOLDER_ICON_SPACING = 20;    ///< Spacing for folder icons
  static constexpr int BOOK_ITEMS_PER_PAGE = 9;     ///< Items per page for book view
  static constexpr int FOLDER_ITEMS_PER_PAGE = 10;  ///< Items per page for folder view

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

  /**
   * @brief Get book comparator for reading status sorting
   * @param ascending Whether to sort titles A-Z or Z-A
   * @return Comparator function for TempBookEntry
   */
  std::function<bool(const TempBookEntry&, const TempBookEntry&)> getReadingStatusComparator(bool ascending) const;

 private:
  TaskHandle_t displayTaskHandle = nullptr;    ///< Handle for display update task
  SemaphoreHandle_t renderingMutex = nullptr;  ///< Mutex for render thread safety

  std::string savedFolderPath;  ///< Saved path when switching views
  std::string basepath;         ///< Current browsing path
  int selectorIndex = 0;        ///< Currently selected item index
  int listScrollOffset = 0;     ///< Scroll offset for list rendering
  bool updateRequired = false;  ///< Flag to trigger re-render
  bool favoritesPromoted = true;

  bool isHeaderButtonSelected = false;      ///< Whether header button is selected
  bool isSortButtonSelected = false;        ///< Whether sort button is selected
  bool favoriteLongPressProcessed = false;  ///< Flag to prevent duplicate favorite toggles

  /** Millis deadline for next Down repeat while held (0 = not repeating). */
  unsigned long libraryListDownNextMs = 0;
  /** Millis deadline for next Up repeat while held (0 = not repeating). */
  unsigned long libraryListUpNextMs = 0;

  // Pagination
  int itemsPerPage;                           ///< Dynamic items per page based on view mode
  int currentPage;                            ///< Current page index (0-based)
  int totalPages;                             ///< Total number of pages
  std::vector<LibraryItem> currentPageItems;  ///< Items for current page

  // Callbacks
  const std::function<void()> onGoToRecent;                         ///< Callback to go to recent books
  const std::function<void(const std::string& path)> onSelectBook;  ///< Callback to open a book
  const std::function<void()> onRecentOpen;                         ///< Callback to open recent tab
  const std::function<void()> onSettingsOpen;                       ///< Callback to open settings tab

  // State
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

  /**
   * @brief Switch to folder view mode
   */
  void switchToFolderView();

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
   * @brief Load the current page of items
   */
  void loadPage();

  /**
   * @brief Update pagination based on current item list
   */
  void updatePagination();

  /**
   * @brief Update items per page based on current view mode
   */
  void updateItemsPerPage();

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
   * @brief Count total folders with books without storing them (fast scan)
   * @param path Starting directory path
   * @return Total number of folders with books found
   */
  int countTotalFolders(const std::string& path);

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
   * @brief Find folders with pagination - stops when enough folders are found
   * @param path Starting directory path
   * @param folders Vector to populate with found folders
   * @param startIndex Skip this many folders before adding
   * @param count Maximum number of folders to add
   * @param foundCount Running count of folders found (updated during scan)
   * @param stop Flag to stop scanning when enough folders are found
   */
  void findFoldersPaginated(const std::string& path, std::vector<LibraryItem>& folders, int startIndex, int count,
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
   * @brief Legacy method - use loadAllBooksRecursive instead
   * @deprecated Use loadAllBooksRecursive() instead
   */
  void loadLibraryItems();

  /**
   * @brief Apply sorting to current items
   */
  void applySorting();

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
   * @deprecated Use sortTempBooks instead
   */
  void sortByTitle(bool ascending = true);

  /**
   * @deprecated Use sortTempBooks with GROUP mode instead
   */
  void sortByGroup(bool ascending = true);

  /**
   * @deprecated Use combineAndPaginateItems instead
   */
  void sortFolderViewByType(bool ascending = true);

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

  /**
   * @brief Render the library list
   * @param startY Starting Y position for the list
   */
  void renderLibraryList(int startY) const;

  /**
   * @brief Render the icon for a list item
   * @param item The library item
   * @param drawY Y position to draw at
   * @param itemHeight Height of the item
   * @param isSelected Whether the item is selected
   */
  void renderItemIcon(const LibraryItem& item, int drawY, int itemHeight, bool isSelected) const;

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
   * @brief Update the scroll position based on current selection
   */
  void updateScrollPosition();

  /**
   * @brief Get the height of a list item based on its type
   * @param item The library item
   * @return Height in pixels
   */
  int getItemHeight(const LibraryItem& item) const;

  /**
   * @brief Get the current list of items (const version)
   * @return Reference to current page items
   */
  const std::vector<LibraryItem>& getCurrentList() const { return currentPageItems; }

  /**
   * @brief Get the current list of items (non-const version)
   * @return Reference to current page items
   */
  std::vector<LibraryItem>& getCurrentList() { return currentPageItems; }
};