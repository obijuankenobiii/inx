#include "MenuDrawer.h"

#include <algorithm>
#include <cstdio>

#include "Epub.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"

#define SETTINGS SystemSetting::getInstance()

constexpr int LIST_ITEM_HEIGHT = 60;
constexpr float TOC_DRAWER_HEIGHT_PERCENT = 0.8f;  // TOC takes 80% of screen height (portrait)

namespace {

bool isLandscapeReader(const GfxRenderer& gfx) {
  const auto o = gfx.getOrientation();
  return o == GfxRenderer::LandscapeClockwise || o == GfxRenderer::LandscapeCounterClockwise;
}

/** In landscape side drawer, list moves with Right/Left (matches on-screen list vs device); still accept Up/Down. */
bool readDrawerListPrev(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasReleased(MappedInputManager::Button::Right) || in.wasReleased(MappedInputManager::Button::Up);
  }
  if (SETTINGS.readerDirectionMapping == SystemSetting::READER_DIRECTION_MAPPING::MAP_NONE) {
    return in.wasReleased(MappedInputManager::Button::Up) || in.wasReleased(MappedInputManager::Button::Left);
  }
  switch (SETTINGS.readerDirectionMapping) {
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_RIGHT_LEFT:
      return in.wasReleased(MappedInputManager::Button::Right);
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_UP_DOWN:
      return in.wasReleased(MappedInputManager::Button::Up);
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_DOWN_UP:
      return in.wasReleased(MappedInputManager::Button::Down);
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_LEFT_RIGHT:
    default:
      return in.wasReleased(MappedInputManager::Button::Left);
  }
}

bool readDrawerListNext(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasReleased(MappedInputManager::Button::Left) ||
           in.wasReleased(MappedInputManager::Button::Down);
  }
  if (SETTINGS.readerDirectionMapping == SystemSetting::READER_DIRECTION_MAPPING::MAP_NONE) {
    return in.wasReleased(MappedInputManager::Button::Down) || in.wasReleased(MappedInputManager::Button::Right);
  }
  switch (SETTINGS.readerDirectionMapping) {
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_RIGHT_LEFT:
      return in.wasReleased(MappedInputManager::Button::Left);
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_UP_DOWN:
      return in.wasReleased(MappedInputManager::Button::Down);
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_DOWN_UP:
      return in.wasReleased(MappedInputManager::Button::Up);
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_LEFT_RIGHT:
    default:
      return in.wasReleased(MappedInputManager::Button::Right);
  }
}

/** Bookmarks list: portrait keeps Up/Down only so Right stays reserved for delete. */
bool readBookmarkLinePrev(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return readDrawerListPrev(in, r);
  }
  return in.wasReleased(MappedInputManager::Button::Up);
}

bool readBookmarkLineNext(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasReleased(MappedInputManager::Button::Left);
  }
  return in.wasReleased(MappedInputManager::Button::Down);
}

}  // namespace

/**
 * @brief Constructs a new MenuDrawer
 * @param renderer Reference to the graphics renderer
 * @param onAction Callback when a menu action is selected
 * @param onDismiss Callback when the drawer is dismissed
 */
MenuDrawer::MenuDrawer(GfxRenderer& renderer, ActionCallback onAction, DismissCallback onDismiss)
    : renderer(renderer),
      onAction(onAction),
      onDismiss(onDismiss),
      selectedIndex(0),
      scrollOffset(0),
      visible(false),
      dismissed(false),
      lastInputTime(0) {
  itemHeight = LIST_ITEM_HEIGHT;
  syncLayoutFromRenderer();

  menuItems = {{"Table of Contents", MenuAction::SELECT_CHAPTER},
               {"Show Bookmarks", MenuAction::SHOW_BOOKMARKS},
               {"KOReader Sync", MenuAction::KOREADER_SYNC},
               {"Delete Cache", MenuAction::DELETE_CACHE},
               {"Delete Progress", MenuAction::DELETE_PROGRESS},
               {"Delete Book", MenuAction::DELETE_BOOK},
               {"Generate Full Data", MenuAction::GENERATE_FULL_DATA},
               {"Go Home", MenuAction::GO_HOME}};
}

/**
 * @brief Destructor
 */
bool MenuDrawer::isLandscapeDrawer(const GfxRenderer& gfx) { return isLandscapeReader(gfx); }

void MenuDrawer::syncLayoutFromRenderer() {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  if (isLandscapeReader(renderer)) {
    drawerWidth = sw / 2;
    drawerX = sw - drawerWidth;
    drawerY = 0;
    drawerHeight = sh;
    tocDrawerX = drawerX;
    tocDrawerWidth = drawerWidth;
    tocDrawerY = 0;
    tocDrawerHeight = sh;
  } else {
    drawerX = 0;
    drawerWidth = sw;
    drawerHeight = sh * 80 / 100;
    drawerY = sh - drawerHeight;
    tocDrawerX = 0;
    tocDrawerWidth = sw;
    tocDrawerHeight = static_cast<int>(sh * TOC_DRAWER_HEIGHT_PERCENT);
    tocDrawerY = sh - tocDrawerHeight;
  }
  itemsPerPage = std::max(1, (drawerHeight - 100) / itemHeight);
}

void MenuDrawer::relayoutForRendererChange() { syncLayoutFromRenderer(); }

void MenuDrawer::drawDrawerHintRow(const char* btn1, const char* btn2, const char* btn3, const char* btn4) {
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, btn1, btn2, btn3, btn4);
}

MenuDrawer::~MenuDrawer() {
  onAction = nullptr;
  onDismiss = nullptr;
  tocSelectionCallback = nullptr;
  bookmarkListProvider = nullptr;
  bookmarkSelectCallback = nullptr;
  bookmarkDeleteCallback = nullptr;
  mappedInputForHints = nullptr;
  epub = nullptr;
}

/**
 * @brief Shows the menu drawer
 */
void MenuDrawer::show() {
  if (visible) return;
  syncLayoutFromRenderer();
  visible = true;
  dismissed = false;
  showingToc = false;
  showingBookmarks = false;
  selectedIndex = 0;
  scrollOffset = 0;
  tocSelectedIndex = 0;
  tocScrollOffset = 0;
  bookmarkSelectedIndex = 0;
  bookmarkEntries.clear();
  renderWithRefresh();
}

/**
 * @brief Hides the menu drawer
 */
void MenuDrawer::hide() {
  visible = false;
  dismissed = true;
  showingToc = false;
  showingBookmarks = false;
}

/**
 * @brief Renders the menu drawer
 */
void MenuDrawer::render() {
  if (!visible) return;
  renderWithRefresh();
}

/**
 * @brief Renders the menu drawer with specified refresh mode
 * @param mode Display refresh mode
 */
void MenuDrawer::renderWithRefresh() {
  if (!visible) return;
  syncLayoutFromRenderer();

  if (showingBookmarks) {
    renderBookmarks();
  } else if (showingToc) {
    renderToc();
  } else {
    drawBackground();
    drawMenuItems();
    drawScrollIndicator();
    drawDrawerHintRow("« Back", "Select", "", "");
  }
  renderer.displayBuffer();
}

/**
 * @brief Draws the background panel of the menu drawer
 */
void MenuDrawer::drawBackground() {
  renderer.fillRect(drawerX, drawerY, drawerWidth, drawerHeight, false);
  renderer.drawRect(drawerX, drawerY, drawerWidth, drawerHeight, true);

  int currentY = drawerY + 10;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, drawerX + 20, currentY, "Reader Menu", true, EpdFontFamily::BOLD);

  std::string displayTitle = bookTitle;
  if (displayTitle.length() > 30) {
    displayTitle.replace(27, std::string::npos, "...");
  }

  currentY += 25;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, drawerX + 20, currentY, displayTitle.c_str(), true);

  int dividerY = currentY + 30;
  renderer.drawLine(drawerX, dividerY, drawerX + drawerWidth, dividerY, true);
}

/**
 * @brief Draws all menu items in the current scroll view
 */
void MenuDrawer::drawMenuItems() {
  int startY = drawerY + 65;

  for (int i = 0; i < itemsPerPage && (i + scrollOffset) < static_cast<int>(menuItems.size()); i++) {
    int index = i + scrollOffset;
    int itemY = startY + (i * itemHeight);
    const auto& item = menuItems[index];
    bool isSelected = (index == selectedIndex);

    if (isSelected) {
      renderer.fillRect(drawerX, itemY, drawerWidth, itemHeight, GfxRenderer::FillTone::Ink);
    }

    int textX = drawerX + 23;
    int textY = itemY + (itemHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, item.label.c_str(), isSelected ? 0 : 1);

    // Draw arrow indicator
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, drawerX + drawerWidth - 30, textY, "›", isSelected ? 0 : 1);

    renderer.drawLine(drawerX, itemY + itemHeight - 1, drawerX + drawerWidth, itemY + itemHeight - 1, true);
  }
}

/**
 * @brief Draws a scroll indicator when content exceeds visible area
 */
void MenuDrawer::drawScrollIndicator() {
  int totalItems = (int)menuItems.size();
  if (totalItems <= itemsPerPage) return;

  int startY = drawerY + 80;
  int listHeight = itemsPerPage * itemHeight;
  int thumbH = (itemsPerPage * listHeight) / totalItems;
  int thumbY = startY + (scrollOffset * listHeight) / totalItems;

  renderer.fillRect(drawerX + drawerWidth - 4, thumbY, 2, thumbH, true);
}

/**
 * @brief Gets the number of TOC items that fit on screen
 * @return Number of items per page
 */
int MenuDrawer::getTocPageItems() const {
  constexpr int headerReserved = 120;  // Header space in TOC drawer
  int items = (tocDrawerHeight - headerReserved) / LIST_ITEM_HEIGHT;
  return (items < 1) ? 1 : items;
}

/**
 * @brief Draws the TOC background with drawer effect
 */
void MenuDrawer::drawTocBackground() {
  renderer.fillRect(tocDrawerX, tocDrawerY, tocDrawerWidth, tocDrawerHeight, false);
  renderer.drawRect(tocDrawerX, tocDrawerY, tocDrawerWidth, tocDrawerHeight, true);
}

/**
 * @brief Renders the Table of Contents view as a drawer
 */
void MenuDrawer::renderToc() {
  if (!epub) return;

  const int panelW = tocDrawerWidth;
  const int totalItems = epub->getTocItemsCount();
  const int pageItems = getTocPageItems();

  // Draw TOC background
  drawTocBackground();

  // Header
  const int headerY = tocDrawerY + 10;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, tocDrawerX + 20, headerY, "Table of Contents", true,
                    EpdFontFamily::BOLD);

  const char* subtitleText = "Select a chapter to begin reading";
  int subtitleY = headerY + 30;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(tocDrawerX, dividerY, tocDrawerX + panelW, dividerY, true);

  const int pageStartIndex = (tocSelectedIndex / pageItems) * pageItems;
  int drawY = dividerY + 2;

  if (totalItems == 0) {
    const int msgY = drawY + 24;
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + 20, msgY,
                      "No table of contents in this book.", true);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + 20, msgY + 22,
                      "Try Delete Cache and reopen to rebuild.", true);
  } else {
    for (int i = 0; i < pageItems; i++) {
      int itemIndex = pageStartIndex + i;
      if (itemIndex >= totalItems) break;

      int itemY = drawY + (i * LIST_ITEM_HEIGHT);
      bool isSelected = (itemIndex == tocSelectedIndex);

      if (isSelected) {
        renderer.fillRect(tocDrawerX, itemY, panelW, LIST_ITEM_HEIGHT, GfxRenderer::FillTone::Ink);
      }

      int textY = itemY + (LIST_ITEM_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

      auto item = epub->getTocItem(itemIndex);
      const int level = std::max(1, static_cast<int>(item.level));
      const int depthPx = (level - 1) * 20;
      const int maxDepthPx = std::max(0, panelW - 120);
      const int relIndent = 20 + std::min(depthPx, maxDepthPx);
      const int indentSize = tocDrawerX + relIndent;
      const int maxTitleW = std::max(40, panelW - 70 - relIndent);
      const std::string truncatedName =
          renderer.truncatedText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, item.title.c_str(), maxTitleW);

      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, indentSize, textY, truncatedName.c_str(), isSelected ? 0 : 1);
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + panelW - 30, textY, "›", isSelected ? 0 : 1);
      renderer.drawLine(tocDrawerX, itemY + LIST_ITEM_HEIGHT - 1, tocDrawerX + panelW, itemY + LIST_ITEM_HEIGHT - 1,
                        true);
    }
  }

  // Footer with page indicator
  const int totalPages = std::max(1, (totalItems + pageItems - 1) / pageItems);
  const int currentPageNum = (tocSelectedIndex / pageItems) + 1;
  char pageStr[24];
  snprintf(pageStr, sizeof(pageStr), "Page %d of %d", currentPageNum, totalPages);
  int pageStrWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageStr);
  int footerY = tocDrawerY + tocDrawerHeight - 35;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + (panelW - pageStrWidth) / 2, footerY, pageStr, true);

  // Button hints for TOC view
  drawDrawerHintRow("« Back", "Select", "«", "»");
}


/**
 * @brief Renders bookmarks in the same drawer layout as the TOC
 */
void MenuDrawer::renderBookmarks() {
  const int panelW = tocDrawerWidth;
  const int totalItems = static_cast<int>(bookmarkEntries.size());
  const int pageItems = getTocPageItems();

  drawTocBackground();

  const int headerY = tocDrawerY + 10;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, tocDrawerX + 20, headerY, "Bookmarks", true, EpdFontFamily::BOLD);

  const char* subtitleText = "Select a bookmark to open it";
  int subtitleY = headerY + 30;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(tocDrawerX, dividerY, tocDrawerX + panelW, dividerY, true);

  if (totalItems == 0) {
    const int msgY = dividerY + 40;
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + 10, msgY, "No bookmarks yet", true);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + 10, msgY + 22, "Long-press Confirm", true);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + 10, msgY + 44, "while reading to add", true);
    drawDrawerHintRow("« Back", "", "", "");
    return;
  }

  const int pageStartIndex = (bookmarkSelectedIndex / pageItems) * pageItems;
  int drawY = dividerY + 2;

  for (int i = 0; i < pageItems; i++) {
    const int itemIndex = pageStartIndex + i;
    if (itemIndex >= totalItems) {
      break;
    }

    const int itemY = drawY + (i * LIST_ITEM_HEIGHT);
    const bool isSelected = (itemIndex == bookmarkSelectedIndex);
    const auto& row = bookmarkEntries[static_cast<size_t>(itemIndex)];

    if (isSelected) {
      renderer.fillRect(tocDrawerX, itemY, panelW, LIST_ITEM_HEIGHT, GfxRenderer::FillTone::Ink);
    }

    const int textY = itemY + (LIST_ITEM_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
    const int kIndent = tocDrawerX + 20;
    const std::string truncated =
        renderer.truncatedText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, row.label.c_str(), panelW - 60 - 20);

    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, kIndent, textY, truncated.c_str(), isSelected ? 0 : 1);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + panelW - 30, textY, "›", isSelected ? 0 : 1);
    renderer.drawLine(tocDrawerX, itemY + LIST_ITEM_HEIGHT - 1, tocDrawerX + panelW, itemY + LIST_ITEM_HEIGHT - 1, true);
  }

  const int totalPages = (totalItems + pageItems - 1) / pageItems;
  const int currentPageNum = (bookmarkSelectedIndex / pageItems) + 1;
  char pageStr[24];
  snprintf(pageStr, sizeof(pageStr), "Page %d of %d", currentPageNum, totalPages);
  const int pageStrWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageStr);
  const int footerY = tocDrawerY + tocDrawerHeight - 35;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + (panelW - pageStrWidth) / 2, footerY, pageStr, true);

  if (mappedInputForHints != nullptr) {
    const auto labels = mappedInputForHints->mapLabels("« Back", "Select", "Up", "Del");
    drawDrawerHintRow(labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    drawDrawerHintRow("« Back", "Select", "", "Del");
  }
}

/**
 * @brief Handles input when TOC is shown
 * @param input Reference to the input manager
 */
void MenuDrawer::handleTocInput(const MappedInputManager& input) {
  if (!epub) return;

  uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  const int totalItems = epub->getTocItemsCount();
  const int pageItems = getTocPageItems();
  const bool skipPage = input.getHeldTime() > 700;

  if (totalItems == 0) {
    if (input.wasReleased(MappedInputManager::Button::Back)) {
      exitToc();
      lastInputTime = currentTime;
      renderWithRefresh();
    }
    return;
  }

  if (readDrawerListPrev(input, renderer)) {
    if (skipPage) {
      tocSelectedIndex = (tocSelectedIndex < pageItems) ? 0 : tocSelectedIndex - pageItems;
    } else {
      tocSelectedIndex = (tocSelectedIndex + totalItems - 1) % totalItems;
    }
    lastInputTime = currentTime;
    renderWithRefresh();
  } else if (readDrawerListNext(input, renderer)) {
    if (skipPage) {
      tocSelectedIndex = (tocSelectedIndex + pageItems >= totalItems) ? totalItems - 1 : tocSelectedIndex + pageItems;
    } else {
      tocSelectedIndex = (tocSelectedIndex + 1) % totalItems;
    }
    lastInputTime = currentTime;
    renderWithRefresh();
  } else if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    if (tocSelectedIndex >= 0 && tocSelectedIndex < totalItems) {
      const int newSpineIndex = epub->getSpineIndexForTocIndex(tocSelectedIndex);

      showingToc = false;
      visible = false;

      if (tocSelectionCallback) {
        tocSelectionCallback(newSpineIndex);
      }

      lastInputTime = currentTime;
    }
  } else if (input.wasReleased(MappedInputManager::Button::Back)) {
    exitToc();
    lastInputTime = currentTime;
    renderWithRefresh();
  }
}

/**
 * @brief Exits TOC view and returns to main menu
 */
void MenuDrawer::exitToc() {
  showingToc = false;
  selectedIndex = 0;
  scrollOffset = 0;
}

void MenuDrawer::exitBookmarks() {
  showingBookmarks = false;
  bookmarkSelectedIndex = 0;
  selectedIndex = 0;
  scrollOffset = 0;
}

void MenuDrawer::refreshBookmarkEntriesFromProvider() {
  if (bookmarkListProvider) {
    bookmarkEntries = bookmarkListProvider();
  } else {
    bookmarkEntries.clear();
  }
  if (bookmarkSelectedIndex >= static_cast<int>(bookmarkEntries.size())) {
    bookmarkSelectedIndex = std::max(0, static_cast<int>(bookmarkEntries.size()) - 1);
  }
}

void MenuDrawer::handleBookmarksInput(const MappedInputManager& input) {
  uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  const int totalItems = static_cast<int>(bookmarkEntries.size());
  const int pageItems = getTocPageItems();
  const bool skipPage = input.getHeldTime() > 700;

  if (totalItems == 0) {
    if (input.wasReleased(MappedInputManager::Button::Back)) {
      exitBookmarks();
      lastInputTime = currentTime;
      renderWithRefresh();
    }
    return;
  }

  if (readBookmarkLinePrev(input, renderer)) {
    if (skipPage) {
      bookmarkSelectedIndex = (bookmarkSelectedIndex < pageItems) ? 0 : bookmarkSelectedIndex - pageItems;
    } else {
      bookmarkSelectedIndex = (bookmarkSelectedIndex + totalItems - 1) % totalItems;
    }
    lastInputTime = currentTime;
    renderWithRefresh();
  } else if (readBookmarkLineNext(input, renderer)) {
    if (skipPage) {
      bookmarkSelectedIndex =
          (bookmarkSelectedIndex + pageItems >= totalItems) ? totalItems - 1 : bookmarkSelectedIndex + pageItems;
    } else {
      bookmarkSelectedIndex = (bookmarkSelectedIndex + 1) % totalItems;
    }
    lastInputTime = currentTime;
    renderWithRefresh();
  } else if ((!isLandscapeReader(renderer) && input.wasReleased(MappedInputManager::Button::Right)) ||
             (isLandscapeReader(renderer) && input.wasReleased(MappedInputManager::Button::Down))) {
    if (bookmarkDeleteCallback && bookmarkSelectedIndex >= 0 && bookmarkSelectedIndex < totalItems) {
      const int storageIndex = bookmarkEntries[static_cast<size_t>(bookmarkSelectedIndex)].storageIndex;
      bookmarkDeleteCallback(storageIndex);
      refreshBookmarkEntriesFromProvider();
      lastInputTime = currentTime;
      renderWithRefresh();
    }
  } else if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    if (bookmarkSelectedIndex >= 0 && bookmarkSelectedIndex < totalItems && bookmarkSelectCallback) {
      const int storageIndex = bookmarkEntries[static_cast<size_t>(bookmarkSelectedIndex)].storageIndex;
      showingBookmarks = false;
      visible = false;
      bookmarkSelectCallback(storageIndex);
    }
    lastInputTime = currentTime;
  } else if (input.wasReleased(MappedInputManager::Button::Back)) {
    exitBookmarks();
    lastInputTime = currentTime;
    renderWithRefresh();
  }
}

/**
 * @brief Handles input for the menu drawer
 * @param input Reference to the input manager
 */
void MenuDrawer::handleInput(MappedInputManager& input) {
  if (!visible) return;

  if (showingBookmarks) {
    handleBookmarksInput(input);
    return;
  }

  if (showingToc) {
    handleTocInput(input);
    return;
  }

  uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  if (readDrawerListPrev(input, renderer)) {
    if (selectedIndex > 0) {
      selectedIndex--;
      if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
      lastInputTime = currentTime;
      renderWithRefresh();
    }
  }
  if (readDrawerListNext(input, renderer)) {
    if (selectedIndex < static_cast<int>(menuItems.size()) - 1) {
      selectedIndex++;
      int maxScroll = std::max(0, (int)menuItems.size() - itemsPerPage);
      if (selectedIndex > scrollOffset + itemsPerPage - 1) {
        scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      }
      lastInputTime = currentTime;
      renderWithRefresh();
    }
  }
  if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(menuItems.size())) {
      if (menuItems[selectedIndex].action == MenuAction::SELECT_CHAPTER) {
        showingToc = true;
        tocSelectedIndex = 0;
        tocScrollOffset = 0;
        lastInputTime = currentTime;
        renderWithRefresh();
      } else if (menuItems[selectedIndex].action == MenuAction::SHOW_BOOKMARKS) {
        if (bookmarkListProvider) {
          bookmarkEntries = bookmarkListProvider();
        } else {
          bookmarkEntries.clear();
        }
        bookmarkSelectedIndex = 0;
        for (int i = 0; i < static_cast<int>(bookmarkEntries.size()); ++i) {
          if (bookmarkEntries[static_cast<size_t>(i)].isCurrentPosition) {
            bookmarkSelectedIndex = i;
            break;
          }
        }
        showingBookmarks = true;
        lastInputTime = currentTime;
        renderWithRefresh();
      } else {
        hide();
        if (onDismiss) {
          onDismiss();
        }
        lastInputTime = currentTime;

        if (onAction) {
          onAction(menuItems[selectedIndex].action);
        }
        renderWithRefresh();
      }
    }
  }
  if (input.wasReleased(MappedInputManager::Button::Back)) {
    hide();
    if (onDismiss) {
      onDismiss();
    }
    lastInputTime = currentTime;
    renderWithRefresh();
  }
}