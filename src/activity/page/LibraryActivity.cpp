#include "LibraryActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <algorithm>
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

namespace {

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

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long FAVORITE_HOLD_MS = 500;
}  // namespace

LibraryActivity::LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 const std::function<void()>& onGoToRecent,
                                 const std::function<void(const std::string& path)>& onSelectBook,
                                 const std::function<void()>& onRecentOpen, const std::function<void()>& onSettingsOpen,
                                 std::string initialPath)
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
      currentSortMode(SortMode::TITLE_AZ) {
  tabSelectorIndex = 1;
}

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

std::string LibraryActivity::getBaseFilename(const std::string& filename) const {
  size_t lastSlash = filename.find_last_of('/');
  std::string basename = (lastSlash != std::string::npos) ? filename.substr(lastSlash + 1) : filename;

  size_t lastDot = basename.find_last_of('.');
  if (lastDot != std::string::npos) {
    basename = basename.substr(0, lastDot);
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

std::string LibraryActivity::getHeaderText() const {
  if (basepath == "/") {
    return currentViewMode == ViewMode::BOOK_LIST_VIEW ? "All Books" : "Collection";
  }

  std::string folderName = extractFolderName(basepath);
  std::string header = formatFolderName(folderName);
  return truncateTextIfNeeded(header, 25);
}

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

std::string LibraryActivity::truncateTextIfNeeded(const std::string& text, size_t maxLength) const {
  if (text.length() > maxLength) {
    return text.substr(0, maxLength - 3) + "...";
  }
  return text;
}

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

int LibraryActivity::drawSortButton(int headerY, int headerHeight, int rightX) const {
  std::string buttonText = getSortButtonText();
  bool isSelected = isSortButtonSelected && tabSelectorIndex == 1;
  return drawHeaderButton(buttonText, headerY, headerHeight, rightX, isSelected);
}

void LibraryActivity::drawButtonHints() const {
  std::string back = currentViewMode == ViewMode::FOLDER_VIEW ? basepath != "/" ? "« Back" : "Books »" : "« Groups";
  std::string select = "Select";

  const auto labels = Activity::mappedInput.mapLabels(back.c_str(), select.c_str(), "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void LibraryActivity::findBooksRecursively(const std::string& path, std::vector<LibraryItem>& books) {
  auto dir = SdMan.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
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
      findBooksRecursively(fullPath, books);
      file.close();
      continue;
    }

    std::string filename = name;
    if (StringUtils::checkFileExtension(filename, ".epub") || StringUtils::checkFileExtension(filename, ".xtch") ||
        StringUtils::checkFileExtension(filename, ".xtc") || StringUtils::checkFileExtension(filename, ".txt") ||
        StringUtils::checkFileExtension(filename, ".md")) {
      LibraryItem bookItem;
      bookItem.type = LibraryItem::Type::BOOK;
      bookItem.name = filename;
      bookItem.path = fullPath;
      bookItem.displayName = formatFolderName(getBaseFilename(filename));

      bookItem.folderPath = extractFolderName(path);
      if (bookItem.folderPath.empty() || path == "/") {
        bookItem.folderPath = "Library";
      }

      books.push_back(bookItem);
    }
    file.close();
  }
  dir.close();
}

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
    if (StringUtils::checkFileExtension(filename, ".epub") || StringUtils::checkFileExtension(filename, ".xtch") ||
        StringUtils::checkFileExtension(filename, ".xtc") || StringUtils::checkFileExtension(filename, ".txt") ||
        StringUtils::checkFileExtension(filename, ".md")) {
      file.close();
      dir.close();
      return true;
    }

    file.close();
  }

  dir.close();
  return false;
}

void LibraryActivity::loadAllBooksRecursive() {
  if (SETTINGS.useLibraryIndex) {
    loadLibraryFromIndex();
  } else {
    if (currentViewMode == ViewMode::BOOK_LIST_VIEW) {
      allBooksList.clear();
      findBooksRecursively(basepath, allBooksList);
      sortByTitle(true);
    } else {
      loadLibraryItems();
    }
  }
}

void LibraryActivity::taskTrampoline(void* param) { static_cast<LibraryActivity*>(param)->displayTaskLoop(); }

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

bool LibraryActivity::isBookMarked(const std::string& path) const {
  auto* book = BOOK_STATE.findBookByPath(path);
  return book ? book->isFavorite : false;
}

bool LibraryActivity::isBookOpened(const std::string& path) const {
  auto* book = BOOK_STATE.findBookByPath(path);
  return book ? book->isReading : false;
}

void LibraryActivity::render() const {
  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth() - 1;
  const int screenHeight = renderer.getScreenHeight();

  renderTabBar(Activity::renderer, 0);

  const int startY = TAB_BAR_HEIGHT + 8;
  const int headerY = startY;

  std::string headerText = getHeaderText();
  int headerTextX = 20;
  int headerTextY = headerY + (TAB_BAR_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  int containerWidth = screenWidth - 110;

  bool headerSelected = isHeaderButtonSelected && tabSelectorIndex == 1;
  if (headerSelected) renderer.fillRect(0, headerY, containerWidth, TAB_BAR_HEIGHT);

  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, headerTextX, headerTextY, headerText.c_str(), !headerSelected,
                    EpdFontFamily::BOLD);
  drawSortButton(headerY, TAB_BAR_HEIGHT, screenWidth);

  renderer.drawLine(0, headerY + TAB_BAR_HEIGHT, screenWidth, headerY + TAB_BAR_HEIGHT);

  renderLibraryList(TAB_BAR_HEIGHT * 2 + 6);
  drawButtonHints();
  renderer.displayBuffer();
}

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

  loadAllBooksRecursive();
  applySorting();

  const std::vector<LibraryItem>& currentList =
      (currentViewMode == ViewMode::BOOK_LIST_VIEW) ? allBooksList : libraryItems;

  bool hasItems = !currentList.empty();
  selectorIndex = hasItems ? 0 : -1;
  isHeaderButtonSelected = !hasItems;
  isSortButtonSelected = false;
  listScrollOffset = 0;
  updateRequired = true;
}

void LibraryActivity::switchToFolderView() {
  currentViewMode = ViewMode::FOLDER_VIEW;

  if (!savedFolderPath.empty()) {
    basepath = savedFolderPath;
    savedFolderPath.clear();
  }

  libraryItems.clear();
  loadAllBooksRecursive();
  applySorting();

  bool hasItems = !libraryItems.empty();
  selectorIndex = hasItems ? 0 : -1;
  isHeaderButtonSelected = !hasItems;
  isSortButtonSelected = false;
  listScrollOffset = 0;
  updateRequired = true;
}

void LibraryActivity::onEnter() {
  Activity::onEnter();
  renderer.clearScreen();
  renderingMutex = xSemaphoreCreateMutex();
  if (!renderingMutex) return;
  renderer.clearScreen(0xff);


  loadAllBooksRecursive();
  currentViewMode = ViewMode::FOLDER_VIEW;
  applySorting();

  const std::vector<LibraryItem>& currentList =
      (currentViewMode == ViewMode::BOOK_LIST_VIEW) ? allBooksList : libraryItems;
  bool hasItems = !currentList.empty();
  selectorIndex = hasItems ? 0 : -1;
  isHeaderButtonSelected = !hasItems;
  isSortButtonSelected = false;
  listScrollOffset = 0;
  tabSelectorIndex = 1;

  if (displayTaskHandle == nullptr) {
    xTaskCreate(&LibraryActivity::taskTrampoline, "LibraryTask", 4096, this, 1, &displayTaskHandle);
  }
  updateRequired = true;
}

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

void LibraryActivity::loop() {
    if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    updateRequired = true;
    return;
  }
  std::vector<LibraryItem>& currentList = (currentViewMode == ViewMode::BOOK_LIST_VIEW) ? allBooksList : libraryItems;
  const int itemCount = static_cast<int>(currentList.size());

  const bool upPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Down);
  const bool leftPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool rightPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool confirmPressed = Activity::mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  const bool confirmHeld = Activity::mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  const unsigned long holdTime = Activity::mappedInput.getHeldTime();

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

  if (tabSelectorIndex != 1) {
    return;
  }

  if (confirmHeld && holdTime >= FAVORITE_HOLD_MS) {
    if (!isHeaderButtonSelected && !isSortButtonSelected && selectorIndex >= 0 && selectorIndex < itemCount) {
      const LibraryItem& item = currentList[selectorIndex];

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
    return;
  }

  if (downPressed) {
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
        updateScrollPosition();
        updateRequired = true;
      }
      return;
    }

    if (selectorIndex < itemCount - 1) {
      selectorIndex++;
      updateScrollPosition();
      updateRequired = true;
    }
    return;
  }

  if (upPressed) {
    if (selectorIndex == 0) {
      selectorIndex = -1;
      isSortButtonSelected = true;
      updateRequired = true;
      return;
    }

    if (selectorIndex > 0) {
      selectorIndex--;
      updateScrollPosition();
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

  if (confirmPressed && holdTime < FAVORITE_HOLD_MS) {
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
      const LibraryItem& item = currentList[selectorIndex];

      if (currentViewMode == ViewMode::FOLDER_VIEW && item.type == LibraryItem::Type::FOLDER) {
        basepath = item.path;
        switchToFolderView();
        return;
      }

      // Will be useful later
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
    return;
  }

  if (Activity::mappedInput.wasPressed(MappedInputManager::Button::Back) && basepath == "/") {
    toggleViewMode();
    return;
  }

  if (Activity::mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    std::string newPath = basepath;

    if (!newPath.empty() && newPath.back() == '/') {
      newPath.pop_back();
    }

    size_t lastSlash = newPath.find_last_of('/');

    if (lastSlash == 0) {
      newPath = "/";
    } else if (lastSlash != std::string::npos) {
      newPath = newPath.substr(0, lastSlash + 1);
    } else {
      newPath = "/";
    }

    basepath = newPath;

    if (currentViewMode != ViewMode::FOLDER_VIEW) {
      currentViewMode = ViewMode::FOLDER_VIEW;
      savedFolderPath.clear();
    }

    loadAllBooksRecursive();
    applySorting();

    const std::vector<LibraryItem>& newList =
        (currentViewMode == ViewMode::BOOK_LIST_VIEW) ? allBooksList : libraryItems;

    bool hasItems = !newList.empty();
    selectorIndex = hasItems ? 0 : -1;
    isHeaderButtonSelected = !hasItems;
    isSortButtonSelected = false;
    listScrollOffset = 0;
    updateRequired = true;

    return;
  }
}

void LibraryActivity::applySorting() {
  if (currentViewMode == ViewMode::BOOK_LIST_VIEW) {
    switch (currentSortMode) {
      case SortMode::TITLE_AZ:
        sortByTitle(true);
        break;
      case SortMode::TITLE_ZA:
        sortByTitle(false);
        break;
      case SortMode::GROUP_AZ:
        sortByGroup(true);
        break;
      case SortMode::GROUP_ZA:
        sortByGroup(false);
        break;
    }
    return;
  }

  loadAllBooksRecursive();
  switch (currentSortMode) {
    case SortMode::TITLE_AZ:
      break;
    case SortMode::TITLE_ZA:
      std::reverse(libraryItems.begin(), libraryItems.end());
      break;
    case SortMode::GROUP_AZ:
      sortFolderViewByType(true);
      break;
    case SortMode::GROUP_ZA:
      sortFolderViewByType(false);
      break;
  }
}

void LibraryActivity::sortByTitle(bool ascending) {
  std::vector<LibraryItem>& items = (currentViewMode == ViewMode::BOOK_LIST_VIEW) ? allBooksList : libraryItems;

  // Split favorites and non-favorites
  std::vector<LibraryItem> favorites;
  std::vector<LibraryItem> others;

  for (const auto& item : items) {
    if (item.type == LibraryItem::Type::BOOK && isBookMarked(item.path)) {
      favorites.push_back(item);
    } else {
      others.push_back(item);
    }
  }

  // Sort both lists
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

  // Combine with favorites at the top
  items.clear();
  items.insert(items.end(), favorites.begin(), favorites.end());
  items.insert(items.end(), others.begin(), others.end());
}

void LibraryActivity::sortByGroup(bool ascending) {
  if (currentViewMode != ViewMode::BOOK_LIST_VIEW) return;

  std::sort(allBooksList.begin(), allBooksList.end(), [ascending](const LibraryItem& a, const LibraryItem& b) {
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

  // Move favorites to top
  std::vector<LibraryItem> favorites;
  std::vector<LibraryItem> others;

  for (const auto& item : allBooksList) {
    if (isBookMarked(item.path)) {
      favorites.push_back(item);
    } else {
      others.push_back(item);
    }
  }

  allBooksList.clear();
  allBooksList.insert(allBooksList.end(), favorites.begin(), favorites.end());
  allBooksList.insert(allBooksList.end(), others.begin(), others.end());
}

void LibraryActivity::cycleSortMode() {
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
      currentSortMode = SortMode::TITLE_AZ;
      break;
  }

  applySorting();

  selectorIndex = -1;
  isHeaderButtonSelected = false;
  isSortButtonSelected = true;
  listScrollOffset = 0;
  updateRequired = true;
}

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
  }
  return "Sort";
}

void LibraryActivity::resetLibraryView() {
  libraryItems.clear();
  allBooksList.clear();
  selectorIndex = -1;
  listScrollOffset = 0;
  updateRequired = true;
}

void LibraryActivity::sortFolderViewByType(bool foldersFirst) {
  std::vector<LibraryItem> folders;
  std::vector<LibraryItem> books;
  std::vector<LibraryItem> favorites;

  for (const auto& item : libraryItems) {
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

  libraryItems.clear();

  if (foldersFirst) {
    libraryItems.insert(libraryItems.end(), folders.begin(), folders.end());
    libraryItems.insert(libraryItems.end(), favorites.begin(), favorites.end());
    libraryItems.insert(libraryItems.end(), books.begin(), books.end());
  } else {
    libraryItems.insert(libraryItems.end(), favorites.begin(), favorites.end());
    libraryItems.insert(libraryItems.end(), books.begin(), books.end());
    libraryItems.insert(libraryItems.end(), folders.begin(), folders.end());
  }
}

void LibraryActivity::updateScrollPosition() {
  const std::vector<LibraryItem>& currentList =
      (currentViewMode == ViewMode::BOOK_LIST_VIEW) ? allBooksList : libraryItems;

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
    int itemHeight = 70;
    if (currentViewMode == ViewMode::FOLDER_VIEW && currentList[i].type == LibraryItem::Type::FOLDER) {
      itemHeight = LIST_ITEM_HEIGHT;
    }
    heightFromScrollToSelector += itemHeight;
  }

  if (heightFromScrollToSelector > visibleAreaHeight) {
    int accumulatedHeight = 0;
    for (int i = selectorIndex; i >= 0; i--) {
      int itemHeight = 70;
      if (currentViewMode == ViewMode::FOLDER_VIEW && currentList[i].type == LibraryItem::Type::FOLDER) {
        itemHeight = LIST_ITEM_HEIGHT;
      }
      accumulatedHeight += itemHeight;

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

void LibraryActivity::renderLibraryList(int startY) const {
  const std::vector<LibraryItem>& items = (currentViewMode == ViewMode::BOOK_LIST_VIEW) ? allBooksList : libraryItems;

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

  for (int i = listScrollOffset; i < static_cast<int>(items.size()); i++) {
    int itemHeight = 70;
    if (currentViewMode == ViewMode::FOLDER_VIEW && items[i].type == LibraryItem::Type::FOLDER) {
      itemHeight = LIST_ITEM_HEIGHT;
    }

    if (drawY + itemHeight > screenHeight) break;
    maxVisibleItems++;
    drawY += itemHeight;
  }

  drawY = startY + 2;
  int itemsDrawn = 0;

  for (int i = listScrollOffset; i < static_cast<int>(items.size()) && itemsDrawn < maxVisibleItems; i++) {
    const LibraryItem& item = items[i];

    bool isSelected = (tabSelectorIndex == 1 && selectorIndex == i && !isHeaderButtonSelected && !isSortButtonSelected);

    int itemHeight = 70;
    if (currentViewMode == ViewMode::FOLDER_VIEW && item.type == LibraryItem::Type::FOLDER) {
      itemHeight = LIST_ITEM_HEIGHT;
    }

    if (isSelected) {
      renderer.fillRect(0, drawY, screenWidth, itemHeight);
    }

    int iconX = 15;
    int iconY = drawY + (itemHeight / 2) - 12;

    if (item.type == LibraryItem::Type::FOLDER) {
      renderer.drawIcon(Folder, iconX, iconY, 24, 24, GfxRenderer::Rotate270CW, isSelected);
    } else {
      renderer.drawIcon(Book, iconX, iconY + 2, 24, 24, GfxRenderer::Rotate270CW, isSelected);

      if (isBookMarked(item.path)) {
        int starX = screenWidth - 45;
        renderer.drawIcon(Star, starX, iconY + 2, 24, 24, GfxRenderer::Rotate270CW, isSelected);
      }
    }

    int textX = iconX + FOLDER_ICON_WIDTH + FOLDER_ICON_SPACING;
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

      if (isBookOpened(item.path)) {
        renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID,
                          renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, secondLineText.c_str()) + iconX + 30,
                          drawY + 38, "(reading)", !isSelected, EpdFontFamily::ITALIC);
      }

    } else {
      int textY = drawY + (itemHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
      std::string displayText =
          renderer.truncatedText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, item.displayName.c_str(), textWidth - 5);
      renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, textX, textY, displayText.c_str(), !isSelected);
    }

    if (i < static_cast<int>(items.size()) - 1) {
      renderer.drawLine(0, drawY + itemHeight - 1, screenWidth, drawY + itemHeight - 1);
    }

    drawY += itemHeight;
    itemsDrawn++;
  }

  int totalItems = static_cast<int>(items.size());

  if (totalItems > maxVisibleItems) {
    int scrollBarHeight = visibleAreaHeight - 10;
    int scrollThumbY = startY + 5 + (listScrollOffset * scrollBarHeight) / totalItems;

    renderer.drawRect(screenWidth - 6, startY + 5, 4, scrollBarHeight);
    renderer.fillRect(screenWidth - 6, scrollThumbY, 4, 30);
  }
}
void LibraryActivity::loadLibraryFromIndex() {
  allBooksList.clear();
  libraryItems.clear();

  FsFile idxFile = SdMan.open("/.metadata/library/library.idx", O_READ);
  if (!idxFile) return;

  idxFile.seek(5);

  std::string cleanBase = basepath;
  if (cleanBase.length() > 1 && cleanBase.back() == '/') cleanBase.pop_back();

  while (idxFile.available()) {
    uint8_t marker;
    if (idxFile.read(&marker, 1) != 1) break;

    if (marker == 0x01) {
      LibraryItem item;
      item.type = LibraryItem::Type::BOOK;

      uint16_t pLen;
      idxFile.read(&pLen, sizeof(pLen));
      std::vector<char> pBuf(pLen + 1, 0);
      idxFile.read(pBuf.data(), pLen);
      item.path = std::string(pBuf.data(), pLen);

      uint8_t nLen;
      idxFile.read(&nLen, sizeof(nLen));
      idxFile.seek(idxFile.position() + nLen);

      uint8_t dLen;
      idxFile.read(&dLen, sizeof(dLen));
      std::vector<char> dBuf(dLen + 1, 0);
      idxFile.read(dBuf.data(), dLen);
      item.displayName = std::string(dBuf.data(), dLen);

      uint8_t fLen;
      idxFile.read(&fLen, sizeof(fLen));
      std::vector<char> fBuf(fLen + 1, 0);
      idxFile.read(fBuf.data(), fLen);
      item.folderPath = std::string(fBuf.data(), fLen);

      size_t pos;
      while ((pos = item.path.find("’")) != std::string::npos) {
        item.path.replace(pos, 3, "'");
      }

      if (currentViewMode == ViewMode::BOOK_LIST_VIEW) {
        if (item.path.find(cleanBase) == 0) {
          allBooksList.push_back(item);
        }
      } else {
        allBooksList.push_back(item);
      }

      if (currentViewMode == ViewMode::FOLDER_VIEW) {
        size_t lastSlash = item.path.find_last_of('/');
        std::string bookParent = (lastSlash == 0) ? "/" : item.path.substr(0, lastSlash);
        if (bookParent == cleanBase) {
          libraryItems.push_back(item);
        }
      }

    } else if (marker == 0xFF) {
      uint16_t pathLen;
      idxFile.read(&pathLen, sizeof(pathLen));
      std::vector<char> pathBuf(pathLen + 1, 0);
      idxFile.read(pathBuf.data(), pathLen);
      std::string dirPath = std::string(pathBuf.data(), pathLen);

      uint16_t entryCount;
      idxFile.read(&entryCount, sizeof(entryCount));

      if (currentViewMode == ViewMode::FOLDER_VIEW) {
        if (dirPath == "/" || dirPath == cleanBase || dirPath == cleanBase + "/") continue;

        std::string checkDir = dirPath;
        if (checkDir.length() > 1 && checkDir.back() == '/') checkDir.pop_back();

        size_t lastSlash = checkDir.find_last_of('/');
        std::string parentOfDir = (lastSlash == 0) ? "/" : checkDir.substr(0, lastSlash);

        if (parentOfDir == cleanBase) {
          LibraryItem folderItem;
          folderItem.type = LibraryItem::Type::FOLDER;
          folderItem.path = dirPath;
          folderItem.displayName = formatFolderName(extractFolderName(dirPath));
          if (!folderItem.displayName.empty()) {
            libraryItems.push_back(folderItem);
          }
        }
      }
    }
  }
  idxFile.close();
}

void LibraryActivity::loadLibraryItems() {
  libraryItems.clear();

  auto root = SdMan.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();
  std::vector<LibraryItem> folders;
  std::vector<LibraryItem> books;

  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));

    if (name[0] == '.' || strcmp(name, "System Volume Information") == 0 || strcmp(name, ".metadata") == 0 ||
        strcmp(name, "sleep") == 0) {
      file.close();
      continue;
    }

    std::string fullPath = basepath;
    if (fullPath.back() != '/') fullPath += "/";
    fullPath += name;

    if (file.isDirectory()) {
      if (directoryHasBooks(fullPath + "/")) {
        LibraryItem item;
        item.type = LibraryItem::Type::FOLDER;
        item.name = name;
        item.displayName = formatFolderName(name);
        item.path = fullPath + "/";
        folders.push_back(item);
      }
    } else {
      std::string filename = name;
    if (StringUtils::checkFileExtension(filename, ".epub") || StringUtils::checkFileExtension(filename, ".xtch") ||
        StringUtils::checkFileExtension(filename, ".xtc") || StringUtils::checkFileExtension(filename, ".txt") ||
        StringUtils::checkFileExtension(filename, ".md")) {
        LibraryItem item;
        item.type = LibraryItem::Type::BOOK;
        item.name = filename;
        item.displayName = formatFolderName(getBaseFilename(filename));
        item.path = fullPath;
        item.folderPath = formatFolderName(extractFolderName(basepath));
        books.push_back(item);
      }
    }
    file.close();
  }
  root.close();

  auto sortFn = [](const LibraryItem& a, const LibraryItem& b) { return a.displayName < b.displayName; };

  std::sort(folders.begin(), folders.end(), sortFn);
  std::sort(books.begin(), books.end(), sortFn);

  libraryItems.insert(libraryItems.end(), folders.begin(), folders.end());
  libraryItems.insert(libraryItems.end(), books.begin(), books.end());
}