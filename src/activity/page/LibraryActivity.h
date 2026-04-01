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
 * Activity for browsing and managing the book library.
 * Supports folder navigation and book list views with sorting options.
 */
class LibraryActivity final : public Activity, public Menu {
 public:
  /**
   * Tab identifiers for navigation.
   */
  enum class Tab { Library };

  /**
   * Display mode for the library.
   */
  enum class ViewMode { 
    FOLDER_VIEW,     /**< Hierarchical folder navigation */
    BOOK_LIST_VIEW,  /**< Flat list of all books */
    FAVORITES_VIEW   /**< List of favorite books */
  };

  /**
   * Sorting modes for library items.
   */
  enum class SortMode { 
    TITLE_AZ,   /**< Title ascending A-Z */
    TITLE_ZA,   /**< Title descending Z-A */
    GROUP_AZ,   /**< Group ascending A-Z */
    GROUP_ZA    /**< Group descending Z-A */
  };

  static constexpr int LIST_ITEM_HEIGHT = 60;
  static constexpr int FOLDER_ICON_WIDTH = 16;
  static constexpr int FOLDER_ICON_SPACING = 20;


 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  
  std::string savedFolderPath;
  std::string basepath;
  int selectorIndex = 0;
  int listScrollOffset = 0;
  bool updateRequired = false;

  bool isHeaderButtonSelected = false;
  bool isSortButtonSelected = false;
  bool favoriteLongPressProcessed = false;
  bool isBookMarked(const std::string& path) const;
  bool isBookOpened(const std::string& path) const;

  /**
   * Represents an item in the library (folder or book).
   */
  struct LibraryItem {
    enum class Type { FOLDER, BOOK };

    Type type;
    std::string name;
    std::string displayName;
    std::string path;
    std::string folderPath;
  };

  std::vector<LibraryItem> libraryItems;
  std::vector<LibraryItem> allBooksList;

  const std::function<void()> onGoToRecent;
  const std::function<void(const std::string& path)> onSelectBook;
  const std::function<void()> onRecentOpen;
  const std::function<void()> onSettingsOpen;

  void loadLibraryItems();
  void loadFavorites();
  bool directoryHasBooks(const std::string& path);
  void loadAllBooksRecursive();
  void findBooksRecursively(const std::string& path, std::vector<LibraryItem>& books);
  void updateScrollPosition();
  
  std::string formatFolderName(const std::string& name) const;
  std::string getBaseFilename(const std::string& filename) const;
  std::string extractFolderName(const std::string& path) const;
  std::string truncateTextIfNeeded(const std::string& text, size_t maxLength) const;

  ViewMode currentViewMode;
  void toggleViewMode();
  void switchToFolderView();
  void switchToFavoritesView();

  static void taskTrampoline(void* param);
  void displayTaskLoop();
  void render() const;
  void renderLibraryList(int startY) const;
  
  std::string getHeaderText() const;
  std::string getSortButtonText() const;
  int drawHeaderButton(const std::string& text, int headerY, int headerHeight, int rightX, bool isSelected) const;
  int drawSortButton(int headerY, int headerHeight, int rightX) const;
  void drawButtonHints() const;

  SortMode currentSortMode;
  void applySorting();
  void sortByTitle(bool ascending = true);
  void sortByGroup(bool ascending = true);
  void sortFolderViewByType(bool ascending = true);
  void cycleSortMode();
  void resetLibraryView();

  const std::vector<LibraryItem>& getCurrentList() const;
  std::vector<LibraryItem>& getCurrentList();

  void navigateToSelectedMenu() override {
    if (tabSelectorIndex == 0) onRecentOpen();
    if (tabSelectorIndex == 2) onSettingsOpen();
  }

 public:
  /**
   * Constructs a new LibraryActivity.
   * 
   * @param renderer Graphics renderer for display output
   * @param mappedInput Input manager for handling button presses
   * @param onGoToRecent Callback for returning to home screen
   * @param onSelectBook Callback for when a book is selected to open
   * @param onRecentOpen Callback for opening recent books tab
   * @param onSettingsOpen Callback for opening settings tab
   * @param initialPath Starting directory path
   */
  explicit LibraryActivity(
    GfxRenderer& renderer, 
    MappedInputManager& mappedInput,
    const std::function<void()>& onGoToRecent,
    const std::function<void(const std::string& path)>& onSelectBook,
    const std::function<void()>& onRecentOpen,
    const std::function<void()>& onSettingsOpen,
    const std::string& initialPath = "/"  // Changed to const reference
  );

  void loadLibraryFromIndex();
  void onEnter() override;
  void onExit() override;
  void loop() override;
};