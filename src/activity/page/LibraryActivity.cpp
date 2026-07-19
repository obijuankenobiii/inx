/**
 * @file LibraryActivity.cpp
 * @brief Definitions for LibraryActivity.
 */

#include "LibraryActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>

#include "../settings/LibraryIndexer.h"
#include "images/Book.h"
#include "images/BookLarge.h"
#include "images/Folder.h"
#include "images/FolderLarge.h"
#include "images/Refresh.h"
#include "images/Star.h"
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

/** Converts a SortMode to its persisted storage value. */
uint8_t sortModeToStorage(SortMode m) { return static_cast<uint8_t>(m); }

/** Converts a persisted storage value back to a SortMode, defaulting to TITLE_AZ if out of range. */
SortMode storageToSortMode(uint8_t v) {
  if (v > static_cast<uint8_t>(SortMode::TAG_ZA)) {
    return SortMode::TITLE_AZ;
  }
  return static_cast<SortMode>(v);
}

/** Converts a ViewMode to its persisted storage value. */
uint8_t viewModeToStorage(ViewMode m) {
  switch (m) {
    case ViewMode::BOOK_LIST_VIEW:
      return SystemSetting::LIBRARY_VIEW_BOOKS;
    case ViewMode::TAG_VIEW:
      return SystemSetting::LIBRARY_VIEW_TAGS;
    case ViewMode::SHELF_VIEW:
      return SystemSetting::LIBRARY_VIEW_SHELF;
    case ViewMode::FOLDER_VIEW:
    default:
      return SystemSetting::LIBRARY_VIEW_FOLDERS;
  }
}

/** Converts a persisted storage value back to a ViewMode, honoring whether the tag index is enabled. */
ViewMode storageToViewMode(uint8_t v, bool indexEnabled) {
  if (v == SystemSetting::LIBRARY_VIEW_BOOKS) {
    return ViewMode::BOOK_LIST_VIEW;
  }
  if (v == SystemSetting::LIBRARY_VIEW_TAGS && indexEnabled) {
    return ViewMode::TAG_VIEW;
  }
  if (v == SystemSetting::LIBRARY_VIEW_SHELF) {
    return ViewMode::SHELF_VIEW;
  }
  return ViewMode::FOLDER_VIEW;
}

/** Strips trailing slashes from a path, returning "/" if the result would be empty. */
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
  /** Attempts to take the mutex, waiting up to timeout ticks. */
  explicit MutexGuard(SemaphoreHandle_t& m, TickType_t timeout = pdMS_TO_TICKS(100)) : mutex(m), acquired(false) {
    if (mutex) {
      acquired = (xSemaphoreTake(mutex, timeout) == pdTRUE);
    }
  }

  /** Releases the mutex if it was successfully acquired. */
  ~MutexGuard() {
    if (acquired && mutex) {
      xSemaphoreGive(mutex);
    }
  }

  /** Returns whether the mutex was successfully acquired. */
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
constexpr int LIB_SHELF_COLS = 3;
constexpr int LIB_SHELF_ROWS = 3;
constexpr int LIB_SHELF_GAP_X = 15;
constexpr int LIB_SHELF_GAP_Y = 12;
constexpr int LIB_SHELF_OUTER_PAD_X = 14;
constexpr int LIB_SHELF_OUTER_PAD_Y = 10;
constexpr int LIB_SHELF_BADGE_SIZE = 22;
constexpr uint32_t LIBRARY_TASK_STACK_SIZE = 8192;
constexpr uint32_t LIBRARY_INDEX_TASK_STACK_SIZE = 6144;
constexpr const char* TAG_UNTAGGED_KEY = "\x01";
constexpr const char* TAG_UNTAGGED_LABEL = "Others";
constexpr uint8_t BOOK_STATE_FAVORITE = 0x01;
constexpr uint8_t BOOK_STATE_READING = 0x02;
constexpr uint8_t BOOK_STATE_FINISHED = 0x04;

/** Returns whether value ends with suffix, ignoring case. */
bool endsWithIgnoreCase(const std::string& value, const char* suffix) {
  const size_t suffixLen = strlen(suffix);
  if (value.size() < suffixLen) {
    return false;
  }
  const size_t start = value.size() - suffixLen;
  for (size_t i = 0; i < suffixLen; ++i) {
    if (tolower(static_cast<unsigned char>(value[start + i])) != tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

/** Builds the metadata cache directory path for a book path under the given root. */
std::string metadataCachePathForBookPath(const std::string& bookPath, const char* root) {
  return std::string(root) + "/" + std::to_string(std::hash<std::string>{}(bookPath));
}

/** Resolves the on-disk thumbnail path for a book's shelf cover, if one has been cached. */
std::string resolveShelfImagePath(const std::string& bookPath) {
  const bool isXtc = endsWithIgnoreCase(bookPath, ".xtc");
  const char* primaryRoot = isXtc ? "/.metadata/xtc" : "/.metadata/epub";
  const char* secondaryRoot = isXtc ? "/.metadata/epub" : "/.metadata/xtc";
  // thumb.* only - no cover.*/cover_crop.* fallback (matches RecentActivity::resolveThumbnailPath).
  // cover.* is the full-size, unresized original embedded cover; JPEG/PNG decode cost scales with the
  // source image's pixel count, not the small card size it's drawn into, so falling back to it here
  // would occasionally pay for a full-size decode just to draw a ~140x190 card. If no thumb exists yet,
  // the card falls back to the no-cover title placeholder instead - cheap either way.
  const char* names[] = {"thumb.jpg", "thumb.png", "thumb.bmp"};

  auto findInRoot = [&](const char* root) -> std::string {
    const std::string base = metadataCachePathForBookPath(bookPath, root);
    for (const char* name : names) {
      const std::string path = base + "/" + name;
      if (SdMan.exists(path.c_str())) {
        return path;
      }
    }
    return "";
  };

  std::string found = findInRoot(primaryRoot);
  if (!found.empty()) {
    return found;
  }
  found = findInRoot(secondaryRoot);
  if (!found.empty()) {
    return found;
  }
  return "";
}

/** Draws the "currently opened" checkmark badge on a shelf card. */
void drawShelfCheckBadge(const GfxRenderer& renderer, int x, int y) {
  renderer.rectangle.fill(x, y, LIB_SHELF_BADGE_SIZE, LIB_SHELF_BADGE_SIZE, false);
  renderer.rectangle.render(x, y, LIB_SHELF_BADGE_SIZE, LIB_SHELF_BADGE_SIZE, true);
  renderer.line.render(x + 5, y + 12, x + 9, y + 16);
  renderer.line.render(x + 9, y + 16, x + 17, y + 6);
  renderer.line.render(x + 5, y + 13, x + 9, y + 17);
  renderer.line.render(x + 9, y + 17, x + 17, y + 7);
}

/** Draws the favorite star badge on a shelf card. */
void drawShelfFavoriteBadge(const GfxRenderer& renderer, int x, int y) {
  renderer.rectangle.fill(x, y, LIB_SHELF_BADGE_SIZE, LIB_SHELF_BADGE_SIZE, false);
  renderer.rectangle.render(x, y, LIB_SHELF_BADGE_SIZE, LIB_SHELF_BADGE_SIZE, true);
  renderer.bitmap.icon(Star, x + 3, y + 3, LIB_SHELF_BADGE_SIZE - 6, LIB_SHELF_BADGE_SIZE - 6);
}

/** No-cover fallback: title one word per line, each line centered - matches
 * RecentActivity::drawRecentNoCoverPlaceholder's look for the same situation. */
void drawShelfNoCoverTitle(const GfxRenderer& renderer, int x, int y, int w, int h, const std::string& title,
                           int fontId) {
  if (w <= 1 || h <= 1) {
    return;
  }
  const int lh = renderer.text.getLineHeight(fontId);
  const int maxLines = std::max(1, (h - 12) / std::max(1, lh));
  int wordCount = 0;
  bool inWord = false;
  for (char c : title) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      inWord = false;
    } else if (!inWord) {
      inWord = true;
      ++wordCount;
    }
  }
  if (wordCount <= 0) {
    return;
  }
  const int linesToDraw = std::min(maxLines, wordCount);
  const int totalTextH = linesToDraw * lh;
  int lineY = y + std::max(4, (h - totalTextH) / 2);
  const int innerPad = 6;
  const int maxWordW = std::max(8, w - 2 * innerPad);

  size_t pos = 0;
  for (int line = 0; line < linesToDraw; ++line) {
    while (pos < title.size() && std::isspace(static_cast<unsigned char>(title[pos]))) {
      ++pos;
    }
    size_t end = pos;
    while (end < title.size() && !std::isspace(static_cast<unsigned char>(title[end]))) {
      ++end;
    }
    std::string wshow = title.substr(pos, end - pos);
    if (renderer.text.getWidth(fontId, wshow.c_str()) > maxWordW) {
      wshow = renderer.text.truncate(fontId, wshow.c_str(), maxWordW, EpdFontFamily::REGULAR);
    }
    const int tw = renderer.text.getWidth(fontId, wshow.c_str());
    renderer.text.render(fontId, x + (w - tw) / 2, lineY, wshow.c_str(), true, EpdFontFamily::REGULAR);
    lineY += lh;
    pos = end;
  }
}

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
 * @brief Destroy the Library Activity, releasing the shelf page buffer
 */
LibraryActivity::~LibraryActivity() { freeLibraryShelfBuffer(); }

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
  // Index mode adds a refresh button to the header; shorten the text so it doesn't overlap it.
  const size_t maxLen = shouldShowIndexButton() ? 22 : 25;
  if (currentViewMode == ViewMode::TAG_VIEW) {
    if (selectedTagKey_.empty()) {
      return "Categories";
    }
    return selectedTagKey_ == TAG_UNTAGGED_KEY ? TAG_UNTAGGED_LABEL : truncateTextIfNeeded(selectedTagKey_, maxLen);
  }

  if (currentViewMode == ViewMode::SHELF_VIEW) {
    return "Shelf";
  }

  if (basepath == "/") {
    return currentViewMode == ViewMode::BOOK_LIST_VIEW ? "All Books" : "Collection";
  }

  std::string folderName = extractFolderName(basepath);
  std::string header = formatFolderName(folderName);
  return truncateTextIfNeeded(header, maxLen);
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

/**
 * @brief Draw the library index refresh button
 * @param headerY Y position of header
 * @param headerHeight Height of header
 * @param x Right X boundary
 * @param isSelected Whether button is selected
 * @return Next button X position
 */
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
    back = selectedTagKey_.empty() ? "Shelf »" : "« Tags";
  } else if (currentViewMode == ViewMode::BOOK_LIST_VIEW) {
    back = SETTINGS.useLibraryIndex ? "Tags »" : "Shelf »";
  } else if (currentViewMode == ViewMode::SHELF_VIEW) {
    back = "« Groups";
  } else {
    back = basepath != "/" ? "« Back" : "Books »";
  }
  std::string select = "Select";

  const auto labels = Activity::mappedInput.mapLabels(back.c_str(), select.c_str(), "", "");
  renderButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
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

/** Returns the cached reading/favorite state flags for a book path. */
uint8_t LibraryActivity::getBookStateFlags(const std::string& path) const {
  auto cached = bookStateCache_.find(path);
  if (cached != bookStateCache_.end()) {
    return cached->second;
  }

  uint8_t flags = 0;
  auto* book = BOOK_STATE.findBookByPath(path);
  if (book) {
    if (book->isFavorite) flags |= BOOK_STATE_FAVORITE;
    if (book->isReading) flags |= BOOK_STATE_READING;
    if (book->isFinished) flags |= BOOK_STATE_FINISHED;
  }
  bookStateCache_[path] = flags;
  return flags;
}

/** Returns the on-disk display cache path for a book's shelf thumbnail. */
std::string LibraryActivity::getShelfImagePath(const std::string& bookPath) const {
  auto cached = shelfImagePathCache_.find(bookPath);
  if (cached != shelfImagePathCache_.end()) {
    return cached->second;
  }

  std::string imagePath = resolveShelfImagePath(bookPath);
  shelfImagePathCache_[bookPath] = imagePath;
  return imagePath;
}

/** Clears the cached library items so they are reloaded on next access. */
void LibraryActivity::invalidateLibraryCache() {
  cachedLibraryItemsValid_ = false;
  std::vector<LibraryItem>().swap(cachedLibraryItems_);
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
  const std::string cachePath = normalizeLibraryPath(path);
  auto cached = directoryHasBooksCache_.find(cachePath);
  if (cached != directoryHasBooksCache_.end()) {
    return cached->second;
  }

  auto dir = SdMan.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    directoryHasBooksCache_[cachePath] = false;
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
        directoryHasBooksCache_[cachePath] = true;
        return true;
      }
      file.close();
      continue;
    }

    std::string filename = name;
    if (isValidBookFile(filename)) {
      file.close();
      dir.close();
      directoryHasBooksCache_[cachePath] = true;
      return true;
    }
    file.close();
  }

  dir.close();
  directoryHasBooksCache_[cachePath] = false;
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

/** Loads all books recursively; assumes the caller already holds renderingMutex. */
void LibraryActivity::loadAllBooksRecursiveLocked() {
  invalidateLibraryCache();
  if (SETTINGS.useLibraryIndex) {
    loadLibraryFromIndex();
  } else {
    if (currentViewMode == ViewMode::BOOK_LIST_VIEW || currentViewMode == ViewMode::SHELF_VIEW) {
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
    const uint8_t aFlags = getBookStateFlags(a.path);
    const uint8_t bFlags = getBookStateFlags(b.path);
    const bool aIsReading = (aFlags & BOOK_STATE_READING) != 0;
    const bool aIsFinished = (aFlags & BOOK_STATE_FINISHED) != 0;
    const bool bIsReading = (bFlags & BOOK_STATE_READING) != 0;
    const bool bIsFinished = (bFlags & BOOK_STATE_FINISHED) != 0;

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

/** Reapplies pagination bounds and refreshes currentPageItems from cachedLibraryItems_. */
void LibraryActivity::applyPaginationToCachedItems() {
  if (currentViewMode == ViewMode::SHELF_VIEW) {
    // Shelf is cover-first; .txt/.md never get a generated cover thumbnail (only EPUB/XTC do), so
    // including them would just fill slots with title-only placeholder cards. Drop them here rather
    // than from the shared book cache, so List/Tag views still show every supported format.
    cachedLibraryItems_.erase(std::remove_if(cachedLibraryItems_.begin(), cachedLibraryItems_.end(),
                                             [](const LibraryItem& item) {
                                               return item.type == LibraryItem::Type::BOOK &&
                                                      !StringUtils::checkFileExtension(item.path, ".epub") &&
                                                      !StringUtils::checkFileExtension(item.path, ".xtc");
                                             }),
                              cachedLibraryItems_.end());
  }
  const int totalItems = static_cast<int>(cachedLibraryItems_.size());
  itemsPerPage = isLibraryGridMode()
                     ? GRID_ITEMS_PER_PAGE
                     : (currentViewMode == ViewMode::SHELF_VIEW
                            ? SHELF_ITEMS_PER_PAGE
                            : (currentViewMode == ViewMode::FOLDER_VIEW ? FOLDER_ITEMS_PER_PAGE : BOOK_ITEMS_PER_PAGE));
  totalPages = (totalItems + itemsPerPage - 1) / itemsPerPage;
  if (totalPages == 0) totalPages = 1;

  if (currentPage >= totalPages) currentPage = totalPages - 1;
  if (currentPage < 0) currentPage = 0;

  currentPageItems.clear();
  const int startIdx = currentPage * itemsPerPage;
  const int endIdx = std::min(startIdx + itemsPerPage, totalItems);
  for (int i = startIdx; i < endIdx; ++i) {
    currentPageItems.push_back(cachedLibraryItems_[static_cast<size_t>(i)]);
  }
  cachedLibraryItemsValid_ = true;
}

/**
 * @brief Apply pagination to sorted books and load current page
 * @param tempBooks Sorted vector of temporary book entries
 */
void LibraryActivity::applyPaginationToBooks(const std::vector<TempBookEntry>& tempBooks) {
  cachedLibraryItems_.clear();
  for (const auto& temp : tempBooks) {
    LibraryItem item;
    item.type = LibraryItem::Type::BOOK;
    item.name = getBaseFilename(temp.path);
    item.path = temp.path;
    item.displayName = temp.displayName;
    item.folderPath = temp.folderPath;
    cachedLibraryItems_.push_back(std::move(item));
  }
  applyPaginationToCachedItems();
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
  cachedLibraryItems_.clear();
  for (const auto& folder : tempFolders) {
    cachedLibraryItems_.push_back(folder);
  }

  for (const auto& temp : tempBooks) {
    LibraryItem item;
    item.type = LibraryItem::Type::BOOK;
    item.name = getBaseFilename(temp.path);
    item.path = temp.path;
    item.displayName = temp.displayName;
    item.folderPath = temp.folderPath;
    cachedLibraryItems_.push_back(std::move(item));
  }
  applyPaginationToCachedItems();
}

/** Restores the current selection to the item matching the given path, if present. */
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

/** Restores the current selection to the item matching the given tag key, if present. */
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
  return (getBookStateFlags(path) & BOOK_STATE_FAVORITE) != 0;
}

/**
 * @brief Check if a book is currently being read
 * @param path Path to the book
 * @return true if book is opened/reading
 */
bool LibraryActivity::isBookOpened(const std::string& path) const {
  return (getBookStateFlags(path) & BOOK_STATE_READING) != 0;
}

/**
 * @brief Check if a book is marked as finished
 * @param path Path to the book
 * @return true if book is finished
 */
bool LibraryActivity::isBookFinished(const std::string& path) const {
  return (getBookStateFlags(path) & BOOK_STATE_FINISHED) != 0;
}

/**
 * @brief Render the library screen
 */
void LibraryActivity::render() const {
  const int headerY = mainContentTop();
  const int dividerY = mainHeaderDividerY();
  const int gridStartY = dividerY - 3;

  // Shelf mode decodes real cover thumbnails - expensive. If only the selection moved (same page,
  // same folder/tag, same item count as last render), skip the whole repaint: restore the framebuffer
  // snapshot from the last full render (instant memcpy) and redraw just the newly-selected card on top.
  const bool canUseShelfBuffer = !isIndexing_ && !isInitialLoading_ && canUseLibraryShelfBuffer();
  if (canUseShelfBuffer && restoreLibraryShelfBuffer()) {
    drawShelfSelectionOverlay(gridStartY);
    renderer.displayBuffer();
    return;
  }

  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth() - 1;

  renderTabBar(renderer);

  std::string headerText = getHeaderText();
  int headerTextX = 20;
  int headerTextY = headerY + (TAB_BAR_HEIGHT - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  const bool showIndexButton = shouldShowIndexButton();
  int containerWidth = screenWidth - 110;
  if (showIndexButton) {
    containerWidth -= 64;
  }

  bool headerSelected = isHeaderButtonSelected && tabSelectorIndex == 1;
  if (headerSelected)
    renderer.rectangle.fill(0, headerY, containerWidth, TAB_BAR_HEIGHT, static_cast<int>(GfxRenderer::FillTone::Ink));

  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, headerTextX, headerTextY, headerText.c_str(), !headerSelected,
                       EpdFontFamily::BOLD);
  int headerButtonRightX = drawSortButton(headerY, TAB_BAR_HEIGHT, screenWidth);
  if (showIndexButton) {
    drawIndexButton(headerY, TAB_BAR_HEIGHT, headerButtonRightX + 10, isIndexButtonSelected && tabSelectorIndex == 1);
  }

  renderer.line.render(0, dividerY, screenWidth, dividerY);

  if (isInitialLoading_) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, gridStartY + 130, "Loading library");
  } else if (isIndexing_) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, gridStartY + 130, "Refreshing library");
  } else {
    suppressShelfSelectionHighlight_ = canUseShelfBuffer;
    renderLibraryList(gridStartY);
    suppressShelfSelectionHighlight_ = false;

    drawButtonHints();
  }

  if (canUseShelfBuffer) {
    storeLibraryShelfBuffer();
    drawShelfSelectionOverlay(gridStartY);
  } else {
    freeLibraryShelfBuffer();
  }

  renderer.displayBuffer();

  if (pendingShelfExitHalfRefresh_) {
    // Shelf mode's dithered cover thumbnails can leave ghosting a normal refresh doesn't fully clear -
    // follow the page we just left shelf mode with with a half refresh to clean it up.
    pendingShelfExitHalfRefresh_ = false;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

/**
 * @brief Toggle between book list view and folder view
 */
void LibraryActivity::toggleViewMode() {
  if (currentViewMode == ViewMode::FOLDER_VIEW) {
    switchToBookListView();
    return;
  }
  if (currentViewMode == ViewMode::BOOK_LIST_VIEW) {
    if (SETTINGS.useLibraryIndex) {
      switchToTagView();
    } else {
      switchToShelfView();
    }
    return;
  }
  if (currentViewMode == ViewMode::TAG_VIEW) {
    switchToShelfView();
    return;
  }
  switchToFolderView();
}

/** Frees the shelf page buffer and flags a cleanup half refresh if currently in shelf mode. */
void LibraryActivity::leaveShelfViewIfNeeded() {
  if (currentViewMode != ViewMode::SHELF_VIEW) {
    return;
  }
  freeLibraryShelfBuffer();
  pendingShelfExitHalfRefresh_ = true;
}

/** Switches the current view mode to the flat book list view. */
void LibraryActivity::switchToBookListView() {
  leaveShelfViewIfNeeded();
  if (currentViewMode == ViewMode::FOLDER_VIEW) {
    savedFolderPath = basepath;
  }
  currentViewMode = ViewMode::BOOK_LIST_VIEW;
  selectedTagKey_.clear();
  basepath = "/";
  resetNavigation();
  loadAllBooksRecursive();
  updateRequired = true;
}

/** Switches the current view mode to the tag collection view. */
void LibraryActivity::switchToTagView() {
  if (!SETTINGS.useLibraryIndex) {
    switchToFolderView();
    return;
  }

  leaveShelfViewIfNeeded();
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

/** Switches the current view mode to the cover shelf view. */
void LibraryActivity::switchToShelfView() {
  if (currentViewMode == ViewMode::FOLDER_VIEW) {
    savedFolderPath = basepath;
  }
  currentViewMode = ViewMode::SHELF_VIEW;
  selectedTagKey_.clear();
  basepath = "/";
  resetNavigation();
  beginLibraryLoadWithLoadingScreen();
}

/**
 * @brief Switch to folder view mode
 */
void LibraryActivity::switchToFolderView() {
  leaveShelfViewIfNeeded();
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

/** Returns whether the index refresh button should be shown for the current view. */
bool LibraryActivity::shouldShowIndexButton() const { return SETTINGS.useLibraryIndex != 0; }

/** Starts a background library indexing pass. */
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
      "LibIdxBtnTask", LIBRARY_INDEX_TASK_STACK_SIZE, this, 1, nullptr);

  if (created != pdPASS) {
    isIndexing_ = false;
    updateRequired = true;
  }
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
  invalidateLibraryCache();
  bookStateCache_.clear();
  directoryHasBooksCache_.clear();
  shelfImagePathCache_.clear();
  renderer.clearScreen(0xff);

  currentViewMode = storageToViewMode(SETTINGS.libraryViewMode, SETTINGS.useLibraryIndex != 0);
  selectedTagKey_.clear();
  cachedTagEntries_.clear();
  cachedTagEntriesLoaded_ = false;
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

  if (displayTaskHandle == nullptr) {
    xTaskCreate(&LibraryActivity::taskTrampoline, "LibraryTask", LIBRARY_TASK_STACK_SIZE, this, 1, &displayTaskHandle);
  }

  // Folder/List/Tag loads are fast enough not to need a loading placeholder - only shelf mode's flat
  // all-books scan, plus the cover thumbnail decodes right after, is slow enough to warrant one.
  if (currentViewMode == ViewMode::SHELF_VIEW) {
    beginLibraryLoadWithLoadingScreen();
  } else {
    loadAllBooksRecursive();
    updateRequired = true;
  }
}

/** Shows a "Loading library" placeholder, then runs loadAllBooksRecursive() on a background task. */
void LibraryActivity::beginLibraryLoadWithLoadingScreen() {
  // Render a lightweight "Loading library" placeholder right now, before the load starts, so the
  // screen doesn't sit frozen while shelf mode's flat all-books scan (and cover thumbnail decodes
  // right after) run; the actual load then happens on a background task so this placeholder is
  // guaranteed to be visible first instead of racing it.
  isInitialLoading_ = true;
  {
    MutexGuard guard(renderingMutex);
    if (!renderingMutex || guard.isAcquired()) {
      render();
    }
  }

  const BaseType_t created = xTaskCreate(
      [](void* param) {
        auto* activity = static_cast<LibraryActivity*>(param);
        activity->loadAllBooksRecursive();
        activity->isInitialLoading_ = false;
        activity->updateRequired = true;
        vTaskDelete(nullptr);
      },
      "LibInitLoadTask", LIBRARY_TASK_STACK_SIZE, this, 1, nullptr);

  if (created != pdPASS) {
    // Couldn't spawn the loader task - fall back to loading synchronously rather than leaving the
    // activity stuck showing "Loading library" forever.
    loadAllBooksRecursive();
    isInitialLoading_ = false;
    updateRequired = true;
  }
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
  // Shelf mode is never persisted as the resumed view - it decodes real cover thumbnails on entry,
  // which is a bad first thing to wait on when you just wanted to jump to Settings. Land back on
  // Collection (folder view) next time instead.
  SETTINGS.libraryViewMode =
      viewModeToStorage(currentViewMode == ViewMode::SHELF_VIEW ? ViewMode::FOLDER_VIEW : currentViewMode);
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
  std::vector<LibraryItem>().swap(currentPageItems);
  std::vector<LibraryItem>().swap(cachedLibraryItems_);
  cachedLibraryItemsValid_ = false;
  std::vector<BookTags::Entry>().swap(cachedTagEntries_);
  bookStateCache_.clear();
  directoryHasBooksCache_.clear();
  freeLibraryShelfBuffer();
  shelfImagePathCache_.clear();
  cachedTagEntriesLoaded_ = false;
  std::string().swap(savedFolderPath);
  std::string().swap(selectedTagKey_);
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
    if (cachedLibraryItemsValid_) {
      applyPaginationToCachedItems();
    } else {
      loadAllBooksRecursiveLocked();
    }
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
    if (cachedLibraryItemsValid_) {
      applyPaginationToCachedItems();
    } else {
      loadAllBooksRecursiveLocked();
    }
    // Land on the LAST item of the previous page (not the first): the user pressed up off the top item, so
    // continuing onto the bottom of the previous page reads naturally.
    selectorIndex = std::max(0, static_cast<int>(currentPageItems.size()) - 1);
    listScrollOffset = 0;
    updateRequired = true;
  }
}

/**
 * @brief Main loop for handling user input
 */
void LibraryActivity::loop() {
  // While the background loader task is populating currentPageItems/cachedLibraryItems_ (see onEnter's
  // "Loading library" placeholder), input handling below reads those same containers without taking
  // renderingMutex - ignore input entirely until the load finishes rather than race it.
  if (isInitialLoading_) {
    return;
  }

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
    renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
    updateRequired = true;
    return;
  }

  std::vector<LibraryItem>& currentList = currentPageItems;
  const int itemCount = static_cast<int>(currentList.size());

  bool wantDownStep = false;
  bool wantUpStep = false;
  // Item-list step buttons depend on the main-menu nav setting (front: Up/Down, side: Left/Right).
  const MappedInputManager::Button itemPrevBtn = itemPrevButton();
  const MappedInputManager::Button itemNextBtn = itemNextButton();
  if (tabSelectorIndex == 1) {
    if (Activity::mappedInput.wasPressed(itemNextBtn)) {
      wantDownStep = true;
      libraryListDownNextMs = millis() + LIB_LIST_REPEAT_INITIAL_MS;
    } else if (Activity::mappedInput.isPressed(itemNextBtn)) {
      if (libraryListDownNextMs != 0 && millis() >= libraryListDownNextMs) {
        wantDownStep = true;
        libraryListDownNextMs = millis() + LIB_LIST_REPEAT_RATE_MS;
      }
    } else {
      libraryListDownNextMs = 0;
    }

    if (Activity::mappedInput.wasPressed(itemPrevBtn)) {
      wantUpStep = true;
      libraryListUpNextMs = millis() + LIB_LIST_REPEAT_INITIAL_MS;
    } else if (Activity::mappedInput.isPressed(itemPrevBtn)) {
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

  // Tab switch buttons depend on the same setting (front: Left/Right, side: Up/Down).
  const bool leftPressed = Activity::mappedInput.wasPressed(tabPrevButton());
  const bool rightPressed = Activity::mappedInput.wasPressed(tabNextButton());
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
          BOOK_STATE.compactForIdle();
        }
      } else {
        const bool makeFavorite = !book->isFavorite;
        book->isFavorite = makeFavorite;
        if (makeFavorite && book->title.empty()) {
          book->title = item.displayName;
        }
        BOOK_STATE.saveToFile();
        BOOK_STATE.compactForIdle();
      }

      bookStateCache_.erase(item.path);
      if (favoritesPromoted) {
        loadAllBooksRecursive();
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
      book->isReading = true;
      if (book->title.empty()) {
        book->title = item.displayName;
      }
    }

    BOOK_STATE.saveToFile();
    BOOK_STATE.compactForIdle();
    bookStateCache_.erase(item.path);
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
    switchToShelfView();
    return;
  }

  if (currentViewMode == ViewMode::BOOK_LIST_VIEW && SETTINGS.useLibraryIndex) {
    switchToTagView();
    return;
  }

  if (currentViewMode == ViewMode::SHELF_VIEW) {
    switchToFolderView();
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

/** Whether the current folder browser should use the 3x4 grid layout. */
bool LibraryActivity::isLibraryGridMode() const {
  if (SETTINGS.libraryMode != SystemSetting::LIBRARY_GRID) {
    return false;
  }
  if (currentViewMode == ViewMode::FOLDER_VIEW) {
    return true;
  }
  if (currentViewMode == ViewMode::TAG_VIEW) {
    return selectedTagKey_.empty();
  }
  return false;
}

/** Returns whether the current view mode is the tag collection view. */
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
    renderLibraryGrid(startY);
    return;
  }

  if (currentViewMode == ViewMode::SHELF_VIEW) {
    renderLibraryShelf(startY);
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
      renderer.line.render(0, drawY + itemHeight - 1, screenWidth, drawY + itemHeight - 1, true,
                           LineRender::Style::Dotted);
    }

    drawY += itemHeight;
    itemsDrawn++;
  }
}

/** Computes the pixel size of a shelf-mode cover slot for the current screen. */
void LibraryActivity::getShelfCoverSize(GfxRenderer& renderer, int& outCoverW, int& outCoverH) {
  const int startY = INX_THEME.mainContentTop() + TAB_BAR_HEIGHT - 3;
  const int screenW = renderer.getScreenWidth();
  const int screenH = INX_THEME.mainContentBottom(renderer) - 10;
  const int availableW = screenW - LIB_SHELF_OUTER_PAD_X * 2;
  const int availableH = screenH - startY - LIB_SHELF_OUTER_PAD_Y * 2;
  // Card size must account for (COLS-1)/(ROWS-1) inter-card gaps, not a single flat gap - otherwise a
  // 3-wide row overflows availableW by one gap's worth (only noticeable once COLS/ROWS > 2).
  const int rawW = std::max(90, (availableW - LIB_SHELF_GAP_X * (LIB_SHELF_COLS - 1)) / LIB_SHELF_COLS);
  const int rawH = std::max(130, (availableH - LIB_SHELF_GAP_Y * (LIB_SHELF_ROWS - 1)) / LIB_SHELF_ROWS);
  // Shrunk 5% below the max-fit size so the grid has breathing room instead of feeling cramped;
  // renderShelfCard's origin centering absorbs the freed space evenly around the whole grid.
  outCoverW = std::max(90, static_cast<int>(rawW * 0.96f));
  outCoverH = std::max(130, static_cast<int>(rawH * 0.96f));
}

/** Renders a single shelf card (cover thumbnail and selection state) at the given grid index. */
void LibraryActivity::renderShelfCard(const int index, const int startY, const bool selected) const {
  if (index < 0 || index >= static_cast<int>(currentPageItems.size()) || index >= SHELF_ITEMS_PER_PAGE) {
    return;
  }
  const LibraryItem& item = currentPageItems[static_cast<size_t>(index)];
  const int screenW = renderer.getScreenWidth();
  const int screenH = mainContentBottom(renderer) - 30;
  const int availableW = screenW - LIB_SHELF_OUTER_PAD_X * 2;
  const int availableH = screenH - startY - LIB_SHELF_OUTER_PAD_Y * 2;
  int cardW = 0;
  int cardH = 0;
  getShelfCoverSize(renderer, cardW, cardH);
  const int totalW = cardW * LIB_SHELF_COLS + LIB_SHELF_GAP_X * (LIB_SHELF_COLS - 1);
  const int totalH = cardH * LIB_SHELF_ROWS + LIB_SHELF_GAP_Y * (LIB_SHELF_ROWS - 1);
  const int originX = LIB_SHELF_OUTER_PAD_X + std::max(0, (availableW - totalW) / 2);
  const int originY = startY + LIB_SHELF_OUTER_PAD_Y + std::max(0, (availableH - totalH) / 2);

  const int row = index / LIB_SHELF_COLS;
  const int col = index % LIB_SHELF_COLS;
  // Cover fills the whole card - no separate outer card frame around it. The old design had a card
  // border plus a second, smaller border around the cover with a dead gap in between; now there's
  // exactly one border, at the cover's own edge. Selection is a solid thick black frame in the
  // inter-card gap (a dithered checker pattern was tried first but was too subtle to read on e-ink).
  const int coverX = originX + col * (cardW + LIB_SHELF_GAP_X);
  const int coverY = originY + row * (cardH + LIB_SHELF_GAP_Y);
  const int coverW = cardW;
  const int coverH = cardH;
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  const bool subtle = SETTINGS.bitmapRoundedCorners == 2;

  if (selected) {
    constexpr int kSelectionBorder = 4;
    renderer.rectangle.fill(coverX - kSelectionBorder, coverY - kSelectionBorder, coverW + kSelectionBorder * 2,
                            coverH + kSelectionBorder * 2, true, rounded, subtle);
  }

  renderer.rectangle.fill(coverX, coverY, coverW, coverH, false, rounded, subtle);
  renderer.rectangle.render(coverX, coverY, coverW, coverH, true, rounded, subtle);

  bool drewCover = false;
  const std::string imagePath = getShelfImagePath(item.path);
  if (!imagePath.empty()) {
    ImageRender::Options options;
    options.cropToFill = true;
    options.useDisplayCache = true;
    options.roundedOutside = !rounded
                                 ? BitmapRender::RoundedOutside::None
                                 : subtle ? BitmapRender::RoundedOutside::SubtlePaperOutside
                                          : BitmapRender::RoundedOutside::PaperOutside;
    drewCover =
        ImageRender::create(renderer, imagePath).render(coverX + 1, coverY + 1, coverW - 2, coverH - 2, options);
  }
  if (!drewCover) {
    if (item.type == LibraryItem::Type::FOLDER) {
      renderGridItemIcon(item, coverX, coverY, coverW, coverH, selected, true);
    } else {
      // Books without a cover show their title centered in the card, same fallback as the Recent page
      // thumbnails (drawRecentNoCoverPlaceholder) instead of a generic book icon.
      drawShelfNoCoverTitle(renderer, coverX + 4, coverY, coverW - 8, coverH, item.displayName,
                            ATKINSON_HYPERLEGIBLE_10_FONT_ID);
    }
  }

  int badgeY = coverY + 3;
  const int badgeX = coverX + coverW - LIB_SHELF_BADGE_SIZE - 3;
  if (isBookMarked(item.path)) {
    drawShelfFavoriteBadge(renderer, badgeX, badgeY);
    badgeY += LIB_SHELF_BADGE_SIZE + 3;
  }
  if (isBookFinished(item.path)) {
    drawShelfCheckBadge(renderer, badgeX, badgeY);
  }
}

/** Renders the cover shelf grid view. */
void LibraryActivity::renderLibraryShelf(int startY) const {
  const int count = std::min(static_cast<int>(currentPageItems.size()), SHELF_ITEMS_PER_PAGE);
  for (int i = 0; i < count; ++i) {
    const bool selected = !suppressShelfSelectionHighlight_ && tabSelectorIndex == 1 && selectorIndex == i &&
                          !isHeaderButtonSelected && !isIndexButtonSelected && !isSortButtonSelected;
    renderShelfCard(i, startY, selected);
  }
}

/** Returns whether the stored shelf page buffer can be reused for the current page/selection. */
bool LibraryActivity::canUseLibraryShelfBuffer() const {
  return currentViewMode == ViewMode::SHELF_VIEW && !currentPageItems.empty();
}

/** Snapshots the current framebuffer into the shelf page buffer. */
bool LibraryActivity::storeLibraryShelfBuffer() const {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }
  freeLibraryShelfBuffer();
  const size_t bufferSize = renderer.getBufferSize();
  libraryShelfPageBuffer_ = static_cast<uint8_t*>(malloc(bufferSize));
  if (!libraryShelfPageBuffer_) {
    return false;
  }
  memcpy(libraryShelfPageBuffer_, frameBuffer, bufferSize);
  libraryShelfPageBufferStored_ = true;
  libraryShelfPageBufferPage_ = currentPage;
  libraryShelfPageBufferItemCount_ = static_cast<int>(currentPageItems.size());
  // Shelf is always the flat all-books listing (basepath == "/"), but keying on it anyway costs
  // nothing and keeps this correct if shelf ever becomes folder/tag-scoped again.
  libraryShelfPageBufferKey_ = basepath;
  return true;
}

/** Restores the framebuffer from the stored shelf page buffer. */
bool LibraryActivity::restoreLibraryShelfBuffer() const {
  if (!libraryShelfPageBufferStored_ || !libraryShelfPageBuffer_ || libraryShelfPageBufferPage_ != currentPage ||
      libraryShelfPageBufferItemCount_ != static_cast<int>(currentPageItems.size()) ||
      libraryShelfPageBufferKey_ != basepath) {
    return false;
  }
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }
  memcpy(frameBuffer, libraryShelfPageBuffer_, renderer.getBufferSize());
  return true;
}

/** Frees the stored shelf page buffer, if any. */
void LibraryActivity::freeLibraryShelfBuffer() const {
  uint8_t* buffer = libraryShelfPageBuffer_;
  libraryShelfPageBuffer_ = nullptr;
  if (buffer) {
    std::free(buffer);
  }
  libraryShelfPageBufferStored_ = false;
  libraryShelfPageBufferPage_ = -1;
  libraryShelfPageBufferItemCount_ = -1;
  libraryShelfPageBufferKey_.clear();
}

/** Draws the selection highlight overlay on top of the restored shelf buffer. */
void LibraryActivity::drawShelfSelectionOverlay(int startY) const {
  if (selectorIndex < 0 || selectorIndex >= static_cast<int>(currentPageItems.size()) ||
      selectorIndex >= SHELF_ITEMS_PER_PAGE) {
    return;
  }
  if (tabSelectorIndex != 1 || isHeaderButtonSelected || isIndexButtonSelected || isSortButtonSelected) {
    return;
  }
  renderShelfCard(selectorIndex, startY, true);
}

/** Renders a large built-in folder or book icon for a grid item. */
void LibraryActivity::renderGridItemIcon(const LibraryItem& item, int x, int y, int w, int h, bool isSelected,
                                         bool isLarge) const {
  const int iconSize = std::min(72, std::max(32, std::min(w, h) - 12));
  const int iconX = x + (w - iconSize) / 2;
  const int iconY = y + (h - iconSize) / 2;
  if (item.type == LibraryItem::Type::FOLDER) {
    renderer.bitmap.icon(isLarge ? FolderLarge : Folder, iconX, iconY, iconSize, iconSize,
                         BitmapRender::Orientation::None, isSelected);
  } else {
    if (isBookMarked(item.path)) {
      const int starSize = 18;
      renderer.bitmap.icon(Star, x + w - starSize - 10, y, starSize, starSize, BitmapRender::Orientation::None,
                           isSelected);
    }
    renderer.bitmap.icon(isLarge ? BookLarge : Book, iconX, iconY, iconSize, iconSize, BitmapRender::Orientation::None,
                         isSelected);
  }
}

/** Renders the folder browser as a 3x4 icon grid. */
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
    const bool selected = tabSelectorIndex == 1 && selectorIndex == i && !isHeaderButtonSelected &&
                          !isIndexButtonSelected && !isSortButtonSelected;

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
  currentPageItems.clear();

  FsFile idxFile = SdMan.open("/.metadata/library/library.idx", O_READ);
  if (!idxFile) return;

  idxFile.seek(5);

  std::string cleanBase = basepath;
  if (cleanBase.length() > 1 && cleanBase.back() == '/') {
    cleanBase.pop_back();
  }

  if (currentViewMode == ViewMode::BOOK_LIST_VIEW || currentViewMode == ViewMode::SHELF_VIEW || isTagViewMode()) {
    loadBooksFromIndex(idxFile, cleanBase);
  } else {
    loadFoldersFromIndex(idxFile, cleanBase);
  }
  idxFile.close();
}

/** Loads cached tag entries into cachedTagEntries_ if not already loaded. */
void LibraryActivity::ensureTagEntriesLoaded() {
  if (cachedTagEntriesLoaded_) {
    return;
  }
  cachedTagEntries_.clear();
  BookTags::load(cachedTagEntries_);
  cachedTagEntriesLoaded_ = true;
}

/** Returns the cached tag key for a book path, or an empty string if none is cached. */
std::string LibraryActivity::findCachedTag(const std::string& path) const {
  return BookTags::find(cachedTagEntries_, path);
}

/**
 * @brief Load books from index file for book list view
 * @param idxFile Open index file
 * @param cleanBase Cleaned base path
 */
void LibraryActivity::loadBooksFromIndex(FsFile& idxFile, const std::string& cleanBase) {
  std::vector<TempBookEntry> tempBooks;
  const bool useTags = isTagViewMode() || currentSortMode == SortMode::TAG_AZ || currentSortMode == SortMode::TAG_ZA;
  if (useTags) {
    ensureTagEntriesLoaded();
  }
  std::vector<LibraryItem> tagFolders;
  size_t indexEntries = 0;

  while (idxFile.available()) {
    uint8_t marker;
    if (idxFile.read(&marker, 1) != 1) break;

    if (marker == 0x01) {
      TempBookEntry tempEntry = readBookEntryFromIndex(idxFile);
      if (useTags) {
        tempEntry.tag = findCachedTag(tempEntry.path);
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
  const bool useTags = currentSortMode == SortMode::TAG_AZ || currentSortMode == SortMode::TAG_ZA;
  if (useTags) {
    ensureTagEntriesLoaded();
  }
  size_t indexEntries = 0;

  while (idxFile.available()) {
    uint8_t marker;
    if (idxFile.read(&marker, 1) != 1) break;

    if (marker == 0x01) {
      TempBookEntry tempEntry = readBookEntryFromIndex(idxFile);
      if (useTags) {
        tempEntry.tag = findCachedTag(tempEntry.path);
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
  tempEntry.path.resize(pLen);
  if (pLen > 0) {
    idxFile.read(&tempEntry.path[0], pLen);
  }

  uint8_t nLen;
  idxFile.read(&nLen, sizeof(nLen));
  idxFile.seek(idxFile.position() + nLen);

  uint8_t dLen;
  idxFile.read(&dLen, sizeof(dLen));
  tempEntry.displayName.resize(dLen);
  if (dLen > 0) {
    idxFile.read(&tempEntry.displayName[0], dLen);
  }

  uint8_t fLen;
  idxFile.read(&fLen, sizeof(fLen));
  tempEntry.folderPath.resize(fLen);
  if (fLen > 0) {
    idxFile.read(&tempEntry.folderPath[0], fLen);
  }

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
  std::string dirPath;
  dirPath.resize(pathLen);
  if (pathLen > 0) {
    idxFile.read(&dirPath[0], pathLen);
  }

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
