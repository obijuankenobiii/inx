/**
 * @file LibraryActivity.cpp
 * @brief Definitions for LibraryActivity.
 */

#include "LibraryActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <algorithm>
#include <functional>
#include <memory>

#include "images/Book.h"
#include "images/BookLarge.h"
#include "images/FolderLarge.h"

#include "images/Folder.h"
#include "images/Star.h"
#include "images/Refresh.h"
#include "../settings/LibraryIndexer.h"
#include "state/BookState.h"
#include "state/BookTags.h"
#include "state/RecentBooks.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"
#include "util/StringUtils.h"

/**
 * @brief Temporary book entry structure for sorting operations
 */
struct TempBookEntry {
  std::string path;
  std::string displayName;
  std::string folderPath;
  std::string tag;
  std::string sortKey;
  bool isFavorite;
};

using LibraryItem = LibraryActivity::LibraryItem;
using ViewMode = LibraryActivity::ViewMode;
using SortMode = LibraryActivity::SortMode;

namespace {

uint8_t sortModeToStorage(SortMode m) { return static_cast<uint8_t>(m); }

SortMode storageToSortMode(uint8_t v) {
  if (v > static_cast<uint8_t>(SortMode::TAG_ZA)) {
    return SortMode::TITLE_AZ;
  }
  return static_cast<SortMode>(v);
}

uint8_t viewModeToStorage(ViewMode m) {
  switch (m) {
    case ViewMode::BOOK_LIST_VIEW:
      return SystemSetting::LIBRARY_VIEW_BOOKS;
    case ViewMode::TAG_VIEW:
      return SystemSetting::LIBRARY_VIEW_TAGS;
    case ViewMode::FOLDER_VIEW:
    default:
      return SystemSetting::LIBRARY_VIEW_FOLDERS;
  }
}

ViewMode storageToViewMode(uint8_t v, bool indexEnabled) {
  if (v == SystemSetting::LIBRARY_VIEW_BOOKS) {
    return ViewMode::BOOK_LIST_VIEW;
  }
  if (v == SystemSetting::LIBRARY_VIEW_TAGS && indexEnabled) {
    return ViewMode::TAG_VIEW;
  }
  return ViewMode::FOLDER_VIEW;
}

std::string normalizeLibraryPath(std::string path) {
  while (path.length() > 1 && path.back() == '/') {
    path.pop_back();
  }
  return path.empty() ? "/" : path;
}

/**
 * @brief RAII mutex guard for automatic mutex management
 */
class MutexGuard {
 private:
  SemaphoreHandle_t& mutex;
  bool acquired;

 public:
  explicit MutexGuard(SemaphoreHandle_t& m, TickType_t timeout = pdMS_TO_TICKS(100)) : mutex(m), acquired(false) {
    if (mutex) {
      acquired = (xSemaphoreTake(mutex, timeout) == pdTRUE);
    }
  }

  ~MutexGuard() {
    if (acquired && mutex) {
      xSemaphoreGive(mutex);
    }
  }

  bool isAcquired() const { return acquired; }
};


constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long FAVORITE_HOLD_MS = 500;
/** First repeat delay after Down/Up press while browsing the list */
constexpr unsigned long LIB_LIST_REPEAT_INITIAL_MS = 420;
/** Repeat interval while Down/Up held */
constexpr unsigned long LIB_LIST_REPEAT_RATE_MS = 95;
constexpr int LIB_GRID_COLS = 3;
constexpr int LIB_GRID_ROWS = 4;
constexpr int LIB_GRID_GAP_X = 8;
constexpr int LIB_GRID_MIN_GAP_Y = 6;
constexpr int LIB_GRID_OUTER_PAD = 8;
constexpr int LIB_GRID_LABEL_GAP = 4;
constexpr int LIB_GRID_LABEL_H = 28;
constexpr const char* TAG_UNTAGGED_KEY = "\x01";
constexpr const char* TAG_UNTAGGED_LABEL = "Others";

}  

/**
 * @brief Construct a new Library Activity object
 */
LibraryActivity::LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 const std::function<void()>& onGoToRecent,
                                 const std::function<void(const std::string& path)>& onSelectBook,
                                 const std::function<void()>& onRecentOpen, const std::function<void()>& onSettingsOpen,
                                 const std::string& initialPath)
    : Activity("Library", renderer, mappedInput),
      Menu(),
      onGoToRecent(onGoToRecent),
      onSelectBook(onSelectBook),
      onRecentOpen(onRecentOpen),
      onSettingsOpen(onSettingsOpen),
      savedFolderPath(""),
      basepath(initialPath),
      selectedTagKey_(""),
      selectorIndex(0),
      listScrollOffset(0),
      updateRequired(false),
      isHeaderButtonSelected(false),
      isIndexButtonSelected(false),
      isSortButtonSelected(false),
      favoriteLongPressProcessed(false),
      currentViewMode(ViewMode::FOLDER_VIEW),
      currentSortMode(SortMode::TITLE_AZ),
      currentPage(0),
      totalPages(0),
      itemsPerPage(FOLDER_ITEMS_PER_PAGE) {
  tabSelectorIndex = 1;
}

/**
 * @brief Format a folder name by replacing underscores with spaces and capitalizing words
 * @param name The raw folder name
 * @return Formatted folder name
 */
std::string LibraryActivity::formatFolderName(const std::string& name) const {
  std::string formatted = name;

  if (!formatted.empty() && formatted.back() == '/') {
    formatted.pop_back();
  }

  std::replace(formatted.begin(), formatted.end(), '_', ' ');

  bool capitalizeNext = true;
  for (char& c : formatted) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      capitalizeNext = true;
      continue;
    }

    if (capitalizeNext) {
      c = std::toupper(static_cast<unsigned char>(c));
      capitalizeNext = false;
    }
  }

  return formatted;
}

/**
 * @brief Extract base filename without extension and remove leading numbers
 * @param filename The full filename
 * @return Cleaned base filename
 */
std::string LibraryActivity::getBaseFilename(const std::string& filename) const {
  size_t lastSlash = filename.find_last_of('/');
  std::string basename = (lastSlash != std::string::npos) ? filename.substr(lastSlash + 1) : filename;

  size_t lastDot = basename.find_last_of('.');
  if (lastDot != std::string::npos) {
    basename.resize(lastDot);
  }

  if (!basename.empty()) {
    size_t underscorePos = basename.find('_');
    if (underscorePos != std::string::npos && underscorePos > 0) {
      bool allDigits = true;
      for (size_t i = 0; i < underscorePos; i++) {
        if (!std::isdigit(static_cast<unsigned char>(basename[i]))) {
          allDigits = false;
          break;
        }
      }

      if (allDigits) {
        basename = basename.substr(underscorePos + 1);
      }
    }
  }

  std::replace(basename.begin(), basename.end(), '_', ' ');
  return basename;
}

/**
 * @brief Get the header text based on current path and view mode
 * @return Header text string
 */
std::string LibraryActivity::getHeaderText() const {
  if (currentViewMode == ViewMode::TAG_VIEW) {
    if (selectedTagKey_.empty()) {
      return "Categories";
    }
    return selectedTagKey_ == TAG_UNTAGGED_KEY ? TAG_UNTAGGED_LABEL : truncateTextIfNeeded(selectedTagKey_, 25);
  }

  if (basepath == "/") {
    return currentViewMode == ViewMode::BOOK_LIST_VIEW ? "All Books" : "Collection";
  }

  std::string folderName = extractFolderName(basepath);
  std::string header = formatFolderName(folderName);
  return truncateTextIfNeeded(header, 25);
}

/**
 * @brief Extract folder name from a path
 * @param path The full path
 * @return The last component of the path
 */
std::string LibraryActivity::extractFolderName(const std::string& path) const {
  std::string folderName = path;

  if (!folderName.empty() && folderName.back() == '/') {
    folderName.pop_back();
  }

  size_t lastSlash = folderName.find_last_of('/');
  if (lastSlash != std::string::npos) {
    return folderName.substr(lastSlash + 1);
  }

  return folderName;
}

/**
 * @brief Truncate text with ellipsis if it exceeds max length
 * @param text The text to truncate
 * @param maxLength Maximum allowed length
 * @return Truncated text
 */
std::string LibraryActivity::truncateTextIfNeeded(const std::string& text, size_t maxLength) const {
  if (text.length() > maxLength) {
    return text.substr(0, maxLength - 3) + "...";
  }
  return text;
}

/**
 * @brief Draw the header button
 * @param text Button text
 * @param headerY Y position of header
 * @param headerHeight Height of header
 * @param rightX Right X boundary
 * @param isSelected Whether button is selected
 * @return Next button X position
 */
int LibraryActivity::drawHeaderButton(const std::string& text, int headerY, int headerHeight, int rightX,
                                      bool isSelected) const {
  const int BUTTON_WIDTH = 110;
  const int BUTTON_PADDING = 10;

  int buttonX = rightX - BUTTON_WIDTH;
  int buttonY = headerY;

  int textWidth = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, text.c_str());
  int textX = buttonX + (BUTTON_WIDTH - textWidth) / 2;
  int textY = buttonY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

  if (isSelected) {
    renderer.rectangle.fill(buttonX, buttonY, BUTTON_WIDTH, headerHeight);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, text.c_str(), false);
    return buttonX - BUTTON_PADDING;
  }

  renderer.line.render(buttonX, buttonY, buttonX, buttonY + headerHeight - 1);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, text.c_str());
  return buttonX - BUTTON_PADDING;
}

/**
 * @brief Draw the sort button
 * @param headerY Y position of header
 * @param headerHeight Height of header
 * @param rightX Right X boundary
 * @return Next button X position
 */
int LibraryActivity::drawSortButton(int headerY, int headerHeight, int rightX) const {
  std::string buttonText = getSortButtonText();
  bool isSelected = isSortButtonSelected && tabSelectorIndex == 1;
  return drawHeaderButton(buttonText, headerY, headerHeight, rightX, isSelected);
}

int LibraryActivity::drawIndexButton(int headerY, int headerHeight, int x, bool isSelected) const {
  constexpr int BUTTON_WIDTH = 64;

  const int buttonX = x - BUTTON_WIDTH;
  const int buttonY = headerY;
  constexpr int iconSize = 40;
  const int iconX = buttonX + (BUTTON_WIDTH - iconSize) / 2;
  const int iconY = buttonY + (headerHeight - iconSize) / 2;

  if (isSelected) {
    renderer.rectangle.fill(buttonX, buttonY, BUTTON_WIDTH, headerHeight);
  }

  renderer.line.render(buttonX, buttonY, buttonX, buttonY + headerHeight - 1);
  renderer.bitmap.icon(Refresh, iconX, iconY, iconSize, iconSize, BitmapRender::Orientation::None, isSelected);
  return buttonX;
}

/**
 * @brief Draw button hints at the bottom of the screen
 */
void LibraryActivity::drawButtonHints() const {
  std::string back;
  if (currentViewMode == ViewMode::TAG_VIEW) {
    back = selectedTagKey_.empty() ? "« Groups" : "« Tags";
  } else if (currentViewMode == ViewMode::BOOK_LIST_VIEW) {
    back = SETTINGS.useLibraryIndex ? "Tags »" : "« Groups";
  } else {
    back = basepath != "/" ? "« Back" : "Books »";
  }
  std::string select = "Select";

  const auto labels = Activity::mappedInput.mapLabels(back.c_str(), select.c_str(), "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

/**
 * @brief Paginated book finder - only scans until we have enough books for current page
 * @param path Directory path to scan
 * @param books Vector to store found books
 * @param startIndex Starting index for pagination
 * @param count Maximum number of books to find
 * @param foundCount Running count of books found
 * @param stop Flag to stop scanning
 */
void LibraryActivity::findBooksPaginated(const std::string& path, std::vector<LibraryItem>& books, int startIndex,
                                         int count, int& foundCount, bool& stop) {
  if (stop) return;

  auto dir = SdMan.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  dir.rewindDirectory();
  char name[500];

  for (auto file = dir.openNextFile(); file && !stop; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));

    if (name[0] == '.' || strcmp(name, "System Volume Information") == 0 || strcmp(name, ".metadata") == 0 ||
        strcmp(name, "sleep") == 0) {
      file.close();
      continue;
    }

    std::string fullPath = path;
    if (fullPath.empty()) fullPath = "/";
    if (fullPath.back() != '/') fullPath += "/";
    fullPath += name;

    if (file.isDirectory()) {
      findBooksPaginated(fullPath, books, startIndex, count, foundCount, stop);
      file.close();
      continue;
    }

    std::string filename = name;
    if (isValidBookFile(filename)) {
      if (foundCount >= startIndex) {
        books.push_back(createBookItem(fullPath, filename, path));
      }
      foundCount++;

      if (books.size() >= (size_t)count) {
        stop = true;
      }
    }
    file.close();
  }
  dir.close();
}

/**
 * @brief Check if a file is a valid book file
 * @param filename The filename to check
 * @return true if valid book file extension
 */
bool LibraryActivity::isValidBookFile(const std::string& filename) const {
  return StringUtils::checkFileExtension(filename, ".epub") || StringUtils::checkFileExtension(filename, ".xtch") ||
         StringUtils::checkFileExtension(filename, ".xtc") || StringUtils::checkFileExtension(filename, ".txt") ||
         StringUtils::checkFileExtension(filename, ".md");
}

/**
 * @brief Create a book item from file information
 * @param fullPath Full path to the book
 * @param filename Name of the book file
 * @param parentPath Parent directory path
 * @return LibraryItem representing the book
 */
LibraryItem LibraryActivity::createBookItem(const std::string& fullPath, const std::string& filename,
                                            const std::string& parentPath) const {
  LibraryItem bookItem;
  bookItem.type = LibraryItem::Type::BOOK;
  bookItem.name = filename;
  bookItem.path = fullPath;
  bookItem.displayName = formatFolderName(getBaseFilename(filename));

  bookItem.folderPath = extractFolderName(parentPath);
  if (bookItem.folderPath.empty() || parentPath == "/") {
    bookItem.folderPath = "Library";
  }

  return bookItem;
}

/**
 * @brief Create a folder item from directory information
 * @param name Folder name
 * @param fullPath Full path to the folder
 * @return LibraryItem representing the folder
 */
LibraryItem LibraryActivity::createFolderItem(const std::string& name, const std::string& fullPath) const {
  LibraryItem folderItem;
  folderItem.type = LibraryItem::Type::FOLDER;
  folderItem.name = name;
  folderItem.displayName = formatFolderName(name);
  folderItem.path = fullPath;
  return folderItem;
}

/**
 * @brief Count total books without storing them (fast scan)
 * @param path Directory path to scan
 * @return Total number of books found
 */
int LibraryActivity::countTotalBooks(const std::string& path) {
  int count = 0;
  auto dir = SdMan.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return 0;
  }

  dir.rewindDirectory();
  char name[500];

  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));

    if (name[0] == '.' || strcmp(name, "System Volume Information") == 0 || strcmp(name, ".metadata") == 0 ||
        strcmp(name, "sleep") == 0) {
      file.close();
      continue;
    }

    std::string fullPath = path;
    if (fullPath.empty()) fullPath = "/";
    if (fullPath.back() != '/') fullPath += "/";
    fullPath += name;

    if (file.isDirectory()) {
      count += countTotalBooks(fullPath);
      file.close();
      continue;
    }

    std::string filename = name;
    if (isValidBookFile(filename)) {
      count++;
    }
    file.close();
  }
  dir.close();
  return count;
}

/**
 * @brief Check if a directory contains any books
 * @param path Directory path to check
 * @return true if directory contains books
 */
bool LibraryActivity::directoryHasBooks(const std::string& path) {
  auto dir = SdMan.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  dir.rewindDirectory();
  char name[500];

  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));

    if (name[0] == '.' || strcmp(name, "System Volume Information") == 0 || strcmp(name, "sleep") == 0 ||
        strcmp(name, "sleep/") == 0 || strcmp(name, ".metadata") == 0 || strcmp(name, ".metadata/") == 0) {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      std::string fullPath = path;
      if (!fullPath.empty() && fullPath.back() != '/') fullPath += "/";
      fullPath += name;

      if (directoryHasBooks(fullPath + "/")) {
        file.close();
        dir.close();
        return true;
      }
      file.close();
      continue;
    }

    std::string filename = name;
    if (isValidBookFile(filename)) {
      file.close();
      dir.close();
      return true;
    }
    file.close();
  }

  dir.close();
  return false;
}

/**
 * @brief Loads all books with pagination - only scans what's needed for current page
 */
void LibraryActivity::loadAllBooksRecursive() {
  MutexGuard guard(renderingMutex);
  if (renderingMutex && !guard.isAcquired()) {
    return;
  }

  loadAllBooksRecursiveLocked();
}

void LibraryActivity::loadAllBooksRecursiveLocked() {
  if (SETTINGS.useLibraryIndex) {
    loadLibraryFromIndex();
  } else {
    if (currentViewMode == ViewMode::BOOK_LIST_VIEW) {
      loadBooksRecursiveScan();
    } else {
      loadFoldersAndBooksCurrentDirectory();
    }
  }
}

/**
 * @brief Load books using recursive scan for book list view
 */
void LibraryActivity::loadBooksRecursiveScan() {
  std::vector<TempBookEntry> tempBooks;

  size_t booksCollected = 0;
  std::function<void(const std::string&)> collectBooks = [&](const std::string& path) {
    auto dir = SdMan.open(path.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      return;
    }

    dir.rewindDirectory();
    char name[500];

    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      file.getName(name, sizeof(name));

      if (shouldSkipFile(name)) {
        file.close();
        continue;
      }

      std::string fullPath = path;
      if (fullPath.empty()) fullPath = "/";
      if (fullPath.back() != '/') fullPath += "/";
      fullPath += name;

      if (file.isDirectory()) {
        collectBooks(fullPath);
        file.close();
        continue;
      }

      std::string filename = name;
      if (isValidBookFile(filename)) {
        TempBookEntry tempEntry = createTempBookEntry(fullPath, filename, path);
        tempBooks.push_back(tempEntry);
        if ((++booksCollected % 48u) == 0u) {
          yield();
        }
      }
      file.close();
    }
    dir.close();
  };

  collectBooks(basepath);
  sortTempBooks(tempBooks);
  applyPaginationToBooks(tempBooks);
}

/**
 * @brief Load folders and books for current directory view
 */
void LibraryActivity::loadFoldersAndBooksCurrentDirectory() {
  std::vector<LibraryItem> tempFolders;
  std::vector<TempBookEntry> tempBooks;

  auto root = SdMan.open(basepath.c_str());
  if (root && root.isDirectory()) {
    size_t scanYieldCount = 0;
    root.rewindDirectory();
    char name[500];

    
    for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
      file.getName(name, sizeof(name));

      if (shouldSkipFile(name)) {
        file.close();
        continue;
      }

      std::string fullPath = basepath;
      if (fullPath.back() != '/') fullPath += "/";
      fullPath += name;

      if (file.isDirectory()) {
        if (directoryHasBooks(fullPath + "/")) {
          tempFolders.push_back(createFolderItem(name, fullPath + "/"));
          if ((++scanYieldCount % 48u) == 0u) {
            yield();
          }
        }
        file.close();
        continue;
      }
      file.close();
    }

    
    root.rewindDirectory();
    for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
      file.getName(name, sizeof(name));

      if (name[0] == '.') {
        file.close();
        continue;
      }

      if (!file.isDirectory()) {
        std::string filename = name;
        if (isValidBookFile(filename)) {
          std::string fullPath = basepath;
          if (fullPath.back() != '/') fullPath += "/";
          TempBookEntry tempEntry = createTempBookEntry(fullPath + filename, filename, basepath);
          tempBooks.push_back(tempEntry);
          if ((++scanYieldCount % 48u) == 0u) {
            yield();
          }
        }
      }
      file.close();
    }
    root.close();
  }

  sortFoldersAndBooks(tempFolders, tempBooks);
  combineAndPaginateItems(tempFolders, tempBooks);
}

/**
 * @brief Check if a file should be skipped during scanning
 * @param name File or directory name
 * @return true if should be skipped
 */
bool LibraryActivity::shouldSkipFile(const char* name) const {
  return name[0] == '.' || strcmp(name, "System Volume Information") == 0 || strcmp(name, ".metadata") == 0 ||
         strcmp(name, "sleep") == 0;
}

/**
 * @brief Create a temporary book entry for sorting
 * @param fullPath Full path to the book
 * @param filename Name of the book file
 * @param parentPath Parent directory path
 * @return TempBookEntry structure
 */
TempBookEntry LibraryActivity::createTempBookEntry(const std::string& fullPath, const std::string& filename,
                                                   const std::string& parentPath) const {
  TempBookEntry tempEntry;
  tempEntry.path = fullPath;
  tempEntry.displayName = formatFolderName(getBaseFilename(filename));
  tempEntry.folderPath = extractFolderName(parentPath);
  if (tempEntry.folderPath.empty() || parentPath == "/") {
    tempEntry.folderPath = "Library";
  }
  tempEntry.sortKey = tempEntry.displayName;
  std::transform(tempEntry.sortKey.begin(), tempEntry.sortKey.end(), tempEntry.sortKey.begin(), ::tolower);
  tempEntry.isFavorite = isBookMarked(fullPath);
  return tempEntry;
}

/**
 * @brief Sort temporary books based on current sort mode
 * @param tempBooks Vector of temporary book entries to sort
 */
void LibraryActivity::sortTempBooks(std::vector<TempBookEntry>& tempBooks) {
  auto comparator = getBookComparator();
  std::sort(tempBooks.begin(), tempBooks.end(), comparator);
}

/**
 * @brief Get the appropriate book comparator for current sort mode
 * @return Comparator function for TempBookEntry
 */
std::function<bool(const TempBookEntry&, const TempBookEntry&)> LibraryActivity::getBookComparator() const {
  if (SETTINGS.librarySortEnabled == 0) {
    return [](const TempBookEntry& a, const TempBookEntry& b) { return a.sortKey < b.sortKey; };
  }
  switch (currentSortMode) {
    case SortMode::TITLE_AZ:
      return [this](const TempBookEntry& a, const TempBookEntry& b) {
        if (favoritesPromoted && a.isFavorite != b.isFavorite) return a.isFavorite > b.isFavorite;
        return a.sortKey < b.sortKey;
      };
    case SortMode::TITLE_ZA:
      return [this](const TempBookEntry& a, const TempBookEntry& b) {
        if (favoritesPromoted && a.isFavorite != b.isFavorite) return a.isFavorite > b.isFavorite;
        return a.sortKey > b.sortKey;
      };
    case SortMode::GROUP_AZ:
      return [this](const TempBookEntry& a, const TempBookEntry& b) {
        if (favoritesPromoted && a.isFavorite != b.isFavorite) return a.isFavorite > b.isFavorite;
        std::string aFolder = a.folderPath;
        std::string bFolder = b.folderPath;
        std::transform(aFolder.begin(), aFolder.end(), aFolder.begin(), ::tolower);
        std::transform(bFolder.begin(), bFolder.end(), bFolder.begin(), ::tolower);
        if (aFolder != bFolder) return aFolder < bFolder;
        return a.sortKey < b.sortKey;
      };
    case SortMode::GROUP_ZA:
      return [this](const TempBookEntry& a, const TempBookEntry& b) {
        if (favoritesPromoted && a.isFavorite != b.isFavorite) return a.isFavorite > b.isFavorite;
        std::string aFolder = a.folderPath;
        std::string bFolder = b.folderPath;
        std::transform(aFolder.begin(), aFolder.end(), aFolder.begin(), ::tolower);
        std::transform(bFolder.begin(), bFolder.end(), bFolder.begin(), ::tolower);
        if (aFolder != bFolder) return aFolder > bFolder;
        return a.sortKey < b.sortKey;
      };
    case SortMode::READING_AZ:
      return getReadingStatusComparator(true);  
    case SortMode::READING_ZA:
      return getReadingStatusComparator(false);  
    case SortMode::TAG_AZ:
      return [this](const TempBookEntry& a, const TempBookEntry& b) {
        if (favoritesPromoted && a.isFavorite != b.isFavorite) return a.isFavorite > b.isFavorite;
        std::string aTag = a.tag.empty() ? "zzzzzz untagged" : a.tag;
        std::string bTag = b.tag.empty() ? "zzzzzz untagged" : b.tag;
        std::transform(aTag.begin(), aTag.end(), aTag.begin(), ::tolower);
        std::transform(bTag.begin(), bTag.end(), bTag.begin(), ::tolower);
        if (aTag != bTag) return aTag < bTag;
        return a.sortKey < b.sortKey;
      };
    case SortMode::TAG_ZA:
      return [this](const TempBookEntry& a, const TempBookEntry& b) {
        if (favoritesPromoted && a.isFavorite != b.isFavorite) return a.isFavorite > b.isFavorite;
        std::string aTag = a.tag.empty() ? "" : a.tag;
        std::string bTag = b.tag.empty() ? "" : b.tag;
        std::transform(aTag.begin(), aTag.end(), aTag.begin(), ::tolower);
        std::transform(bTag.begin(), bTag.end(), bTag.begin(), ::tolower);
        if (aTag != bTag) return aTag > bTag;
        return a.sortKey < b.sortKey;
      };
  }
  return [](const TempBookEntry& a, const TempBookEntry& b) { return a.sortKey < b.sortKey; };
}

/**
 * @brief Get book comparator for reading status sorting (favorites not prioritized)
 * @param ascending Whether to sort titles A-Z or Z-A
 * @return Comparator function for TempBookEntry
 */
std::function<bool(const TempBookEntry&, const TempBookEntry&)> LibraryActivity::getReadingStatusComparator(
    bool ascending) const {
  return [ascending, this](const TempBookEntry& a, const TempBookEntry& b) {
    
    
    bool aIsReading = isBookOpened(a.path);
    bool aIsFinished = isBookFinished(a.path);
    bool bIsReading = isBookOpened(b.path);
    bool bIsFinished = isBookFinished(b.path);

    int aPriority = aIsFinished ? 0 : (aIsReading ? 2 : 1);
    int bPriority = bIsFinished ? 0 : (bIsReading ? 2 : 1);

    if (aPriority != bPriority) return aPriority > bPriority;

    
    if (ascending) {
      return a.sortKey < b.sortKey;
    } else {
      return a.sortKey > b.sortKey;
    }
  };
}

/**
 * @brief Apply pagination to sorted books and load current page
 * @param tempBooks Sorted vector of temporary book entries
 */
void LibraryActivity::applyPaginationToBooks(const std::vector<TempBookEntry>& tempBooks) {
  int totalItems = tempBooks.size();
  itemsPerPage = isLibraryGridMode() ? GRID_ITEMS_PER_PAGE : BOOK_ITEMS_PER_PAGE;
  totalPages = (totalItems + itemsPerPage - 1) / itemsPerPage;
  if (totalPages == 0) totalPages = 1;

  if (currentPage >= totalPages) currentPage = totalPages - 1;
  if (currentPage < 0) currentPage = 0;

  currentPageItems.clear();
  int startIdx = currentPage * itemsPerPage;
  int endIdx = std::min(startIdx + itemsPerPage, totalItems);

  for (int i = startIdx; i < endIdx; i++) {
    const auto& temp = tempBooks[i];
    LibraryItem item;
    item.type = LibraryItem::Type::BOOK;
    item.name = getBaseFilename(temp.path);
    item.path = temp.path;
    item.displayName = temp.displayName;
    item.folderPath = temp.folderPath;
    currentPageItems.push_back(item);
  }
}

/**
 * @brief Sort folders and books for directory view
 * @param tempFolders Vector of folder items to sort
 * @param tempBooks Vector of temporary book entries to sort
 */
void LibraryActivity::sortFoldersAndBooks(std::vector<LibraryItem>& tempFolders,
                                          std::vector<TempBookEntry>& tempBooks) {
  auto folderComparator = getFolderComparator();
  auto bookComparator = getBookComparator();

  std::sort(tempFolders.begin(), tempFolders.end(), folderComparator);
  std::sort(tempBooks.begin(), tempBooks.end(), bookComparator);
}

/**
 * @brief Get folder comparator for current sort mode
 * @return Comparator function for LibraryItem folders
 */
std::function<bool(const LibraryItem&, const LibraryItem&)> LibraryActivity::getFolderComparator() const {
  if (SETTINGS.librarySortEnabled == 0) {
    return [](const LibraryItem& a, const LibraryItem& b) {
      std::string aName = a.displayName;
      std::string bName = b.displayName;
      std::transform(aName.begin(), aName.end(), aName.begin(), ::tolower);
      std::transform(bName.begin(), bName.end(), bName.begin(), ::tolower);
      if (aName != bName) {
        return aName < bName;
      }
      return a.path < b.path;
    };
  }
  switch (currentSortMode) {
    case SortMode::TITLE_AZ:
    case SortMode::GROUP_AZ:
    case SortMode::TAG_AZ:
      return [](const LibraryItem& a, const LibraryItem& b) {
        std::string aName = a.displayName;
        std::string bName = b.displayName;
        std::transform(aName.begin(), aName.end(), aName.begin(), ::tolower);
        std::transform(bName.begin(), bName.end(), bName.begin(), ::tolower);
        return aName < bName;
      };
    case SortMode::TITLE_ZA:
    case SortMode::GROUP_ZA:
    case SortMode::TAG_ZA:
      return [](const LibraryItem& a, const LibraryItem& b) {
        std::string aName = a.displayName;
        std::string bName = b.displayName;
        std::transform(aName.begin(), aName.end(), aName.begin(), ::tolower);
        std::transform(bName.begin(), bName.end(), bName.begin(), ::tolower);
        return aName > bName;
      };
    case SortMode::READING_AZ:
    case SortMode::READING_ZA:
      return [](const LibraryItem& a, const LibraryItem& b) { return a.displayName < b.displayName; };
  }
  return [](const LibraryItem& a, const LibraryItem& b) { return a.displayName < b.displayName; };
}

/**
 * @brief Combine folders and books and apply pagination
 * @param tempFolders Sorted vector of folder items
 * @param tempBooks Sorted vector of temporary book entries
 */
void LibraryActivity::combineAndPaginateItems(const std::vector<LibraryItem>& tempFolders,
                                              const std::vector<TempBookEntry>& tempBooks) {
  std::vector<LibraryItem> allItems;
  allItems.insert(allItems.end(), tempFolders.begin(), tempFolders.end());

  for (const auto& temp : tempBooks) {
    LibraryItem item;
    item.type = LibraryItem::Type::BOOK;
    item.name = getBaseFilename(temp.path);
    item.path = temp.path;
    item.displayName = temp.displayName;
    item.folderPath = temp.folderPath;
    allItems.push_back(item);
  }

  int totalItems = allItems.size();
  itemsPerPage = isLibraryGridMode() ? GRID_ITEMS_PER_PAGE : FOLDER_ITEMS_PER_PAGE;
  totalPages = (totalItems + itemsPerPage - 1) / itemsPerPage;
  if (totalPages == 0) totalPages = 1;

  if (currentPage >= totalPages) currentPage = totalPages - 1;
  if (currentPage < 0) currentPage = 0;

  currentPageItems.clear();
  int startIdx = currentPage * itemsPerPage;
  int endIdx = std::min(startIdx + itemsPerPage, totalItems);

  for (int i = startIdx; i < endIdx; i++) {
    currentPageItems.push_back(allItems[i]);
  }
}

bool LibraryActivity::restoreSelectionToPath(const std::string& path) {
  const std::string targetPath = normalizeLibraryPath(path);
  if (targetPath.empty()) {
    return false;
  }

  const int originalPage = currentPage;

  for (int page = 0; page < totalPages; ++page) {
    currentPage = page;
    loadAllBooksRecursive();
    for (int i = 0; i < static_cast<int>(currentPageItems.size()); ++i) {
      if (normalizeLibraryPath(currentPageItems[i].path) == targetPath) {
        selectorIndex = i;
        isHeaderButtonSelected = false;
        isIndexButtonSelected = false;
        isSortButtonSelected = false;
        listScrollOffset = 0;
        return true;
      }
    }
  }

  currentPage = std::max(0, std::min(originalPage, totalPages - 1));
  loadAllBooksRecursive();
  return false;
}

bool LibraryActivity::restoreSelectionToTag(const std::string& tagKey) {
  if (tagKey.empty()) {
    return false;
  }

  const int originalPage = currentPage;

  for (int page = 0; page < totalPages; ++page) {
    currentPage = page;
    loadAllBooksRecursive();
    for (int i = 0; i < static_cast<int>(currentPageItems.size()); ++i) {
      if (currentPageItems[i].type == LibraryItem::Type::FOLDER && currentPageItems[i].path == tagKey) {
        selectorIndex = i;
        isHeaderButtonSelected = false;
        isIndexButtonSelected = false;
        isSortButtonSelected = false;
        listScrollOffset = 0;
        return true;
      }
    }
  }

  currentPage = std::max(0, std::min(originalPage, totalPages - 1));
  loadAllBooksRecursive();
  return false;
}

/**
 * @brief Task trampoline for display task
 * @param param Pointer to LibraryActivity instance
 */
void LibraryActivity::taskTrampoline(void* param) { static_cast<LibraryActivity*>(param)->displayTaskLoop(); }

/**
 * @brief Display task loop that handles periodic rendering
 */
void LibraryActivity::displayTaskLoop() {
  while (!displayTaskStopRequested_) {
    {
      MutexGuard guard(renderingMutex);
      if (guard.isAcquired() && updateRequired) {
        updateRequired = false;
        render();
        if (!halfRefreshOnLoadApplied_) {
          halfRefreshOnLoadApplied_ = true;
          SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Library);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  displayTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

/**
 * @brief Check if a book is marked as favorite
 * @param path Path to the book
 * @return true if book is favorite
 */
bool LibraryActivity::isBookMarked(const std::string& path) const {
  auto* book = BOOK_STATE.findBookByPath(path);
  return book ? book->isFavorite : false;
}

/**
 * @brief Check if a book is currently being read
 * @param path Path to the book
 * @return true if book is opened/reading
 */
bool LibraryActivity::isBookOpened(const std::string& path) const {
  auto* book = BOOK_STATE.findBookByPath(path);
  return book ? book->isReading : false;
}

/**
 * @brief Check if a book is marked as finished
 * @param path Path to the book
 * @return true if book is finished
 */
bool LibraryActivity::isBookFinished(const std::string& path) const {
  auto* book = BOOK_STATE.findBookByPath(path);
  return book ? book->isFinished : false;
}

/**
 * @brief Render the library screen
 */
void LibraryActivity::render() const {
  const int gridStartY = TAB_BAR_HEIGHT * 2;

  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth() - 1;

  renderTabBar(renderer);

  std::string headerText = getHeaderText();
  int headerTextX = 20;
  int headerTextY = TAB_BAR_HEIGHT + (TAB_BAR_HEIGHT - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  const bool showIndexButton = shouldShowIndexButton();
  int containerWidth = screenWidth - 110;
  if (showIndexButton) {
    containerWidth -= 64;
  }

  bool headerSelected = isHeaderButtonSelected && tabSelectorIndex == 1;
  if (headerSelected) renderer.rectangle.fill(0, TAB_BAR_HEIGHT, containerWidth, TAB_BAR_HEIGHT, static_cast<int>(GfxRenderer::FillTone::Ink));

  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, headerTextX, headerTextY, headerText.c_str(), !headerSelected,
                    EpdFontFamily::BOLD);
  int headerButtonRightX = drawSortButton(TAB_BAR_HEIGHT, TAB_BAR_HEIGHT, screenWidth);
  if (showIndexButton) {
    drawIndexButton(TAB_BAR_HEIGHT, TAB_BAR_HEIGHT, headerButtonRightX + 10, isIndexButtonSelected && tabSelectorIndex == 1);
  }

  renderer.line.render(0, TAB_BAR_HEIGHT + TAB_BAR_HEIGHT, screenWidth, TAB_BAR_HEIGHT * 2);

  renderLibraryList(gridStartY);
  drawButtonHints();
  if (isIndexing_) {
    showIndexingPopup();
  }
  renderer.displayBuffer();
}

/**
 * @brief Toggle between book list view and folder view
 */
void LibraryActivity::toggleViewMode() {
  if (currentViewMode == ViewMode::BOOK_LIST_VIEW) {
    currentViewMode = ViewMode::FOLDER_VIEW;
    selectedTagKey_.clear();
    if (!savedFolderPath.empty()) {
      basepath = savedFolderPath;
      savedFolderPath.clear();
    }
  } else {
    currentViewMode = ViewMode::BOOK_LIST_VIEW;
    selectedTagKey_.clear();
    savedFolderPath = basepath;
  }

  resetNavigation();
  loadAllBooksRecursive();
  updateRequired = true;
}

void LibraryActivity::switchToTagView() {
  if (!SETTINGS.useLibraryIndex) {
    switchToFolderView();
    return;
  }

  if (currentViewMode == ViewMode::FOLDER_VIEW) {
    savedFolderPath = basepath;
  }
  currentViewMode = ViewMode::TAG_VIEW;
  currentSortMode = SortMode::TAG_AZ;
  basepath = "/";
  selectedTagKey_.clear();
  resetNavigation();
  loadAllBooksRecursive();
  updateRequired = true;
}

/**
 * @brief Switch to folder view mode
 */
void LibraryActivity::switchToFolderView() {
  currentViewMode = ViewMode::FOLDER_VIEW;
  selectedTagKey_.clear();

  if (!savedFolderPath.empty()) {
    basepath = savedFolderPath;
    savedFolderPath.clear();
  }

  resetNavigation();
  loadAllBooksRecursive();
  updateRequired = true;
}

/**
 * @brief Reset navigation state (selection, scroll, page)
 */
void LibraryActivity::resetNavigation() {
  currentPage = 0;
  selectorIndex = 0;
  isHeaderButtonSelected = false;
  isIndexButtonSelected = false;
  isSortButtonSelected = false;
  listScrollOffset = 0;
  libraryListDownNextMs = 0;
  libraryListUpNextMs = 0;
}

bool LibraryActivity::shouldShowIndexButton() const {
  return SETTINGS.useLibraryIndex != 0;
}

void LibraryActivity::startLibraryIndexing() {
  if (isIndexing_) {
    return;
  }

  isIndexing_ = true;
  libraryIndexReloadRequested_ = false;
  indexingProgress_ = 0;
  indexingTotal_ = 0;
  updateRequired = true;

  BaseType_t created = xTaskCreate(
      [](void* param) {
        auto* activity = static_cast<LibraryActivity*>(param);

        FsFile root = SdMan.open("/");
        if (root) {
          activity->indexingTotal_ = LibraryIndexer::countBooks(root);
          root.close();
        }

        vTaskDelay(pdMS_TO_TICKS(10));

        LibraryIndexer::indexAll([activity](int current, int total, const char*) {
          activity->indexingProgress_ = current;
          activity->indexingTotal_ = total;
          if (current % 10 == 0) {
            activity->updateRequired = true;
            vTaskDelay(pdMS_TO_TICKS(1));
          }
        });

        SETTINGS.useLibraryIndex = 1;
        SETTINGS.saveToFile();
        activity->isIndexing_ = false;
        activity->libraryIndexReloadRequested_ = true;
        activity->updateRequired = true;
        vTaskDelete(nullptr);
      },
      "LibIdxBtnTask", 4096, this, 1, nullptr);

  if (created != pdPASS) {
    isIndexing_ = false;
    updateRequired = true;
  }
}

void LibraryActivity::showIndexingPopup() const {
  ScreenComponents::drawPopup(renderer, "Indexing");
}

/**
 * @brief Called when entering the activity
 */
void LibraryActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  if (!renderingMutex) return;
  displayTaskStopRequested_ = false;
  halfRefreshOnLoadApplied_ = false;
  renderer.clearScreen(0xff);

  currentViewMode = storageToViewMode(SETTINGS.libraryViewMode, SETTINGS.useLibraryIndex != 0);
  selectedTagKey_.clear();
  if (currentViewMode != ViewMode::FOLDER_VIEW) {
    savedFolderPath = basepath;
    basepath = "/";
  }
  resetNavigation();
  tabSelectorIndex = 1;
  currentSortMode = storageToSortMode(SETTINGS.librarySortMode);
  if (!SETTINGS.useLibraryIndex && (currentSortMode == SortMode::TAG_AZ || currentSortMode == SortMode::TAG_ZA)) {
    currentSortMode = SortMode::TITLE_AZ;
  }

  loadAllBooksRecursive();

  if (displayTaskHandle == nullptr) {
    xTaskCreate(&LibraryActivity::taskTrampoline, "LibraryTask", 4096, this, 1, &displayTaskHandle);
  }
  updateRequired = true;
}

/**
 * @brief Called when exiting the activity
 */
void LibraryActivity::onExit() {
  displayTaskStopRequested_ = true;
  const unsigned long stopStart = millis();
  while (displayTaskHandle && millis() - stopStart < 1500) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  SETTINGS.librarySortMode = sortModeToStorage(currentSortMode);
  SETTINGS.libraryViewMode = viewModeToStorage(currentViewMode);
  SETTINGS.saveToFile();

  Activity::onExit();

  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }

  resetLibraryView();
}

/**
 * @brief Go to the next page
 */
void LibraryActivity::goToNextPage() {
  if (currentPage < totalPages - 1) {
    MutexGuard guard(renderingMutex, pdMS_TO_TICKS(1000));
    if (renderingMutex && !guard.isAcquired()) {
      return;
    }

    currentPage++;
    loadAllBooksRecursiveLocked();
    selectorIndex = 0;
    listScrollOffset = 0;
    updateRequired = true;
  }
}

/**
 * @brief Go to the previous page
 */
void LibraryActivity::goToPreviousPage() {
  if (currentPage > 0) {
    MutexGuard guard(renderingMutex, pdMS_TO_TICKS(1000));
    if (renderingMutex && !guard.isAcquired()) {
      return;
    }

    currentPage--;
    loadAllBooksRecursiveLocked();
    selectorIndex = 0;
    listScrollOffset = 0;
    updateRequired = true;
  }
}

/**
 * @brief Main loop for handling user input
 */
void LibraryActivity::loop() {
  if (libraryIndexReloadRequested_ && !isIndexing_) {
    libraryIndexReloadRequested_ = false;
    if (currentViewMode == ViewMode::FOLDER_VIEW) {
      basepath = "/";
    }
    resetNavigation();
    loadAllBooksRecursive();
    updateRequired = true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    updateRequired = true;
    return;
  }

  std::vector<LibraryItem>& currentList = currentPageItems;
  const int itemCount = static_cast<int>(currentList.size());

  bool wantDownStep = false;
  bool wantUpStep = false;
  if (tabSelectorIndex == 1) {
    if (Activity::mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      wantDownStep = true;
      libraryListDownNextMs = millis() + LIB_LIST_REPEAT_INITIAL_MS;
    } else if (Activity::mappedInput.isPressed(MappedInputManager::Button::Down)) {
      if (libraryListDownNextMs != 0 && millis() >= libraryListDownNextMs) {
        wantDownStep = true;
        libraryListDownNextMs = millis() + LIB_LIST_REPEAT_RATE_MS;
      }
    } else {
      libraryListDownNextMs = 0;
    }

    if (Activity::mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      wantUpStep = true;
      libraryListUpNextMs = millis() + LIB_LIST_REPEAT_INITIAL_MS;
    } else if (Activity::mappedInput.isPressed(MappedInputManager::Button::Up)) {
      if (libraryListUpNextMs != 0 && millis() >= libraryListUpNextMs) {
        wantUpStep = true;
        libraryListUpNextMs = millis() + LIB_LIST_REPEAT_RATE_MS;
      }
    } else {
      libraryListUpNextMs = 0;
    }
  } else {
    libraryListDownNextMs = 0;
    libraryListUpNextMs = 0;
  }

  const bool leftPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool rightPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool confirmPressed = Activity::mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  const bool confirmHeld = Activity::mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  const unsigned long holdTime = Activity::mappedInput.getHeldTime();

  if (tabSelectorIndex == 1 && !isHeaderButtonSelected && !isIndexButtonSelected && !isSortButtonSelected) {
    if (handlePageNavigation(wantUpStep, wantDownStep, itemCount)) {
      return;
    }
  }

  
  if (confirmHeld && holdTime >= FAVORITE_HOLD_MS) {
    handleFavoriteLongPress(itemCount);
    return;
  }

  if (tabSelectorIndex != 1) return;

  
  if (leftPressed) {
    tabSelectorIndex = 0;
    navigateToSelectedMenu();
    return;
  }

  if (rightPressed) {
    tabSelectorIndex = 2;
    navigateToSelectedMenu();
    return;
  }

  if (tabSelectorIndex != 1) return;

  
  handleSelectionNavigation(wantUpStep, wantDownStep, itemCount);
  handleButtonSelectionNavigation(leftPressed, rightPressed);

  
  if (confirmPressed && holdTime < FAVORITE_HOLD_MS) {
    handleConfirmAction(itemCount);
    return;
  }

  if (Activity::mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    handleBackNavigation();
    return;
  }
}

/**
 * @brief Handle page up/down navigation
 * @param wantUpStep One list-level up step (press or hold repeat)
 * @param wantDownStep One list-level down step (press or hold repeat)
 * @param itemCount Number of items in current list
 * @return true if page was changed
 */
bool LibraryActivity::handlePageNavigation(bool wantUpStep, bool wantDownStep, int itemCount) {
  if (itemCount <= 0) {
    return false;
  }

  
  if (currentPage < totalPages - 1) {
    int pageChangeThreshold = itemCount - 1;
    if (selectorIndex >= pageChangeThreshold && wantDownStep) {
      goToNextPage();
      return true;
    }
  }

  
  if (currentPage > 0) {
    if (selectorIndex == 0 && wantUpStep) {
      goToPreviousPage();
      return true;
    }
  }

  return false;
}

/**
 * @brief Handle favorite marking on long press
 * @param itemCount Number of items in current list
 */
void LibraryActivity::handleFavoriteLongPress(int itemCount) {
  if (!isHeaderButtonSelected && !isIndexButtonSelected && !isSortButtonSelected && selectorIndex >= 0 &&
      selectorIndex < itemCount) {
    const LibraryItem& item = currentPageItems[selectorIndex];

    if (item.type == LibraryItem::Type::BOOK) {
      auto* book = BOOK_STATE.findBookByPath(item.path);

      if (!book) {
        BOOK_STATE.addOrUpdateBook(item.path, item.displayName, "");
        book = BOOK_STATE.findBookByPath(item.path);
        if (book) {
          book->isFavorite = true;
          BOOK_STATE.saveToFile();
        }
      } else {
        BOOK_STATE.toggleFavorite(item.path);
      }

      updateRequired = true;
    }
  }
}

/**
 * @brief Handle selection navigation (up/down) in the list
 * @param wantUpStep One list-level up step (press or hold repeat)
 * @param wantDownStep One list-level down step (press or hold repeat)
 * @param itemCount Number of items in current list
 */
void LibraryActivity::handleSelectionNavigation(bool wantUpStep, bool wantDownStep, int itemCount) {
  if (wantDownStep) {
    if (isHeaderButtonSelected) {
      isHeaderButtonSelected = false;
      if (shouldShowIndexButton()) {
        isIndexButtonSelected = true;
      } else {
        isSortButtonSelected = true;
      }
      selectorIndex = -1;
      updateRequired = true;
      return;
    }

    if (isIndexButtonSelected) {
      isIndexButtonSelected = false;
      isSortButtonSelected = true;
      selectorIndex = -1;
      updateRequired = true;
      return;
    }

    if (isSortButtonSelected) {
      isSortButtonSelected = false;
      if (itemCount > 0) {
        selectorIndex = 0;
        updateRequired = true;
      }
      return;
    }

    if (selectorIndex < itemCount - 1) {
      selectorIndex++;
      updateRequired = true;
    }
    return;
  }

  if (wantUpStep) {
    if (selectorIndex == 0) {
      selectorIndex = -1;
      isSortButtonSelected = true;
      updateRequired = true;
      return;
    }

    if (selectorIndex > 0) {
      selectorIndex--;
      updateRequired = true;
      return;
    }

    if (isSortButtonSelected) {
      isSortButtonSelected = false;
      if (shouldShowIndexButton()) {
        isIndexButtonSelected = true;
      } else {
        isHeaderButtonSelected = true;
      }
      updateRequired = true;
      return;
    }

    if (isIndexButtonSelected) {
      isIndexButtonSelected = false;
      isHeaderButtonSelected = true;
      updateRequired = true;
      return;
    }
  }
}

/**
 * @brief Handle button selection navigation (left/right)
 * @param leftPressed Whether left button was pressed
 * @param rightPressed Whether right button was pressed
 */
void LibraryActivity::handleButtonSelectionNavigation(bool leftPressed, bool rightPressed) {
  if (isHeaderButtonSelected || isIndexButtonSelected || isSortButtonSelected) {
    if (leftPressed && isSortButtonSelected) {
      isSortButtonSelected = false;
      if (shouldShowIndexButton()) {
        isIndexButtonSelected = true;
      } else {
        isHeaderButtonSelected = true;
      }
      updateRequired = true;
      return;
    }

    if (leftPressed && isIndexButtonSelected) {
      isIndexButtonSelected = false;
      isHeaderButtonSelected = true;
      updateRequired = true;
      return;
    }

    if (rightPressed && isHeaderButtonSelected) {
      isHeaderButtonSelected = false;
      if (shouldShowIndexButton()) {
        isIndexButtonSelected = true;
      } else {
        isSortButtonSelected = true;
      }
      updateRequired = true;
      return;
    }

    if (rightPressed && isIndexButtonSelected) {
      isIndexButtonSelected = false;
      isSortButtonSelected = true;
      updateRequired = true;
      return;
    }
  }
}

/**
 * @brief Handle confirm action (select item or button)
 * @param itemCount Number of items in current list
 */
void LibraryActivity::handleConfirmAction(int itemCount) {
  if (tabSelectorIndex != 1) return;

  if (isHeaderButtonSelected) {
    toggleViewMode();
    return;
  }

  if (isIndexButtonSelected) {
    startLibraryIndexing();
    return;
  }

  if (isSortButtonSelected) {
    cycleSortMode();
    updateRequired = true;
    return;
  }

  if (selectorIndex >= 0 && selectorIndex < itemCount) {
    const LibraryItem& item = currentPageItems[selectorIndex];

    if (currentViewMode == ViewMode::TAG_VIEW && item.type == LibraryItem::Type::FOLDER) {
      selectedTagKey_ = item.path;
      resetNavigation();
      loadAllBooksRecursive();
      updateRequired = true;
      return;
    }

    if (currentViewMode == ViewMode::FOLDER_VIEW && item.type == LibraryItem::Type::FOLDER) {
      basepath = item.path;
      switchToFolderView();
      return;
    }

    auto* book = BOOK_STATE.findBookByPath(item.path);

    if (!book) {
      BOOK_STATE.addOrUpdateBook(item.path, item.displayName, "");
      book = BOOK_STATE.findBookByPath(item.path);
      if (book) {
        book->isReading = true;
      }
    } else {
      BOOK_STATE.setReading(item.path, true);
    }

    BOOK_STATE.saveToFile();
    onSelectBook(item.path);
  }
}

/**
 * @brief Handle back button navigation
 */
void LibraryActivity::handleBackNavigation() {
  if (currentViewMode == ViewMode::TAG_VIEW) {
    if (!selectedTagKey_.empty()) {
      const std::string previousTagKey = selectedTagKey_;
      selectedTagKey_.clear();
      resetNavigation();
      loadAllBooksRecursive();
      restoreSelectionToTag(previousTagKey);
      updateRequired = true;
      return;
    }
    switchToFolderView();
    return;
  }

  if (currentViewMode == ViewMode::BOOK_LIST_VIEW && SETTINGS.useLibraryIndex) {
    switchToTagView();
    return;
  }

  if (basepath == "/") {
    toggleViewMode();
    return;
  }

  std::string previousChildPath = basepath;
  if (!previousChildPath.empty() && previousChildPath.back() != '/') {
    previousChildPath += "/";
  }

  std::string newPath = basepath;

  if (!newPath.empty() && newPath.back() == '/') {
    newPath.pop_back();
  }

  size_t lastSlash = newPath.find_last_of('/');

  if (lastSlash == 0) {
    newPath = "/";
  } else if (lastSlash != std::string::npos) {
    newPath.resize(lastSlash);
  } else {
    newPath = "/";
  }

  basepath = newPath;

  if (currentViewMode != ViewMode::FOLDER_VIEW) {
    currentViewMode = ViewMode::FOLDER_VIEW;
    savedFolderPath.clear();
  }

  resetNavigation();
  loadAllBooksRecursive();
  restoreSelectionToPath(previousChildPath);
  updateRequired = true;
}

/**
 * @brief Cycle through sort modes
 */
void LibraryActivity::cycleSortMode() {
  if (SETTINGS.librarySortEnabled == 0) {
    return;
  }
  favoritesPromoted = false;
  switch (currentSortMode) {
    case SortMode::TITLE_AZ:
      currentSortMode = SortMode::TITLE_ZA;
      break;
    case SortMode::TITLE_ZA:
      currentSortMode = SortMode::GROUP_AZ;
      break;
    case SortMode::GROUP_AZ:
      currentSortMode = SortMode::GROUP_ZA;
      break;
    case SortMode::GROUP_ZA:
      currentSortMode = SortMode::READING_AZ;
      break;
    case SortMode::READING_AZ:
      currentSortMode = SortMode::READING_ZA;
      break;
    case SortMode::READING_ZA:
      currentSortMode = SETTINGS.useLibraryIndex ? SortMode::TAG_AZ : SortMode::TITLE_AZ;
      break;
    case SortMode::TAG_AZ:
      currentSortMode = SortMode::TAG_ZA;
      break;
    case SortMode::TAG_ZA:
      currentSortMode = SortMode::TITLE_AZ;
      break;
  }

  loadAllBooksRecursive();

  selectorIndex = -1;
  isHeaderButtonSelected = false;
  isIndexButtonSelected = false;
  isSortButtonSelected = true;
  listScrollOffset = 0;
  updateRequired = true;
}

/**
 * @brief Reset the library view state
 */
void LibraryActivity::resetLibraryView() {
  currentPageItems.clear();
  selectorIndex = -1;
  listScrollOffset = 0;
  currentPage = 0;
  totalPages = 0;
  updateRequired = true;
}

/**
 * @brief Get the text for the sort button based on current sort mode
 * @return Sort button text
 */
std::string LibraryActivity::getSortButtonText() const {
  if (SETTINGS.librarySortEnabled == 0) {
    return "A-Z";
  }
  switch (currentSortMode) {
    case SortMode::TITLE_AZ:
      return "Title A-Z";
    case SortMode::TITLE_ZA:
      return "Title Z-A";
    case SortMode::GROUP_AZ:
      return "Group A-Z";
    case SortMode::GROUP_ZA:
      return "Group Z-A";
    case SortMode::READING_AZ:
      return "Read A-Z";
    case SortMode::READING_ZA:
      return "Read Z-A";
    case SortMode::TAG_AZ:
      return "Tag A-Z";
    case SortMode::TAG_ZA:
      return "Tag Z-A";
  }
  return "Sort";
}

/**
 * @brief Get the height of a list item based on its type
 * @param item The library item
 * @return Height in pixels
 */
int LibraryActivity::getItemHeight(const LibraryItem& item) const {
  if (currentViewMode == ViewMode::FOLDER_VIEW && item.type == LibraryItem::Type::FOLDER) {
    return LIST_ITEM_HEIGHT;
  }
  return 70;  
}

bool LibraryActivity::isLibraryGridMode() const {
  return (currentViewMode == ViewMode::FOLDER_VIEW || currentViewMode == ViewMode::TAG_VIEW) &&
         SETTINGS.libraryMode == SystemSetting::LIBRARY_GRID;
}

bool LibraryActivity::isTagViewMode() const {
  return currentViewMode == ViewMode::TAG_VIEW && SETTINGS.useLibraryIndex;
}

/**
 * @brief Render the library list
 * @param startY Starting Y position for the list
 */
void LibraryActivity::renderLibraryList(int startY) const {
  const std::vector<LibraryItem>& items = currentPageItems;

  if (items.empty()) {
    int messageY = startY + 150;
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, messageY, "No books found");
    return;
  }

  if (isLibraryGridMode()) {
    renderLibraryGrid(startY );
    return;
  }

  int screenWidth = renderer.getScreenWidth() - 1;
  int screenHeight = renderer.getScreenHeight();

  int drawY = startY;
  int maxVisibleItems = 0;

  
  for (int i = listScrollOffset; i < static_cast<int>(items.size()); i++) {
    int itemHeight = getItemHeight(items[i]);
    if (drawY + itemHeight > screenHeight) break;
    maxVisibleItems++;
    drawY += itemHeight;
  }

  drawY = startY + 2;
  int itemsDrawn = 0;

  for (int i = listScrollOffset; i < static_cast<int>(items.size()) && itemsDrawn < maxVisibleItems; i++) {
    const LibraryItem& item = items[i];
    bool isSelected = (tabSelectorIndex == 1 && selectorIndex == i && !isHeaderButtonSelected &&
                       !isIndexButtonSelected && !isSortButtonSelected);
    int itemHeight = getItemHeight(item);

    if (isSelected) {
      renderer.rectangle.fill(0, drawY, screenWidth, itemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    renderItemIcon(item, drawY, itemHeight, isSelected);
    renderItemText(item, drawY, itemHeight, isSelected, screenWidth);

    if (i < static_cast<int>(items.size()) - 1) {
      renderer.line.render(0, drawY + itemHeight - 1, screenWidth, drawY + itemHeight - 1);
    }

    drawY += itemHeight;
    itemsDrawn++;
  }
}

void LibraryActivity::renderGridItemIcon(const LibraryItem& item, int x, int y, int w, int h, bool isSelected, bool isLarge) const {
  const int iconSize = std::min(72, std::max(32, std::min(w, h) - 12));
  const int iconX = x + (w - iconSize) / 2;
  const int iconY = y + (h - iconSize) / 2;
  if (item.type == LibraryItem::Type::FOLDER) {
    renderer.bitmap.icon(isLarge ? FolderLarge : Folder, iconX, iconY, iconSize, iconSize, BitmapRender::Orientation::None, isSelected);
  } else {
    if (isBookMarked(item.path)) {
      const int starSize = 18;
      renderer.bitmap.icon(Star, x + w - starSize - 10, y, starSize, starSize, BitmapRender::Orientation::None,
                           isSelected);
    }
    renderer.bitmap.icon(isLarge ? BookLarge : Book, iconX, iconY, iconSize, iconSize, BitmapRender::Orientation::None, isSelected);

  }
}

void LibraryActivity::renderLibraryGrid(int startY) const {
  const std::vector<LibraryItem>& items = currentPageItems;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight() - 30;
  const int availW = std::max(1, screenW - LIB_GRID_OUTER_PAD * 2);
  const int availH = std::max(1, screenH - startY - LIB_GRID_OUTER_PAD * 2);
  const int frameW = std::min(GRID_ICON_SIZE, (availW - (LIB_GRID_COLS - 1) * LIB_GRID_GAP_X) / LIB_GRID_COLS);
  const int maxFrameH = (availH - (LIB_GRID_ROWS - 1) * LIB_GRID_MIN_GAP_Y) / LIB_GRID_ROWS;
  const int frameH = std::max(96, std::min(GRID_ICON_SIZE, maxFrameH));
  const int remainingH = availH - LIB_GRID_ROWS * frameH;
  const int gapY = (LIB_GRID_ROWS > 1) ? std::max(LIB_GRID_MIN_GAP_Y, remainingH / (LIB_GRID_ROWS - 1)) : 0;
  const int blockH = LIB_GRID_ROWS * frameH + (LIB_GRID_ROWS - 1) * gapY;
  const int blockTop = startY + LIB_GRID_OUTER_PAD + std::max(0, (availH - blockH) / 2);
  const int blockW = LIB_GRID_COLS * frameW + (LIB_GRID_COLS - 1) * LIB_GRID_GAP_X;
  const int row0X = LIB_GRID_OUTER_PAD + std::max(0, (availW - blockW) / 2);
  const bool rounded = true;

  for (int i = 0; i < static_cast<int>(items.size()) && i < GRID_ITEMS_PER_PAGE; ++i) {
    const int row = i / LIB_GRID_COLS;
    const int col = i % LIB_GRID_COLS;
    const int boxX = row0X + col * (frameW + LIB_GRID_GAP_X);
    const int boxY = blockTop + row * (frameH + gapY);
    const bool selected =
        tabSelectorIndex == 1 && selectorIndex == i && !isHeaderButtonSelected && !isIndexButtonSelected &&
        !isSortButtonSelected;

    // renderer.rectangle.fill(boxX, boxY, frameW, frameH, false, rounded);
    // renderer.rectangle.render(boxX, boxY, frameW, frameH, true, rounded);
    if (selected) {
      renderer.rectangle.fill(boxX + 1, boxY + 1, frameW - 2, frameH - 2, true, rounded);
    }

    const int iconX = boxX + 8;
    const int iconY = boxY + 8;
    const int iconW = std::max(8, frameW - 16);
    const int iconH = std::max(8, frameH - LIB_GRID_LABEL_H - LIB_GRID_LABEL_GAP - 16);
    renderGridItemIcon(items[i], iconX, iconY, iconW, iconH, selected, true);

    const int labelY = iconY + iconH + LIB_GRID_LABEL_GAP;
    const std::string label =
        renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, items[i].displayName.c_str(), frameW - 10);
    const int labelW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, label.c_str());
    const int labelX = boxX + std::max(4, (frameW - labelW) / 2);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labelX, labelY - 10, label.c_str(), !selected);
  }
}

/**
 * @brief Render the icon for a list item
 * @param item The library item
 * @param drawY Y position to draw at
 * @param itemHeight Height of the item
 * @param isSelected Whether the item is selected
 */
void LibraryActivity::renderItemIcon(const LibraryItem& item, int drawY, int itemHeight, bool isSelected) const {
  int iconX = 15;
  int iconY = drawY + (itemHeight / 2) - 12;

  if (item.type == LibraryItem::Type::FOLDER) {
    renderer.bitmap.icon(Folder, iconX, iconY, 24, 24, BitmapRender::Orientation::None, isSelected);
  } else {
    renderer.bitmap.icon(Book, iconX, iconY + 2, 24, 24, BitmapRender::Orientation::None, isSelected);

    if (isBookMarked(item.path)) {
      int starX = renderer.getScreenWidth() - 1 - 45;
      renderer.bitmap.icon(Star, starX, iconY + 2, 24, 24, BitmapRender::Orientation::None, isSelected);
    }
  }
}

/**
 * @brief Render the text for a list item
 * @param item The library item
 * @param drawY Y position to draw at
 * @param itemHeight Height of the item
 * @param isSelected Whether the item is selected
 * @param screenWidth Width of the screen
 */
void LibraryActivity::renderItemText(const LibraryItem& item, int drawY, int itemHeight, bool isSelected,
                                     int screenWidth) const {
  int iconX = 15;
  int textX = iconX + 24 + 10;
  int textWidth = screenWidth - textX - (isBookMarked(item.path) ? 50 : 15);

  bool useTwoLineFormat = (currentViewMode == ViewMode::BOOK_LIST_VIEW) || (item.type == LibraryItem::Type::BOOK);

  if (useTwoLineFormat) {
    std::string titleText =
        renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, item.displayName.c_str(), textWidth - 5);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, drawY + 8, titleText.c_str(), !isSelected);

    std::string secondLineText =
        !item.folderPath.empty()
            ? renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, item.folderPath.c_str(), textWidth - 5)
            : "Library";
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, drawY + 40, secondLineText.c_str(), !isSelected);

    bool isDone = isBookFinished(item.path);
    int markerSpace = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, secondLineText.c_str()) + iconX + 40;
    if (isDone) {
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, markerSpace, drawY + 40, "(completed)", !isSelected,
                        EpdFontFamily::BOLD);
    }

    if (isBookOpened(item.path) && !isDone) {
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, markerSpace, drawY + 40, "(reading)", !isSelected,
                        EpdFontFamily::BOLD);
    }
  } else {
    int textY = drawY + (itemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
    std::string displayText =
        renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, item.displayName.c_str(), textWidth - 5);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, displayText.c_str(), !isSelected);
  }
}

/**
 * @brief Load library items from index file (optimized for performance)
 */
void LibraryActivity::loadLibraryFromIndex() {
  allBooksList.clear();
  libraryItems.clear();
  currentPageItems.clear();

  FsFile idxFile = SdMan.open("/.metadata/library/library.idx", O_READ);
  if (!idxFile) return;

  idxFile.seek(5);

  std::string cleanBase = basepath;
  if (cleanBase.length() > 1 && cleanBase.back() == '/') {
    cleanBase.pop_back();
  }

  if (currentViewMode == ViewMode::BOOK_LIST_VIEW || isTagViewMode()) {
    loadBooksFromIndex(idxFile, cleanBase);
  } else {
    loadFoldersFromIndex(idxFile, cleanBase);
  }
  idxFile.close();
}

/**
 * @brief Load books from index file for book list view
 * @param idxFile Open index file
 * @param cleanBase Cleaned base path
 */
void LibraryActivity::loadBooksFromIndex(FsFile& idxFile, const std::string& cleanBase) {
  std::vector<TempBookEntry> tempBooks;
  std::vector<BookTags::Entry> tags;
  const bool useTags = isTagViewMode() || currentSortMode == SortMode::TAG_AZ || currentSortMode == SortMode::TAG_ZA;
  if (useTags) {
    BookTags::load(tags);
  }
  std::vector<LibraryItem> tagFolders;
  size_t indexEntries = 0;

  while (idxFile.available()) {
    uint8_t marker;
    if (idxFile.read(&marker, 1) != 1) break;

    if (marker == 0x01) {  
      TempBookEntry tempEntry = readBookEntryFromIndex(idxFile);
      if (useTags) {
        tempEntry.tag = BookTags::find(tags, tempEntry.path);
        tempEntry.folderPath = tempEntry.tag.empty() ? tempEntry.folderPath : tempEntry.tag;
      }

      if (isTagViewMode() && selectedTagKey_.empty()) {
        const std::string tagKey = tempEntry.tag.empty() ? TAG_UNTAGGED_KEY : tempEntry.tag;
        bool seen = false;
        for (const auto& folder : tagFolders) {
          if (folder.path == tagKey) {
            seen = true;
            break;
          }
        }
        if (!seen) {
          LibraryItem tagItem;
          tagItem.type = LibraryItem::Type::FOLDER;
          tagItem.name = tagKey;
          tagItem.path = tagKey;
          tagItem.displayName = tempEntry.tag.empty() ? TAG_UNTAGGED_LABEL : tempEntry.tag;
          tagFolders.push_back(tagItem);
        }
        if ((++indexEntries % 64u) == 0u) {
          yield();
        }
        continue;
      }

      const bool matchesTag =
          !isTagViewMode() || selectedTagKey_.empty() ||
          ((selectedTagKey_ == TAG_UNTAGGED_KEY && tempEntry.tag.empty()) || selectedTagKey_ == tempEntry.tag);

      if (matchesTag && tempEntry.path.find(cleanBase) == 0) {
        tempEntry.isFavorite = isBookMarked(tempEntry.path);
        tempBooks.push_back(tempEntry);
        if ((++indexEntries % 64u) == 0u) {
          yield();
        }
      }
    } else if (marker == 0xFF) {
      skipDirectoryMarker(idxFile);
    }
  }

  if (isTagViewMode() && selectedTagKey_.empty()) {
    std::sort(tagFolders.begin(), tagFolders.end(), [](const LibraryItem& a, const LibraryItem& b) {
      const bool aUntagged = a.path == TAG_UNTAGGED_KEY;
      const bool bUntagged = b.path == TAG_UNTAGGED_KEY;
      if (aUntagged != bUntagged) {
        return !aUntagged;
      }
      std::string aName = a.displayName;
      std::string bName = b.displayName;
      std::transform(aName.begin(), aName.end(), aName.begin(), ::tolower);
      std::transform(bName.begin(), bName.end(), bName.begin(), ::tolower);
      return aName < bName;
    });
    combineAndPaginateItems(tagFolders, tempBooks);
    return;
  }

  sortTempBooks(tempBooks);
  applyPaginationToBooks(tempBooks);
}

/**
 * @brief Load folders from index file for folder view
 * @param idxFile Open index file
 * @param cleanBase Cleaned base path
 */
void LibraryActivity::loadFoldersFromIndex(FsFile& idxFile, const std::string& cleanBase) {
  std::vector<LibraryItem> tempFolders;
  std::vector<TempBookEntry> tempBooks;
  std::vector<BookTags::Entry> tags;
  const bool useTags = currentSortMode == SortMode::TAG_AZ || currentSortMode == SortMode::TAG_ZA;
  if (useTags) {
    BookTags::load(tags);
  }
  size_t indexEntries = 0;

  while (idxFile.available()) {
    uint8_t marker;
    if (idxFile.read(&marker, 1) != 1) break;

    if (marker == 0x01) {  
      TempBookEntry tempEntry = readBookEntryFromIndex(idxFile);
      if (useTags) {
        tempEntry.tag = BookTags::find(tags, tempEntry.path);
        tempEntry.folderPath = tempEntry.tag.empty() ? "Others" : tempEntry.tag;
      }

      
      size_t lastSlash = tempEntry.path.find_last_of('/');
      std::string bookParent =
          (lastSlash == 0 || lastSlash == std::string::npos) ? "/" : tempEntry.path.substr(0, lastSlash);
      if (bookParent == cleanBase) {
        tempEntry.isFavorite = isBookMarked(tempEntry.path);
        tempBooks.push_back(tempEntry);
        if ((++indexEntries % 64u) == 0u) {
          yield();
        }
      }
    } else if (marker == 0xFF) {  
      LibraryItem folderItem = readDirectoryEntryFromIndex(idxFile);

      if (shouldIncludeFolder(folderItem.path, cleanBase)) {
        tempFolders.push_back(folderItem);
        if ((++indexEntries % 64u) == 0u) {
          yield();
        }
      }
    }
  }

  sortFoldersAndBooks(tempFolders, tempBooks);
  combineAndPaginateItems(tempFolders, tempBooks);
}

/**
 * @brief Read a book entry from the index file
 * @param idxFile Open index file
 * @return TempBookEntry structure
 */
TempBookEntry LibraryActivity::readBookEntryFromIndex(FsFile& idxFile) {
  TempBookEntry tempEntry;

  uint16_t pLen;
  idxFile.read(&pLen, sizeof(pLen));
  std::vector<char> pBuf(pLen + 1, 0);
  idxFile.read(pBuf.data(), pLen);
  tempEntry.path = std::string(pBuf.data(), pLen);

  uint8_t nLen;
  idxFile.read(&nLen, sizeof(nLen));
  idxFile.seek(idxFile.position() + nLen);  

  uint8_t dLen;
  idxFile.read(&dLen, sizeof(dLen));
  std::vector<char> dBuf(dLen + 1, 0);
  idxFile.read(dBuf.data(), dLen);
  tempEntry.displayName = std::string(dBuf.data(), dLen);

  uint8_t fLen;
  idxFile.read(&fLen, sizeof(fLen));
  std::vector<char> fBuf(fLen + 1, 0);
  idxFile.read(fBuf.data(), fLen);
  tempEntry.folderPath = std::string(fBuf.data(), fLen);

  size_t pos;
  while ((pos = tempEntry.path.find("’")) != std::string::npos) {
    tempEntry.path.replace(pos, 3, "'");
  }

  tempEntry.sortKey = tempEntry.displayName;
  std::transform(tempEntry.sortKey.begin(), tempEntry.sortKey.end(), tempEntry.sortKey.begin(), ::tolower);

  tempEntry.isFavorite = isBookMarked(tempEntry.path);
  return tempEntry;
}

/**
 * @brief Read a directory entry from the index file
 * @param idxFile Open index file
 * @return LibraryItem representing the directory
 */
LibraryItem LibraryActivity::readDirectoryEntryFromIndex(FsFile& idxFile) {
  uint16_t pathLen;
  idxFile.read(&pathLen, sizeof(pathLen));
  std::vector<char> pathBuf(pathLen + 1, 0);
  idxFile.read(pathBuf.data(), pathLen);
  std::string dirPath = std::string(pathBuf.data(), pathLen);

  uint16_t entryCount;
  idxFile.read(&entryCount, sizeof(entryCount));

  LibraryItem folderItem;
  folderItem.type = LibraryItem::Type::FOLDER;
  folderItem.path = dirPath;
  folderItem.displayName = formatFolderName(extractFolderName(dirPath));

  return folderItem;
}

/**
 * @brief Skip a directory marker in the index file
 * @param idxFile Open index file
 */
void LibraryActivity::skipDirectoryMarker(FsFile& idxFile) {
  uint16_t pathLen;
  idxFile.read(&pathLen, sizeof(pathLen));
  idxFile.seek(idxFile.position() + pathLen);
  uint16_t entryCount;
  idxFile.read(&entryCount, sizeof(entryCount));
}

/**
 * @brief Check if a folder should be included in the current view
 * @param folderPath Path to the folder
 * @param cleanBase Cleaned base path
 * @return true if folder should be included
 */
bool LibraryActivity::shouldIncludeFolder(const std::string& folderPath, const std::string& cleanBase) const {
  if (folderPath == "/" || folderPath == cleanBase || folderPath == cleanBase + "/") {
    return false;
  }

  std::string checkDir = folderPath;
  if (checkDir.length() > 1 && checkDir.back() == '/') {
    checkDir.pop_back();
  }

  size_t lastSlash = checkDir.find_last_of('/');
  std::string parentOfDir = (lastSlash == 0 || lastSlash == std::string::npos) ? "/" : checkDir.substr(0, lastSlash);

  return parentOfDir == cleanBase;
}
