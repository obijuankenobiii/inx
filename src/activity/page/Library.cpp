/**
 * @file Library.cpp
 * @brief Indexed grid library page for the inx-pro UI migration.
 */

#include "Library.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <utility>

#include "../settings/LibraryIndexer.h"
#include "BitmapRender.h"
#include "images/Filter.h"
#include "images/Hamburger.h"
#include "images/LibraryViewGrid.h"
#include "images/LibraryViewList.h"
#include "images/Refresh.h"
#include "images/SortAsc.h"
#include "state/BookState.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"
#include "state/SystemSetting.h"
#include "system/UiLayout.h"

extern void onGoToRecent();
extern void onGoToLibrary(const std::string& path);
extern void openReaderFromCallback(const std::string& path);
extern void onGoToSettings();
extern void onGoToFileTransfer();

namespace {

constexpr unsigned long kHeaderHoldMs = 650;
constexpr unsigned long kDoubleBackWindowMs = 450;
constexpr int kHeaderButtonCount = 4;
constexpr unsigned long kItemJumpHoldMs = 500;
constexpr unsigned long kItemJumpRepeatMs = 250;

unsigned long lastBackReleaseMs = 0;

std::string parentPath(const std::string& value);
std::string lower(std::string value);
bool endsWith(const std::string& value, const char* suffix);

}  // namespace

Library::Library(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Page("Library", renderer, mappedInput),
      path_(std::move(path)),
      grid_(renderer, items_),
      list_(renderer, items_, [this](const LibraryIndex::Book& item) {
        return favoritePaths_.find(item.path) != favoritePaths_.end();
      }) {
  tabSelectorIndex = 1;
  selectedHeaderButton_ = 3;
}

void Library::onEnter() {
  Page::onEnter();
  tabSelectorIndex = 1;
  if (path_.empty()) {
    path_ = "/";
  }
  while (path_.size() > 1 && path_.back() == '/') {
    path_.pop_back();
  }
  selectedHeaderButton_ = 3;
  headerFocused_ = false;
  backLongPressProcessed_ = false;
  sortOpen_ = false;
  sortIndex_ = 0;
  filterOpen_ = false;
  filterPage_ = 0;
  filterIndex_ = 0;
  filterTab_ = FilterTab::TITLE;
  typeFilterIndex_ = 0;
  typeFilter_.clear();
  hideFinished_ = false;
  letterFilter_ = 0;
  viewMode_ = SETTINGS.libraryMode == SystemSetting::LIBRARY_LIST ? ViewMode::LIST : ViewMode::GRID;
  sortMode_ = SETTINGS.librarySortEnabled && SETTINGS.librarySortMode <= static_cast<uint8_t>(SortMode::AUTHOR_ZA)
                  ? static_cast<SortMode>(SETTINGS.librarySortMode)
                  : SortMode::TITLE_AZ;
  sortIndex_ = static_cast<int>(sortMode_);
  sidebarOpen_ = false;
  selectedItemIndex_ = 0;
  nextItemJumpMs_ = 0;
  loadIndexedItems();
}

void Library::onExit() {
  // The indexing task uses this page for progress updates. Wait for it before the
  // activity is destroyed so a quick tab change cannot leave a dangling callback.
  while (isIndexing_) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  Page::onExit();
}

void Library::loop() {
  if (filterOpen_) {
    handleFilterInput();
    renderIfNeeded();
    return;
  }
  if (sortOpen_) {
    handleSortInput();
    renderIfNeeded();
    return;
  }

  if (indexReloadRequested_ && !isIndexing_) {
    indexReloadRequested_ = false;
    loadIndexedItems();
    requestRender();
  }

  if (!sidebarOpen_ && mappedInput.isPressed(MappedInputManager::Button::Back) && !backLongPressProcessed_ &&
      mappedInput.getHeldTime() >= kHeaderHoldMs) {
    headerFocused_ = true;
    selectedHeaderButton_ = 3;
    backLongPressProcessed_ = true;
    requestRender();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (backLongPressProcessed_) {
      backLongPressProcessed_ = false;
      lastBackReleaseMs = 0;
      return;
    }
    const unsigned long now = millis();
    const bool doubleBack = lastBackReleaseMs != 0 && now - lastBackReleaseMs <= kDoubleBackWindowMs;
    lastBackReleaseMs = doubleBack ? 0 : now;
    if (doubleBack) {
      sidebarOpen_ = true;
      headerFocused_ = false;
      requestRender();
      return;
    }
    if (headerFocused_) {
      headerFocused_ = false;
      requestRender();
      return;
    }
    if (path_ != "/") {
      onGoToLibrary(parentPath(path_));
      return;
    }
    sidebarOpen_ = !sidebarOpen_;
    headerFocused_ = false;
    requestRender();
    return;
  }

  // Consume the held Back button until release so Page::loop() cannot interpret
  // the initial press as a normal page-back action.
  if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
    return;
  }

  if (sidebarOpen_) {
    renderIfNeeded();
    return;
  }

  if (headerFocused_) {
    if (mappedInput.wasPressed(itemPrevButton())) {
      selectedHeaderButton_ = (selectedHeaderButton_ + kHeaderButtonCount - 1) % kHeaderButtonCount;
      requestRender();
      return;
    }
    if (mappedInput.wasPressed(itemNextButton())) {
      selectedHeaderButton_ = (selectedHeaderButton_ + 1) % kHeaderButtonCount;
      requestRender();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      handleHeaderConfirm();
      return;
    }
    Page::loop();
    return;
  }

  if (!isIndexing_ && indexLoaded_ && !items_.empty()) {
    const auto previousItemButton = itemPrevButton();
    const auto nextItemButton = itemNextButton();
    if (mappedInput.wasPressed(previousItemButton)) {
      moveSelectedItem(-1);
      nextItemJumpMs_ = millis() + kItemJumpHoldMs;
      requestRender();
      return;
    }
    if (mappedInput.wasPressed(nextItemButton)) {
      moveSelectedItem(1);
      nextItemJumpMs_ = millis() + kItemJumpHoldMs;
      requestRender();
      return;
    }
    if (mappedInput.isPressed(previousItemButton)) {
      if (mappedInput.getHeldTime() >= kItemJumpHoldMs && millis() >= nextItemJumpMs_) {
        moveSelectedItem(-5);
        nextItemJumpMs_ = millis() + kItemJumpRepeatMs;
        requestRender();
      }
      return;
    }
    if (mappedInput.isPressed(nextItemButton)) {
      if (mappedInput.getHeldTime() >= kItemJumpHoldMs && millis() >= nextItemJumpMs_) {
        moveSelectedItem(5);
        nextItemJumpMs_ = millis() + kItemJumpRepeatMs;
        requestRender();
      }
      return;
    }
    nextItemJumpMs_ = 0;
  } else {
    nextItemJumpMs_ = 0;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && indexLoaded_ &&
      selectedItemIndex_ >= 0 && selectedItemIndex_ < static_cast<int>(items_.size())) {
    const LibraryIndex::Book& selected = items_[static_cast<size_t>(selectedItemIndex_)];
    if (selected.type == LibraryIndex::Book::Type::FOLDER) {
      onGoToLibrary(selected.path);
    } else {
      BOOK_STATE.setReading(selected.path, true, selected.title);
      openReaderFromCallback(selected.path);
    }
    return;
  }

  Page::loop();
}

void Library::menu() {
  Page::menu();
  if (sortOpen_) {
    renderSortPicker();
  }
  if (filterOpen_) {
    renderFilterPicker();
  }
  if (sidebarOpen_) {
    navigation::Sidebar::render(renderer);
  }
}

void Library::title() const {
  // Match the Pro header: center the title within the hamburger icon's row
  // instead of using Home's top-aligned text baseline.
  renderer.bitmap.icon(Hamburger, navigation::Menu::leftMargin, navigation::Menu::topPadding,
                       navigation::Menu::iconSize, navigation::Menu::iconSize);
  const int font = MONTSERRAT_16_FONT_ID;
  const int textY = navigation::Menu::topPadding +
                    (navigation::Menu::iconSize - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, navigation::Menu::leftMargin + navigation::Menu::iconSize + 12, textY, name(), true,
                       EpdFontFamily::BOLD);
}

int Library::buttonX(const int index) const {
  const int right = renderer.getScreenWidth() - UiLayout::MENU_LEFT_MARGIN;
  const int refreshX = right - UiLayout::MENU_ICON_SIZE;
  const int filterX = refreshX - buttonGap - buttonSize;
  const int sortX = filterX - buttonGap - buttonSize;
  const int viewX = sortX - buttonGap - buttonSize;
  switch (index) {
    case 0:
      return viewX;
    case 1:
      return sortX;
    case 2:
      return filterX;
    default:
      return refreshX;
  }
}

int Library::buttonY() const { return UiLayout::MENU_TOP_PADDING; }

void Library::center() const {
  const int y = buttonY();
  const auto drawButton = [&](const uint8_t* icon, const int index,
                              const BitmapRender::Orientation orientation = BitmapRender::Orientation::None) {
    const int x = buttonX(index);
    const bool selected = headerFocused_ && selectedHeaderButton_ == index;
    if (selected) {
      constexpr int padding = UiLayout::LIBRARY_HEADER_BUTTON_PADDING;
      renderer.rectangle.fill(x - padding, y - padding, buttonSize + padding * 2, buttonSize + padding * 2, true, true,
                              true);
    }
    renderer.bitmap.icon(icon, x, y, buttonSize, buttonSize, orientation, selected);
  };
  drawButton(viewMode_ == ViewMode::GRID ? LibraryViewGrid : LibraryViewList, 0);
  const bool descending = sortMode_ == SortMode::TITLE_ZA || sortMode_ == SortMode::GROUP_ZA ||
                          sortMode_ == SortMode::AUTHOR_ZA;
  drawButton(SortAsc, 1, descending ? BitmapRender::Orientation::Rotate180 : BitmapRender::Orientation::None);
  drawButton(Filter, 2);
  drawButton(Refresh, 3);
}

int Library::sortCount() { return 6; }

const char* Library::sortLabel(const int index) {
  static constexpr const char* labels[] = {"Title", "Title", "Folder", "Folder", "Author", "Author"};
  return index >= 0 && index < sortCount() ? labels[index] : "Title";
}

const char* Library::sortDirection(const int index) {
  static constexpr const char* directions[] = {"A-Z", "Z-A", "A-Z", "Z-A", "A-Z", "Z-A"};
  return index >= 0 && index < sortCount() ? directions[index] : "A-Z";
}

const char* Library::typeFilterLabel(const int index) {
  switch (index) {
    case 1:
      return "EPUB";
    case 2:
      return "PDF";
    case 3:
      return "TXT";
    case 4:
      return "XTC";
    default:
      return "All";
  }
}

const char* Library::typeFilterCategory(const int index) {
  switch (index) {
    case 1:
      return "epub";
    case 2:
      return "pdf";
    case 3:
      return "txt";
    case 4:
      return "xtc";
    default:
      return "";
  }
}

bool Library::matchesTypeFilter(const std::string& path, const std::string& category) {
  if (category.empty()) return true;
  const std::string value = lower(path);
  if (category == "epub") return endsWith(value, ".epub");
  if (category == "pdf") return endsWith(value, ".pdf");
  if (category == "txt") return endsWith(value, ".txt") || endsWith(value, ".md");
  if (category == "xtc") return endsWith(value, ".xtc") || endsWith(value, ".xtch");
  return true;
}

void Library::renderSortPicker() const {
  constexpr int rowHeight = UiLayout::LIST_ITEM_HEIGHT;
  constexpr int width = 250;
  constexpr int leftPadding = 20;
  constexpr int rightPadding = 40;
  const int height = sortCount() * rowHeight + 1;
  const int x = std::max(0, buttonX(1) + buttonSize - width);
  const int y = UiLayout::MENU_HEIGHT;

  renderer.rectangle.fill(x, y, width, height, false);
  for (int index = 0; index < sortCount(); ++index) {
    const int rowY = y + index * rowHeight;
    const bool selected = index == sortIndex_;
    if (selected) renderer.rectangle.fill(x, rowY, width, rowHeight, true);
    const int font = MONTSERRAT_12_FONT_ID;
    const int textY = rowY + (rowHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, x + leftPadding, textY, sortLabel(index), !selected, EpdFontFamily::REGULAR);
    const int directionFont = MONTSERRAT_8_FONT_ID;
    const int directionWidth = renderer.text.getWidth(directionFont, sortDirection(index));
    const int directionY = rowY + (rowHeight - renderer.text.getLineHeight(directionFont)) / 2;
    renderer.text.render(directionFont, x + width - rightPadding - directionWidth, directionY, sortDirection(index),
                         !selected, EpdFontFamily::REGULAR);
    if (index + 1 < sortCount()) {
      renderer.line.render(x + 10, rowY + rowHeight, x + width - 10, rowY + rowHeight, !selected,
                           LineRender::Style::Dotted);
    }
  }
  renderer.rectangle.render(x, y, width, height, true);
}

namespace {

std::string cleanPath(std::string value) {
  while (value.size() > 1 && value.back() == '/') {
    value.pop_back();
  }
  return value.empty() ? "/" : value;
}

std::string parentPath(const std::string& value) {
  const size_t slash = value.find_last_of('/');
  if (slash == std::string::npos || slash == 0) {
    return "/";
  }
  return value.substr(0, slash);
}

bool isImmediateChild(const std::string& value, const std::string& base) {
  const std::string cleanBase = cleanPath(base);
  if (value == cleanBase) {
    return false;
  }
  const std::string prefix = cleanBase == "/" ? "/" : cleanBase + "/";
  if (value.compare(0, prefix.size(), prefix) != 0) {
    return false;
  }
  return value.find('/', prefix.size()) == std::string::npos;
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool endsWith(const std::string& value, const char* suffix) {
  const size_t length = std::char_traits<char>::length(suffix);
  return value.size() >= length && value.compare(value.size() - length, length, suffix) == 0;
}

}  // namespace

void Library::loadIndexedItems() {
  items_.clear();
  favoritePaths_.clear();
  for (const BookState::Book& favorite : BOOK_STATE.getFavoriteBooks()) {
    favoritePaths_.insert(favorite.path);
  }
  indexLoaded_ = false;

  std::vector<LibraryIndex::Book> indexedItems;
  if (!LibraryIndex::search("", indexedItems, LibraryIndex::all)) {
    return;
  }

  path_ = cleanPath(path_);
  std::unordered_set<std::string> finishedPaths;
  if (hideFinished_) {
    for (const BookState::Book& finished : BOOK_STATE.getFinishedBooks()) {
      finishedPaths.insert(finished.path);
    }
  }
  for (const LibraryIndex::Book& item : indexedItems) {
    const bool inCurrentFolder = (item.type == LibraryIndex::Book::Type::FOLDER && isImmediateChild(item.path, path_)) ||
                                 (item.type == LibraryIndex::Book::Type::BOOK && parentPath(item.path) == path_);
    if (!inCurrentFolder) {
      continue;
    }
    if (letterFilter_ != 0 && leadingLetter(item.title.empty() ? item.path : item.title) != letterFilter_) {
      continue;
    }
    if (item.type == LibraryIndex::Book::Type::BOOK && !matchesTypeFilter(item.path, typeFilter_)) {
      continue;
    }
    if (item.type == LibraryIndex::Book::Type::BOOK && finishedPaths.find(item.path) != finishedPaths.end()) {
      continue;
    }
    items_.push_back(item);
  }
  const bool ascending = sortMode_ == SortMode::TITLE_AZ || sortMode_ == SortMode::GROUP_AZ ||
                         sortMode_ == SortMode::AUTHOR_AZ;
  const bool groupSort = sortMode_ == SortMode::GROUP_AZ || sortMode_ == SortMode::GROUP_ZA;
  const bool authorSort = sortMode_ == SortMode::AUTHOR_AZ || sortMode_ == SortMode::AUTHOR_ZA;
  std::stable_sort(items_.begin(), items_.end(), [ascending, groupSort, authorSort](const LibraryIndex::Book& left,
                                                                                     const LibraryIndex::Book& right) {
    if (authorSort) {
      const std::string leftAuthor = lower(left.author);
      const std::string rightAuthor = lower(right.author);
      if (leftAuthor != rightAuthor) {
        return ascending ? leftAuthor < rightAuthor : leftAuthor > rightAuthor;
      }
    }
    const std::string leftKey = lower(groupSort ? left.folder : left.title);
    const std::string rightKey = lower(groupSort ? right.folder : right.title);
    if (leftKey == rightKey) {
      return lower(left.title) < lower(right.title);
    }
    return ascending ? leftKey < rightKey : leftKey > rightKey;
  });
  const int visibleCount = static_cast<int>(items_.size());
  selectedItemIndex_ = visibleCount > 0 ? std::min(selectedItemIndex_, visibleCount - 1) : 0;
  indexLoaded_ = true;
}

void Library::content() {
  if (isIndexing_) {
    renderer.text.centered(MONTSERRAT_12_FONT_ID, renderer.getScreenHeight() / 2 - 14,
                           "Refreshing library", true, EpdFontFamily::BOLD);
    if (indexingTotal_ > 0) {
      ScreenComponents::drawProgressBar(renderer, renderer.getScreenWidth() / 2 - 120,
                                         renderer.getScreenHeight() / 2 + 12, 240, 6, indexingProgress_,
                                         indexingTotal_);
    }
    return;
  }
  if (!indexLoaded_) {
    renderer.text.centered(MONTSERRAT_12_FONT_ID, renderer.getScreenHeight() / 2,
                           LibraryIndex::hasIndex() ? "Unable to read library index" : "Build the library index first");
    return;
  }
  if (items_.empty()) {
    renderer.text.centered(MONTSERRAT_12_FONT_ID, renderer.getScreenHeight() / 2, "No books in this folder");
    return;
  }
  const int selectedIndex = headerFocused_ ? -1 : selectedItemIndex_;
  if (viewMode_ == ViewMode::LIST) {
    list_.render(selectedIndex);
  } else {
    grid_.render(selectedIndex, selectedItemIndex_ / views::library::Grid::itemsPerPage());
  }
}

bool Library::moveSelectedItem(const int delta) {
  const int count = static_cast<int>(items_.size());
  if (count <= 0) {
    selectedItemIndex_ = 0;
    return false;
  }
  const int oldIndex = selectedItemIndex_;
  selectedItemIndex_ = (selectedItemIndex_ + delta) % count;
  if (selectedItemIndex_ < 0) {
    selectedItemIndex_ += count;
  }
  return selectedItemIndex_ != oldIndex;
}

void Library::handleHeaderConfirm() {
  switch (selectedHeaderButton_) {
    case 0:
      toggleViewMode();
      break;
    case 1:
      openSortPicker();
      break;
    case 2:
      openFilterPicker();
      break;
    case 3:
      startIndexing();
      break;
    default:
      // Grid view is the only view currently rendered; its toggle is reserved
      // for the next migration step.
      requestRender();
      break;
  }
}

void Library::toggleViewMode() {
  viewMode_ = viewMode_ == ViewMode::GRID ? ViewMode::LIST : ViewMode::GRID;
  SETTINGS.libraryMode = viewMode_ == ViewMode::LIST ? SystemSetting::LIBRARY_LIST : SystemSetting::LIBRARY_GRID;
  SETTINGS.saveToFile();
  requestRender();
}

void Library::openSortPicker() {
  sortOpen_ = true;
  sortIndex_ = static_cast<int>(sortMode_);
  requestRender();
}

void Library::handleSortInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    sortOpen_ = false;
    requestRender();
    return;
  }
  if (mappedInput.wasPressed(itemPrevButton())) {
    sortIndex_ = (sortIndex_ + sortCount() - 1) % sortCount();
    requestRender();
    return;
  }
  if (mappedInput.wasPressed(itemNextButton())) {
    sortIndex_ = (sortIndex_ + 1) % sortCount();
    requestRender();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    applySortSelection();
  }
}

void Library::applySortSelection() {
  if (!SETTINGS.librarySortEnabled || sortIndex_ < 0 || sortIndex_ >= sortCount()) {
    sortOpen_ = false;
    requestRender();
    return;
  }
  sortMode_ = static_cast<SortMode>(sortIndex_);
  SETTINGS.librarySortMode = static_cast<uint8_t>(sortMode_);
  SETTINGS.saveToFile();
  sortOpen_ = false;
  selectedItemIndex_ = 0;
  loadIndexedItems();
  requestRender();
}

void Library::openFilterPicker() {
  filterOpen_ = true;
  if (letterFilter_ >= 'A' && letterFilter_ <= 'Z') {
    const int offset = letterFilter_ - 'A';
    filterPage_ = std::min(2, offset / 9);
    filterIndex_ = offset % 9;
  } else {
    filterPage_ = 0;
    filterIndex_ = 9;
  }
  requestRender();
}

void Library::handleFilterInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    filterOpen_ = false;
    requestRender();
    return;
  }

  if (mappedInput.wasPressed(tabPrevButton())) {
    filterTab_ = static_cast<FilterTab>((static_cast<int>(filterTab_) + 2) % 3);
    requestRender();
    return;
  }
  if (mappedInput.wasPressed(tabNextButton())) {
    filterTab_ = static_cast<FilterTab>((static_cast<int>(filterTab_) + 1) % 3);
    requestRender();
    return;
  }
  if (mappedInput.wasPressed(itemPrevButton())) {
    if (filterTab_ == FilterTab::TITLE) {
      moveFilterSelection(-1);
    } else if (filterTab_ == FilterTab::TYPE) {
      typeFilterIndex_ = (typeFilterIndex_ + 5 - 1) % 5;
    }
    requestRender();
    return;
  }
  if (mappedInput.wasPressed(itemNextButton())) {
    if (filterTab_ == FilterTab::TITLE) {
      moveFilterSelection(1);
    } else if (filterTab_ == FilterTab::TYPE) {
      typeFilterIndex_ = (typeFilterIndex_ + 1) % 5;
    }
    requestRender();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (filterTab_ == FilterTab::TITLE) {
      applyFilterSelection();
    } else if (filterTab_ == FilterTab::TYPE) {
      applyTypeFilterSelection();
    } else {
      toggleFinishedFilter();
    }
  }
}

void Library::applyFilterSelection() {
  const char selected = filterLetter(filterPage_, filterIndex_);
  letterFilter_ = selected == '*' ? 0 : selected;
  filterOpen_ = false;
  selectedItemIndex_ = 0;
  loadIndexedItems();
  requestRender();
}

void Library::applyTypeFilterSelection() {
  typeFilter_ = typeFilterCategory(typeFilterIndex_);
  filterOpen_ = false;
  selectedItemIndex_ = 0;
  loadIndexedItems();
  requestRender();
}

void Library::toggleFinishedFilter() {
  // Keep this page-local until the Pro hide-finished setting is migrated into
  // the inx settings schema.
  hideFinished_ = !hideFinished_;
  selectedItemIndex_ = 0;
  loadIndexedItems();
  requestRender();
}

void Library::moveFilterSelection(const int delta) {
  if (delta == 0) {
    return;
  }
  const int validCount = std::max(1, std::min(9, 26 - filterPage_ * 9));
  if (delta > 0) {
    if (filterIndex_ < validCount - 1) {
      ++filterIndex_;
    } else if (filterIndex_ != 9) {
      filterIndex_ = 9;
    } else {
      filterPage_ = (filterPage_ + 1) % 3;
      filterIndex_ = 0;
    }
  } else if (filterIndex_ > 0 && filterIndex_ != 9) {
    --filterIndex_;
  } else if (filterIndex_ == 9) {
    filterIndex_ = validCount - 1;
  } else {
    filterPage_ = (filterPage_ + 2) % 3;
    filterIndex_ = 9;
  }
}

char Library::filterLetter(const int page, const int index) {
  if (index == 9) {
    return '*';
  }
  const int value = page * 9 + index;
  return value >= 0 && value < 26 ? static_cast<char>('A' + value) : 0;
}

char Library::leadingLetter(const std::string& value) {
  for (const char character : value) {
    const unsigned char current = static_cast<unsigned char>(character);
    if (std::isalpha(current)) {
      return static_cast<char>(std::toupper(current));
    }
  }
  return 0;
}

void Library::renderFilterPicker() const {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int panelW = std::min(screenW - 48, 330);
  const int panelH = std::min(370, std::max(260, screenH - UiLayout::MENU_HEIGHT - UiLayout::MENU_BOTTOM_HEIGHT - 20));
  const int panelX = (screenW - panelW) / 2;
  const int panelY = std::max(UiLayout::MENU_HEIGHT + 20, (screenH - panelH) / 2);
  constexpr int pad = 18;
  constexpr int titleH = 28;
  constexpr int tabGap = 20;
  constexpr int gap = 10;
  constexpr int allGap = 10;
  const int gridY = panelY + pad + titleH + tabGap;
  const int cellW = (panelW - pad * 2 - gap * 2) / 3;
  const int cellH = std::max(28, (panelH - pad * 2 - titleH - tabGap - gap * 2 - allGap - 20) / 4);
  const int selectorSize = std::min(cellW, cellH);
  constexpr int font = MONTSERRAT_12_FONT_ID;

  renderer.rectangle.fill(panelX, panelY, panelW, panelH, false);
  renderer.rectangle.render(panelX, panelY, panelW, panelH, true);

  static constexpr const char* tabs[] = {"Title", "Type", "Options"};
  const int tabAreaX = panelX + pad;
  const int tabWidth = (panelW - pad * 2) / 3;
  const int tabY = panelY + pad + 3;
  for (int tab = 0; tab < 3; ++tab) {
    const bool selected = static_cast<int>(filterTab_) == tab;
    const int textWidth = renderer.text.getWidth(font, tabs[tab], selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.text.render(font, tabAreaX + tab * tabWidth + (tabWidth - textWidth) / 2, tabY, tabs[tab], true,
                         selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  if (filterTab_ == FilterTab::TITLE) {
    for (int index = 0; index < 9; ++index) {
      const char letter = filterLetter(filterPage_, index);
      if (letter == 0) continue;
      const int col = index % 3;
      const int row = index / 3;
      const int x = panelX + pad + col * (cellW + gap);
      const int y = gridY + row * (cellH + gap);
      const int selectorX = x + (cellW - selectorSize) / 2;
      const int selectorY = y + (cellH - selectorSize) / 2;
      const bool selected = index == filterIndex_;
      if (selected) renderer.rectangle.fill(selectorX, selectorY, selectorSize, selectorSize, true, true, true);
      char label[2] = {letter, '\0'};
      const int textWidth = renderer.text.getWidth(font, label, EpdFontFamily::BOLD);
      const int textY = y + (cellH - renderer.text.getLineHeight(font)) / 2;
      renderer.text.render(font, x + (cellW - textWidth) / 2, textY, label, !selected, EpdFontFamily::BOLD);
    }

    const int allY = gridY + 3 * (cellH + gap) + allGap;
    const int allX = panelX + (panelW - selectorSize) / 2;
    const bool allSelected = filterIndex_ == 9;
    if (allSelected) renderer.rectangle.fill(allX, allY, selectorSize, selectorSize, true, true, true);
    const int textWidth = renderer.text.getWidth(font, "All", EpdFontFamily::BOLD);
    const int textY = allY + (selectorSize - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, panelX + (panelW - textWidth) / 2, textY, "All", !allSelected, EpdFontFamily::BOLD);
  } else if (filterTab_ == FilterTab::TYPE) {
    for (int index = 0; index < 5; ++index) {
      const int col = index % 3;
      const int row = index / 3;
      const int x = panelX + pad + col * (cellW + gap);
      const int y = gridY + row * (cellH + gap);
      const int selectorX = x + (cellW - selectorSize) / 2;
      const int selectorY = y + (cellH - selectorSize) / 2;
      const bool selected = index == typeFilterIndex_;
      if (selected) renderer.rectangle.fill(selectorX, selectorY, selectorSize, selectorSize, true, true, true);
      const char* label = typeFilterLabel(index);
      const int textWidth = renderer.text.getWidth(font, label, EpdFontFamily::BOLD);
      const int textY = y + (cellH - renderer.text.getLineHeight(font)) / 2;
      renderer.text.render(font, x + (cellW - textWidth) / 2, textY, label, !selected, EpdFontFamily::BOLD);
    }
  } else {
    const int rowY = gridY;
    const int textY = rowY + (cellH - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, panelX + pad, textY, "Hide finished books", true, EpdFontFamily::REGULAR);
    const int toggleW = 52;
    const int toggleH = 30;
    const int toggleX = panelX + panelW - pad - toggleW;
    const int toggleY = rowY + (cellH - toggleH) / 2;
    const bool enabled = hideFinished_;
    const int tone = static_cast<int>(enabled ? GfxRenderer::FillTone::Ink : GfxRenderer::FillTone::Gray);
    renderer.rectangle.fill(toggleX, toggleY, toggleW, toggleH, tone, true);
    const int knobX = enabled ? toggleX + toggleW - toggleH / 2 - 1 : toggleX + toggleH / 2;
    renderer.rectangle.fill(knobX - 7, toggleY + toggleH / 2 - 7, 14, 14,
                            static_cast<int>(GfxRenderer::FillTone::Paper), true);
  }
}

void Library::startIndexing() {
  if (isIndexing_) {
    return;
  }

  isIndexing_ = true;
  indexReloadRequested_ = false;
  indexingProgress_ = 0;
  indexingTotal_ = 0;
  requestRender();

  constexpr uint32_t taskStackSize = 6144;
  const BaseType_t created = xTaskCreate(
      [](void* parameter) {
        auto* page = static_cast<Library*>(parameter);
        LibraryIndexer::indexAll([page](const int current, const int total, const char*) {
          page->indexingProgress_ = current;
          page->indexingTotal_ = total;
          if (current % 10 == 0) {
            page->requestRender();
            vTaskDelay(pdMS_TO_TICKS(1));
          }
        });

        SETTINGS.useLibraryIndex = 1;
        SETTINGS.saveToFile();
        page->isIndexing_ = false;
        page->indexReloadRequested_ = true;
        page->requestRender();
        vTaskDelete(nullptr);
      },
      "LibIdxBtnTask", taskStackSize, this, 1, nullptr);

  if (created != pdPASS) {
    isIndexing_ = false;
    requestRender();
  }
}

void Library::navigateToSelectedMenu() {
  switch (tabSelectorIndex) {
    case 0:
      onGoToRecent();
      break;
    case 2:
      onGoToSettings();
      break;
    case 3:
      onGoToFileTransfer();
      break;
    default:
      // The fifth slot is the visual search control; search will be wired in later.
      break;
  }
}
