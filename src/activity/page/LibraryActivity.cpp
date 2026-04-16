#include "LibraryActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <algorithm>
#include <functional>
#include <memory>

#include "images/Book.h"
#include "images/Folder.h"
#include "images/Star.h"
#include "state/BookState.h"
#include "state/RecentBooks.h"
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
  std::string sortKey;
  bool isFavorite;
};

using LibraryItem = LibraryActivity::LibraryItem;
using ViewMode = LibraryActivity::ViewMode;
using SortMode = LibraryActivity::SortMode;

namespace {

/**
 * @brief RAII mutex guard for automatic mutex management
 */
class MutexGuard {
 private:
  SemaphoreHandle_t& mutex;
  bool acquired;

 public:
  explicit MutexGuard(SemaphoreHandle_t& m) : mutex(m), acquired(false) {
    if (mutex) {
      acquired = (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE);
    }
  }

  ~MutexGuard() {
    if (acquired && mutex) {
      xSemaphoreGive(mutex);
    }
  }

  bool isAcquired() const { return acquired; }
};

// Timing constants
constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long FAVORITE_HOLD_MS = 500;
/** First repeat delay after Down/Up press while browsing the list */
constexpr unsigned long LIB_LIST_REPEAT_INITIAL_MS = 420;
/** Repeat interval while Down/Up held */
constexpr unsigned long LIB_LIST_REPEAT_RATE_MS = 95;

}  // namespace

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
      selectorIndex(0),
      listScrollOffset(0),
      updateRequired(false),
      isHeaderButtonSelected(false),
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

  int textWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, text.c_str());
  int textX = buttonX + (BUTTON_WIDTH - textWidth) / 2;
  int textY = buttonY + (headerHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

  if (isSelected) {
    renderer.fillRect(buttonX, buttonY, BUTTON_WIDTH, headerHeight);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, text.c_str(), false);
    return buttonX - BUTTON_PADDING;
  }

  renderer.drawLine(buttonX, buttonY, buttonX, buttonY + headerHeight - 1);
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, text.c_str());
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

/**
 * @brief Draw button hints at the bottom of the screen
 */
void LibraryActivity::drawButtonHints() const {
  std::string back = currentViewMode == ViewMode::FOLDER_VIEW ? basepath != "/" ? "« Back" : "Books »" : "« Groups";
  std::string select = "Select";

  const auto labels = Activity::mappedInput.mapLabels(back.c_str(), select.c_str(), "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
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
 * @brief Paginated folder finder - only scans until we have enough folders for current page
 * @param path Directory path to scan
 * @param folders Vector to store found folders
 * @param startIndex Starting index for pagination
 * @param count Maximum number of folders to find
 * @param foundCount Running count of folders found
 * @param stop Flag to stop scanning
 */
void LibraryActivity::findFoldersPaginated(const std::string& path, std::vector<LibraryItem>& folders, int startIndex,
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
      if (directoryHasBooks(fullPath + "/")) {
        if (foundCount >= startIndex) {
          folders.push_back(createFolderItem(name, fullPath + "/"));
        }
        foundCount++;

        if (folders.size() >= (size_t)count) {
          stop = true;
        }
      }
      file.close();
      continue;
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
 * @brief Count total folders with books without storing them (fast scan)
 * @param path Directory path to scan
 * @return Total number of folders containing books
 */
int LibraryActivity::countTotalFolders(const std::string& path) {
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
      if (directoryHasBooks(fullPath + "/")) {
        count++;
      }
      file.close();
      continue;
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
    root.rewindDirectory();
    char name[500];

    // Scan for folders
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
        }
        file.close();
        continue;
      }
      file.close();
    }

    // Scan for books
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
      return getReadingStatusComparator(true);  // Favorites NOT prioritized
    case SortMode::READING_ZA:
      return getReadingStatusComparator(false);  // Favorites NOT prioritized
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
    // NOTE: Favorites are NOT prioritized in this sort mode
    // Sort by reading status: Reading (2) > Unfinished (1) > Completed (0)
    bool aIsReading = isBookOpened(a.path);
    bool aIsFinished = isBookFinished(a.path);
    bool bIsReading = isBookOpened(b.path);
    bool bIsFinished = isBookFinished(b.path);

    int aPriority = aIsFinished ? 0 : (aIsReading ? 2 : 1);
    int bPriority = bIsFinished ? 0 : (bIsReading ? 2 : 1);

    if (aPriority != bPriority) return aPriority > bPriority;

    // Then sort by title
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
  itemsPerPage = BOOK_ITEMS_PER_PAGE;
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
  switch (currentSortMode) {
    case SortMode::TITLE_AZ:
    case SortMode::GROUP_AZ:
      return [](const LibraryItem& a, const LibraryItem& b) {
        std::string aName = a.displayName;
        std::string bName = b.displayName;
        std::transform(aName.begin(), aName.end(), aName.begin(), ::tolower);
        std::transform(bName.begin(), bName.end(), bName.begin(), ::tolower);
        return aName < bName;
      };
    case SortMode::TITLE_ZA:
    case SortMode::GROUP_ZA:
      return [](const LibraryItem& a, const LibraryItem& b) {
        std::string aName = a.displayName;
        std::string bName = b.displayName;
        std::transform(aName.begin(), aName.end(), aName.begin(), ::tolower);
        std::transform(bName.begin(), bName.end(), bName.begin(), ::tolower);
        return aName > bName;
      };
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
  itemsPerPage = FOLDER_ITEMS_PER_PAGE;
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

/**
 * @brief Task trampoline for display task
 * @param param Pointer to LibraryActivity instance
 */
void LibraryActivity::taskTrampoline(void* param) { static_cast<LibraryActivity*>(param)->displayTaskLoop(); }

/**
 * @brief Display task loop that handles periodic rendering
 */
void LibraryActivity::displayTaskLoop() {
  while (true) {
    {
      MutexGuard guard(renderingMutex);
      if (guard.isAcquired() && updateRequired) {
        updateRequired = false;
        render();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
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
  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth() - 1;

  renderTabBar(renderer);

  std::string headerText = getHeaderText();
  int headerTextX = 20;
  int headerTextY = TAB_BAR_HEIGHT + (TAB_BAR_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  int containerWidth = screenWidth - 110;

  bool headerSelected = isHeaderButtonSelected && tabSelectorIndex == 1;
  if (headerSelected) renderer.fillRect(0, TAB_BAR_HEIGHT, containerWidth, TAB_BAR_HEIGHT, GfxRenderer::FillTone::Gray);

  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, headerTextX, headerTextY, headerText.c_str(), !headerSelected,
                    EpdFontFamily::BOLD);
  drawSortButton(TAB_BAR_HEIGHT, TAB_BAR_HEIGHT, screenWidth);

  renderer.drawLine(0, TAB_BAR_HEIGHT + TAB_BAR_HEIGHT, screenWidth, TAB_BAR_HEIGHT * 2);

  renderLibraryList(TAB_BAR_HEIGHT * 2);
  drawButtonHints();
  renderer.displayBuffer();
}

/**
 * @brief Toggle between book list view and folder view
 */
void LibraryActivity::toggleViewMode() {
  if (currentViewMode == ViewMode::BOOK_LIST_VIEW) {
    currentViewMode = ViewMode::FOLDER_VIEW;
    if (!savedFolderPath.empty()) {
      basepath = savedFolderPath;
      savedFolderPath.clear();
    }
  } else {
    currentViewMode = ViewMode::BOOK_LIST_VIEW;
    savedFolderPath = basepath;
  }

  resetNavigation();
  loadAllBooksRecursive();
  updateRequired = true;
}

/**
 * @brief Switch to folder view mode
 */
void LibraryActivity::switchToFolderView() {
  currentViewMode = ViewMode::FOLDER_VIEW;

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
  isSortButtonSelected = false;
  listScrollOffset = 0;
  libraryListDownNextMs = 0;
  libraryListUpNextMs = 0;
}

/**
 * @brief Called when entering the activity
 */
void LibraryActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  if (!renderingMutex) return;
  renderer.clearScreen(0xff);

  currentViewMode = ViewMode::FOLDER_VIEW;
  resetNavigation();
  tabSelectorIndex = 1;

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
 * @brief Update items per page based on current view mode
 */
void LibraryActivity::updateItemsPerPage() {
  if (currentViewMode == ViewMode::BOOK_LIST_VIEW) {
    itemsPerPage = BOOK_ITEMS_PER_PAGE;
  } else {
    itemsPerPage = FOLDER_ITEMS_PER_PAGE;
  }
}

/**
 * @brief Update pagination and reload items
 */
void LibraryActivity::updatePagination() {
  updateItemsPerPage();
  loadAllBooksRecursive();
}

/**
 * @brief Load the current page of items
 */
void LibraryActivity::loadPage() { loadAllBooksRecursive(); }

/**
 * @brief Go to the next page
 */
void LibraryActivity::goToNextPage() {
  if (currentPage < totalPages - 1) {
    currentPage++;
    loadAllBooksRecursive();
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
    currentPage--;
    loadAllBooksRecursive();
    selectorIndex = 0;
    listScrollOffset = 0;
    updateRequired = true;
  }
}

/**
 * @brief Main loop for handling user input
 */
void LibraryActivity::loop() {
  // Handle power button page refresh
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

  bool pageChanged = false;

  // Handle page navigation
  if (tabSelectorIndex == 1 && !isHeaderButtonSelected && !isSortButtonSelected) {
    pageChanged = handlePageNavigation(wantUpStep, wantDownStep, itemCount);
    if (pageChanged) {
      return;
    }
  }

  // Handle long press for favorite marking
  if (confirmHeld && holdTime >= FAVORITE_HOLD_MS) {
    handleFavoriteLongPress(itemCount);
    return;
  }

  if (tabSelectorIndex != 1) return;

  // Handle tab navigation
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

  // Handle selection navigation
  handleSelectionNavigation(wantUpStep, wantDownStep, itemCount);
  handleButtonSelectionNavigation(leftPressed, rightPressed);

  // Handle confirm action
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

  // Auto page down when reaching bottom of list
  if (currentPage < totalPages - 1) {
    int pageChangeThreshold = itemCount - 1;
    if (selectorIndex >= pageChangeThreshold && wantDownStep) {
      goToNextPage();
      return true;
    }
  }

  // Auto page up when reaching top of list
  if (currentPage > 0) {
    int pageChangeThreshold = 1;
    if (selectorIndex <= pageChangeThreshold && wantUpStep) {
      goToPreviousPage();
      return true;
    }
  }

  if (selectorIndex == 0 && wantUpStep && currentPage > 0) {
    goToPreviousPage();
    return true;
  }

  return false;
}

/**
 * @brief Handle favorite marking on long press
 * @param itemCount Number of items in current list
 */
void LibraryActivity::handleFavoriteLongPress(int itemCount) {
  if (!isHeaderButtonSelected && !isSortButtonSelected && selectorIndex >= 0 && selectorIndex < itemCount) {
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
  if (isHeaderButtonSelected || isSortButtonSelected) {
    if (leftPressed && isSortButtonSelected) {
      isSortButtonSelected = false;
      isHeaderButtonSelected = true;
      updateRequired = true;
      return;
    }

    if (rightPressed && isHeaderButtonSelected) {
      isHeaderButtonSelected = false;
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

  if (isSortButtonSelected) {
    cycleSortMode();
    updateRequired = true;
    return;
  }

  if (selectorIndex >= 0 && selectorIndex < itemCount) {
    const LibraryItem& item = currentPageItems[selectorIndex];

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
  // Toggle view mode when at root
  if (basepath == "/") {
    toggleViewMode();
    return;
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
  updateRequired = true;
}

/**
 * @brief Apply sorting to current items
 */
void LibraryActivity::applySorting() {
  loadAllBooksRecursive();
  updateRequired = true;
}

/**
 * @deprecated Use sortTempBooks instead
 */
void LibraryActivity::sortByTitle(bool ascending) {
  std::vector<LibraryItem> favorites;
  std::vector<LibraryItem> others;

  for (const auto& item : currentPageItems) {
    if (item.type == LibraryItem::Type::BOOK && isBookMarked(item.path)) {
      favorites.push_back(item);
    } else {
      others.push_back(item);
    }
  }

  std::sort(favorites.begin(), favorites.end(), [ascending](const LibraryItem& a, const LibraryItem& b) {
    std::string aName = a.displayName;
    std::string bName = b.displayName;
    std::transform(aName.begin(), aName.end(), aName.begin(), ::tolower);
    std::transform(bName.begin(), bName.end(), bName.begin(), ::tolower);
    return ascending ? (aName < bName) : (aName > bName);
  });

  std::sort(others.begin(), others.end(), [ascending](const LibraryItem& a, const LibraryItem& b) {
    std::string aName = a.displayName;
    std::string bName = b.displayName;
    std::transform(aName.begin(), aName.end(), aName.begin(), ::tolower);
    std::transform(bName.begin(), bName.end(), bName.begin(), ::tolower);
    return ascending ? (aName < bName) : (aName > bName);
  });

  currentPageItems.clear();
  currentPageItems.insert(currentPageItems.end(), favorites.begin(), favorites.end());
  currentPageItems.insert(currentPageItems.end(), others.begin(), others.end());
}

/**
 * @deprecated Use sortTempBooks with GROUP mode instead
 */
void LibraryActivity::sortByGroup(bool ascending) {
  if (currentViewMode != ViewMode::BOOK_LIST_VIEW) return;

  std::sort(currentPageItems.begin(), currentPageItems.end(), [ascending](const LibraryItem& a, const LibraryItem& b) {
    std::string aGroup = a.folderPath.empty() ? "" : a.folderPath;
    std::string bGroup = b.folderPath.empty() ? "" : b.folderPath;

    std::transform(aGroup.begin(), aGroup.end(), aGroup.begin(), ::tolower);
    std::transform(bGroup.begin(), bGroup.end(), bGroup.begin(), ::tolower);

    if (aGroup == bGroup) {
      std::string aName = a.displayName;
      std::string bName = b.displayName;
      std::transform(aName.begin(), aName.end(), aName.begin(), ::tolower);
      std::transform(bName.begin(), bName.end(), bName.begin(), ::tolower);
      return aName < bName;
    }

    return ascending ? (aGroup < bGroup) : (aGroup > bGroup);
  });

  std::vector<LibraryItem> favorites;
  std::vector<LibraryItem> others;

  for (const auto& item : currentPageItems) {
    if (isBookMarked(item.path)) {
      favorites.push_back(item);
    } else {
      others.push_back(item);
    }
  }

  currentPageItems.clear();
  currentPageItems.insert(currentPageItems.end(), favorites.begin(), favorites.end());
  currentPageItems.insert(currentPageItems.end(), others.begin(), others.end());
}

/**
 * @brief Cycle through sort modes
 */
/**
 * @brief Cycle through sort modes
 */
void LibraryActivity::cycleSortMode() {
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
      currentSortMode = SortMode::TITLE_AZ;
      break;
  }

  loadAllBooksRecursive();

  selectorIndex = -1;
  isHeaderButtonSelected = false;
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
 * @deprecated Use combineAndPaginateItems instead
 */
void LibraryActivity::sortFolderViewByType(bool foldersFirst) {
  std::vector<LibraryItem> folders;
  std::vector<LibraryItem> books;
  std::vector<LibraryItem> favorites;

  for (const auto& item : currentPageItems) {
    if (item.type == LibraryItem::Type::FOLDER) {
      folders.push_back(item);
    } else {
      if (isBookMarked(item.path)) {
        favorites.push_back(item);
      } else {
        books.push_back(item);
      }
    }
  }

  auto sortFunc = [](const LibraryItem& a, const LibraryItem& b) {
    std::string aName = a.displayName;
    std::string bName = b.displayName;
    std::transform(aName.begin(), aName.end(), aName.begin(), ::tolower);
    std::transform(bName.begin(), bName.end(), bName.begin(), ::tolower);
    return aName < bName;
  };

  std::sort(folders.begin(), folders.end(), sortFunc);
  std::sort(favorites.begin(), favorites.end(), sortFunc);
  std::sort(books.begin(), books.end(), sortFunc);

  currentPageItems.clear();

  if (foldersFirst) {
    currentPageItems.insert(currentPageItems.end(), folders.begin(), folders.end());
    currentPageItems.insert(currentPageItems.end(), favorites.begin(), favorites.end());
    currentPageItems.insert(currentPageItems.end(), books.begin(), books.end());
  } else {
    currentPageItems.insert(currentPageItems.end(), favorites.begin(), favorites.end());
    currentPageItems.insert(currentPageItems.end(), books.begin(), books.end());
    currentPageItems.insert(currentPageItems.end(), folders.begin(), folders.end());
  }
}

/**
 * @brief Get the text for the sort button based on current sort mode
 * @return Sort button text
 */
/**
 * @brief Get the text for the sort button based on current sort mode
 * @return Sort button text
 */
std::string LibraryActivity::getSortButtonText() const {
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
  }
  return "Sort";
}

/**
 * @brief Update the scroll position based on current selection
 */
void LibraryActivity::updateScrollPosition() {
  const std::vector<LibraryItem>& currentList = currentPageItems;

  if (currentList.empty()) {
    listScrollOffset = 0;
    selectorIndex = -1;
    return;
  }

  if (selectorIndex < 0 || selectorIndex >= static_cast<int>(currentList.size())) {
    selectorIndex = -1;
    listScrollOffset = 0;
    return;
  }

  int screenHeight = renderer.getScreenHeight();
  int startY = TAB_BAR_HEIGHT * 2 + 10;
  int visibleAreaHeight = screenHeight - startY;

  int heightFromScrollToSelector = 0;
  for (int i = listScrollOffset; i <= selectorIndex; i++) {
    heightFromScrollToSelector += getItemHeight(currentList[i]);
  }

  if (heightFromScrollToSelector > visibleAreaHeight) {
    int accumulatedHeight = 0;
    for (int i = selectorIndex; i >= 0; i--) {
      accumulatedHeight += getItemHeight(currentList[i]);

      if (accumulatedHeight > visibleAreaHeight) {
        listScrollOffset = i + 1;
        return;
      }
    }
    listScrollOffset = 0;
    return;
  }

  if (selectorIndex < listScrollOffset) {
    listScrollOffset = selectorIndex;
  }
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
  return 70;  // Book item height
}

/**
 * @brief Render the library list
 * @param startY Starting Y position for the list
 */
void LibraryActivity::renderLibraryList(int startY) const {
  const std::vector<LibraryItem>& items = currentPageItems;

  if (items.empty()) {
    int messageY = startY + 150;
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, messageY, "No books found");
    return;
  }

  int screenWidth = renderer.getScreenWidth() - 1;
  int screenHeight = renderer.getScreenHeight();
  int visibleAreaHeight = screenHeight - startY;

  int drawY = startY;
  int maxVisibleItems = 0;

  // Calculate visible items
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
    bool isSelected = (tabSelectorIndex == 1 && selectorIndex == i && !isHeaderButtonSelected && !isSortButtonSelected);
    int itemHeight = getItemHeight(item);

    if (isSelected) {
      renderer.fillRect(0, drawY, screenWidth, itemHeight, GfxRenderer::FillTone::Gray);
    }

    renderItemIcon(item, drawY, itemHeight, isSelected);
    renderItemText(item, drawY, itemHeight, isSelected, screenWidth);

    if (i < static_cast<int>(items.size()) - 1) {
      renderer.drawLine(0, drawY + itemHeight - 1, screenWidth, drawY + itemHeight - 1);
    }

    drawY += itemHeight;
    itemsDrawn++;
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
    renderer.drawIcon(Folder, iconX, iconY, 24, 24, GfxRenderer::None, isSelected);
  } else {
    renderer.drawIcon(Book, iconX, iconY + 2, 24, 24, GfxRenderer::None, isSelected);

    if (isBookMarked(item.path)) {
      int starX = renderer.getScreenWidth() - 1 - 45;
      renderer.drawIcon(Star, starX, iconY + 2, 24, 24, GfxRenderer::None, isSelected);
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
        renderer.truncatedText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, item.displayName.c_str(), textWidth - 5);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, textX, drawY + 5, titleText.c_str(), !isSelected);

    std::string secondLineText =
        !item.folderPath.empty()
            ? renderer.truncatedText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, item.folderPath.c_str(), textWidth - 5)
            : "Library";
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, drawY + 38, secondLineText.c_str(), !isSelected);

    bool isDone = isBookFinished(item.path);
    int markerSpace = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, secondLineText.c_str()) + iconX + 40;
    if (isDone) {
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, markerSpace, drawY + 38, "(completed)", !isSelected,
                        EpdFontFamily::ITALIC);
    }

    if (isBookOpened(item.path) && !isDone) {
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, markerSpace, drawY + 38, "(reading)", !isSelected,
                        EpdFontFamily::ITALIC);
    }
  } else {
    int textY = drawY + (itemHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
    std::string displayText =
        renderer.truncatedText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, item.displayName.c_str(), textWidth - 5);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, textX, textY, displayText.c_str(), !isSelected);
  }
}

/**
 * @brief Load library items from index file (optimized for performance)
 */
void LibraryActivity::loadLibraryFromIndex() {
  MutexGuard guard(renderingMutex);
  if (!guard.isAcquired()) return;

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

  if (currentViewMode == ViewMode::BOOK_LIST_VIEW) {
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

  while (idxFile.available()) {
    uint8_t marker;
    if (idxFile.read(&marker, 1) != 1) break;

    if (marker == 0x01) {  // Book entry
      TempBookEntry tempEntry = readBookEntryFromIndex(idxFile);

      // Filter books under basepath
      if (tempEntry.path.find(cleanBase) == 0) {
        tempEntry.isFavorite = isBookMarked(tempEntry.path);
        tempBooks.push_back(tempEntry);
      }
    } else if (marker == 0xFF) {
      skipDirectoryMarker(idxFile);
    }
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

  while (idxFile.available()) {
    uint8_t marker;
    if (idxFile.read(&marker, 1) != 1) break;

    if (marker == 0x01) {  // Book entry
      TempBookEntry tempEntry = readBookEntryFromIndex(idxFile);

      // Check if book is in current folder
      size_t lastSlash = tempEntry.path.find_last_of('/');
      std::string bookParent =
          (lastSlash == 0 || lastSlash == std::string::npos) ? "/" : tempEntry.path.substr(0, lastSlash);
      if (bookParent == cleanBase) {
        tempEntry.isFavorite = isBookMarked(tempEntry.path);
        tempBooks.push_back(tempEntry);
      }
    } else if (marker == 0xFF) {  // Directory marker
      LibraryItem folderItem = readDirectoryEntryFromIndex(idxFile);

      if (shouldIncludeFolder(folderItem.path, cleanBase)) {
        tempFolders.push_back(folderItem);
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
  idxFile.seek(idxFile.position() + nLen);  // Skip name

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

/**
 * @deprecated Use loadBooksFromIndex or loadFoldersFromIndex instead
 */
void LibraryActivity::loadLibraryItems() { loadAllBooksRecursive(); }
