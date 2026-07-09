/**
 * @file MenuDrawer.cpp
 * @brief Definitions for MenuDrawer.
 */

#include "MenuDrawer.h"

#include <algorithm>
#include <cstdio>

#include "Epub.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"

#define SETTINGS SystemSetting::getInstance()

constexpr int LIST_ITEM_HEIGHT = 60;
constexpr float TOC_DRAWER_HEIGHT_PERCENT = 0.8f;

namespace {

bool isLandscapeReader(const GfxRenderer& gfx) {
  const auto o = gfx.getOrientation();
  return o == GfxRenderer::LandscapeClockwise || o == GfxRenderer::LandscapeCounterClockwise;
}

/**
 * List line movement: use wasPressed like SettingsDrawer so one physical tap advances one row with a fast main loop
 * (wasReleased is easy to miss or double-count across frames).
 */
bool readDrawerListPrev(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasPressed(MappedInputManager::Button::Right) || in.wasPressed(MappedInputManager::Button::Up);
  }
  if (SETTINGS.readerDirectionMapping == SystemSetting::READER_DIRECTION_MAPPING::MAP_NONE) {
    return in.wasPressed(MappedInputManager::Button::Up) || in.wasPressed(MappedInputManager::Button::Left);
  }
  switch (SETTINGS.readerDirectionMapping) {
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_RIGHT_LEFT:
      return in.wasPressed(MappedInputManager::Button::Right);
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_UP_DOWN:
      return in.wasPressed(MappedInputManager::Button::Up);
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_DOWN_UP:
      return in.wasPressed(MappedInputManager::Button::Down);
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_LEFT_RIGHT:
    default:
      return in.wasPressed(MappedInputManager::Button::Left);
  }
}

bool readDrawerListNext(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasPressed(MappedInputManager::Button::Left) || in.wasPressed(MappedInputManager::Button::Down);
  }
  if (SETTINGS.readerDirectionMapping == SystemSetting::READER_DIRECTION_MAPPING::MAP_NONE) {
    return in.wasPressed(MappedInputManager::Button::Down) || in.wasPressed(MappedInputManager::Button::Right);
  }
  switch (SETTINGS.readerDirectionMapping) {
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_RIGHT_LEFT:
      return in.wasPressed(MappedInputManager::Button::Left);
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_UP_DOWN:
      return in.wasPressed(MappedInputManager::Button::Down);
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_DOWN_UP:
      return in.wasPressed(MappedInputManager::Button::Up);
    case SystemSetting::READER_DIRECTION_MAPPING::MAP_LEFT_RIGHT:
    default:
      return in.wasPressed(MappedInputManager::Button::Right);
  }
}

/** Bookmarks list: portrait keeps Up/Down only so Right stays reserved for delete. */
bool readBookmarkLinePrev(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return readDrawerListPrev(in, r);
  }
  return in.wasPressed(MappedInputManager::Button::Up);
}

bool readBookmarkLineNext(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return readDrawerListNext(in, r);
  }
  return in.wasPressed(MappedInputManager::Button::Down);
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
               {"Go To Percent", MenuAction::GO_TO_PERCENT},
               {"Show Bookmarks", MenuAction::SHOW_BOOKMARKS},
               {"Annotations", MenuAction::SHOW_ANNOTATIONS},
               {"KOReader Sync", MenuAction::KOREADER_SYNC},
               {"Delete Cache", MenuAction::DELETE_CACHE},
               {"Delete Progress", MenuAction::DELETE_PROGRESS},
               {"Delete Book", MenuAction::DELETE_BOOK},
               {"Generate Full Data", MenuAction::GENERATE_FULL_DATA},
               {"Regenerate Thumbnail", MenuAction::REGENERATE_THUMBNAIL},
               {"Go Home", MenuAction::GO_HOME}};
}

/**
 * @brief Destructor
 */
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
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, btn1, btn2, btn3, btn4);
}

void MenuDrawer::drawMappedButtonHints(const char* back, const char* confirm, const char* previous, const char* next) {
  if (mappedInputForHints != nullptr) {
    const auto labels = mappedInputForHints->mapLabels(back, confirm, previous, next);
    drawDrawerHintRow(labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    drawDrawerHintRow(back, confirm, previous, next);
  }
}

void MenuDrawer::drawMappedReaderNavHints(const char* back, const char* confirm, const char* prevSym,
                                          const char* nextSym) {
  if (mappedInputForHints != nullptr) {
    const auto labels =
        mappedInputForHints->mapLabelsWithReaderNav(back, confirm, prevSym, nextSym, isLandscapeReader(renderer));
    drawDrawerHintRow(labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    drawMappedButtonHints(back, confirm, prevSym, nextSym);
  }
}

MenuDrawer::~MenuDrawer() {
  onAction = nullptr;
  onDismiss = nullptr;
  tocSelectionCallback = nullptr;
  bookmarkListProvider = nullptr;
  bookmarkSelectCallback = nullptr;
  bookmarkDeleteCallback = nullptr;
  annotationListProvider = nullptr;
  annotationSelectCallback = nullptr;
  percentProvider = nullptr;
  percentSelectedCallback = nullptr;
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
  showingAnnotations = false;
  showingPercent = false;
  selectedIndex = 0;
  scrollOffset = 0;
  tocSelectedIndex = 0;
  tocScrollOffset = 0;
  bookmarkSelectedIndex = 0;
  bookmarkEntries.clear();
  annotationSelectedIndex = 0;
  annotationEntries.clear();
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
  showingAnnotations = false;
  showingPercent = false;
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
  } else if (showingAnnotations) {
    renderAnnotations();
  } else if (showingToc) {
    renderToc();
  } else if (showingPercent) {
    renderPercent();
  } else {
    drawBackground();
    drawMenuItems();
    drawScrollIndicator();
    drawMappedButtonHints("\xC2\xAB Back", "Select", "", "");
  }
  renderer.displayBuffer();
}

/**
 * @brief Draws the background panel of the menu drawer
 */
void MenuDrawer::drawBackground() {
  renderer.rectangle.fill(drawerX, drawerY, drawerWidth, drawerHeight, false);
  renderer.rectangle.render(drawerX, drawerY, drawerWidth, drawerHeight, true);

  // Header band is the same height as a list item, title vertically centered, with a divider beneath it.
  const int headerH = itemHeight;
  const int titleY = drawerY + (headerH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, drawerX + 20, titleY, "Reader Menu", true,
                       EpdFontFamily::BOLD);

  // Sit the divider on the last row of the header band; the first list item starts at drawerY+headerH and
  // its row fill would otherwise paint over a divider drawn exactly there.
  const int dividerY = drawerY + headerH - 1;
  renderer.line.render(drawerX, dividerY, drawerX + drawerWidth, dividerY, true);
}

/**
 * @brief Draws all menu items in the current scroll view
 */
void MenuDrawer::drawMenuItems() {
  for (int i = 0; i < itemsPerPage && (i + scrollOffset) < static_cast<int>(menuItems.size()); i++) {
    drawMenuItemRow(i, i + scrollOffset);
  }
}

void MenuDrawer::drawMenuItemRow(int visibleRow, int menuIndex) {
  if (menuIndex < 0 || menuIndex >= static_cast<int>(menuItems.size())) {
    return;
  }

  const int startY = drawerY + itemHeight;  // header band height == one list item
  const int itemY = startY + (visibleRow * itemHeight);
  const auto& item = menuItems[static_cast<size_t>(menuIndex)];
  const bool isSelected = (menuIndex == selectedIndex);

  renderer.rectangle.fill(
      drawerX, itemY, drawerWidth, itemHeight,
      isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));

  const int textX = drawerX + 23;
  const int textY = itemY + (itemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, item.label.c_str(), isSelected ? 0 : 1);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, drawerX + drawerWidth - 30, textY, "›", isSelected ? 0 : 1);
  renderer.line.render(drawerX, itemY + itemHeight - 1, drawerX + drawerWidth, itemY + itemHeight - 1, true);
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

  renderer.rectangle.fill(drawerX + drawerWidth - 4, thumbY, 2, thumbH, true);
}

void MenuDrawer::clearScrollIndicatorArea() {
  const int startY = drawerY + 80;
  const int listHeight = itemsPerPage * itemHeight;
  renderer.rectangle.fill(drawerX + drawerWidth - 5, startY, 4, listHeight, false);
}

void MenuDrawer::refreshMainMenuSelection(int previousIndex, bool redrawScrollIndicator) {
  if (!visible || showingToc || showingBookmarks || showingAnnotations || showingPercent) {
    return;
  }

  if (previousIndex >= scrollOffset && previousIndex < scrollOffset + itemsPerPage) {
    drawMenuItemRow(previousIndex - scrollOffset, previousIndex);
  }

  if (selectedIndex >= scrollOffset && selectedIndex < scrollOffset + itemsPerPage) {
    drawMenuItemRow(selectedIndex - scrollOffset, selectedIndex);
  }

  if (redrawScrollIndicator) {
    clearScrollIndicatorArea();
    drawScrollIndicator();
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

/**
 * @brief Gets the number of TOC items that fit on screen
 * @return Number of items per page
 */
int MenuDrawer::getTocPageItems() const {
  constexpr int headerReserved = 120;
  int items = (tocDrawerHeight - headerReserved) / LIST_ITEM_HEIGHT;
  return (items < 1) ? 1 : items;
}

/**
 * @brief Draws the TOC background with drawer effect
 */
void MenuDrawer::drawTocBackground() {
  renderer.rectangle.fill(tocDrawerX, tocDrawerY, tocDrawerWidth, tocDrawerHeight, false);
  renderer.rectangle.render(tocDrawerX, tocDrawerY, tocDrawerWidth, tocDrawerHeight, true);
}

/**
 * @brief Renders the Table of Contents view as a drawer
 */
void MenuDrawer::renderToc() {
  if (!epub) return;

  const int panelW = tocDrawerWidth;
  const int totalItems = epub->getTocItemsCount();
  const int pageItems = getTocPageItems();

  drawTocBackground();

  // Header band is the same height as a list item, title vertically centered, with a divider beneath it.
  const int headerH = LIST_ITEM_HEIGHT;
  const int headerY = tocDrawerY + (headerH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, tocDrawerX + 20, headerY, "Table of Contents", true,
                       EpdFontFamily::BOLD);

  const int dividerY = tocDrawerY + headerH;
  renderer.line.render(tocDrawerX, dividerY, tocDrawerX + panelW, dividerY, true);

  const int pageStartIndex = (tocSelectedIndex / pageItems) * pageItems;
  int drawY = dividerY + 2;

  if (totalItems == 0) {
    const int msgY = drawY + 24;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + 20, msgY, "No table of contents in this book.",
                         true);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + 20, msgY + 22,
                         "Try Delete Cache and reopen to rebuild.", true);
  } else {
    for (int i = 0; i < pageItems; i++) {
      int itemIndex = pageStartIndex + i;
      if (itemIndex >= totalItems) break;

      int itemY = drawY + (i * LIST_ITEM_HEIGHT);
      bool isSelected = (itemIndex == tocSelectedIndex);

      if (isSelected) {
        renderer.rectangle.fill(tocDrawerX, itemY, panelW, LIST_ITEM_HEIGHT,
                                static_cast<int>(GfxRenderer::FillTone::Ink));
      }

      int textY = itemY + (LIST_ITEM_HEIGHT - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

      auto item = epub->getTocItem(itemIndex);
      const int level = std::max(1, static_cast<int>(item.level));
      const int depthPx = (level - 1) * 20;
      const int maxDepthPx = std::max(0, panelW - 120);
      const int relIndent = 20 + std::min(depthPx, maxDepthPx);
      const int indentSize = tocDrawerX + relIndent;
      const int maxTitleW = std::max(40, panelW - 70 - relIndent);
      const std::string truncatedName =
          renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, item.title.c_str(), maxTitleW);

      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, indentSize, textY, truncatedName.c_str(),
                           isSelected ? 0 : 1);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + panelW - 30, textY, "›", isSelected ? 0 : 1);
      renderer.line.render(tocDrawerX, itemY + LIST_ITEM_HEIGHT - 1, tocDrawerX + panelW, itemY + LIST_ITEM_HEIGHT - 1,
                           true);
    }
  }

  const int totalPages = std::max(1, (totalItems + pageItems - 1) / pageItems);
  const int currentPageNum = (tocSelectedIndex / pageItems) + 1;
  char pageStr[24];
  snprintf(pageStr, sizeof(pageStr), "Page %d of %d", currentPageNum, totalPages);
  constexpr int kTocFooterAboveHints = 75;
  const int footerY = std::max(tocDrawerY + 8, tocDrawerY + tocDrawerHeight - kTocFooterAboveHints);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + 20, footerY, pageStr, true);

  drawMappedReaderNavHints("\xC2\xAB Back", "Select", "\xC2\xAB", "\xC2\xBB");
}

/**
 * @brief Renders bookmarks in the same drawer layout as the TOC
 */
void MenuDrawer::renderBookmarks() {
  const int panelW = tocDrawerWidth;
  const int totalItems = static_cast<int>(bookmarkEntries.size());
  const int pageItems = getTocPageItems();

  drawTocBackground();

  const int headerH = LIST_ITEM_HEIGHT;
  const int headerY = tocDrawerY + (headerH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, tocDrawerX + 20, headerY, "Bookmarks", true,
                       EpdFontFamily::BOLD);

  const int dividerY = tocDrawerY + headerH;
  renderer.line.render(tocDrawerX, dividerY, tocDrawerX + panelW, dividerY, true);

  if (totalItems == 0) {
    const char* line1 = "No bookmarks yet";
    const char* line2 = "Long press confirm to bookmark";
    const int lh = renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID);
    const int msgY = dividerY + 48;
    const int w1 = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, line1);
    const int w2 = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, line2);
    const int x1 = tocDrawerX + (panelW - w1) / 2;
    const int x2 = tocDrawerX + (panelW - w2) / 2;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, x1, msgY, line1, true);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, x2, msgY + lh + 6, line2, true);
    drawMappedButtonHints("\xC2\xAB Back", "", "", "");
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
      renderer.rectangle.fill(tocDrawerX, itemY, panelW, LIST_ITEM_HEIGHT,
                              static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    const int textY = itemY + (LIST_ITEM_HEIGHT - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
    const int kIndent = tocDrawerX + 20;
    const std::string truncated =
        renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, row.label.c_str(), panelW - 60 - 20);

    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, kIndent, textY, truncated.c_str(), isSelected ? 0 : 1);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + panelW - 30, textY, "›", isSelected ? 0 : 1);
    renderer.line.render(tocDrawerX, itemY + LIST_ITEM_HEIGHT - 1, tocDrawerX + panelW, itemY + LIST_ITEM_HEIGHT - 1,
                         true);
  }

  const int totalPages = (totalItems + pageItems - 1) / pageItems;
  const int currentPageNum = (bookmarkSelectedIndex / pageItems) + 1;
  char pageStr[24];
  snprintf(pageStr, sizeof(pageStr), "Page %d of %d", currentPageNum, totalPages);
  constexpr int kBookmarkFooterAboveHints = 75;
  const int footerY = std::max(tocDrawerY + 8, tocDrawerY + tocDrawerHeight - kBookmarkFooterAboveHints);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + 20, footerY, pageStr, true);

  drawMappedButtonHints("\xC2\xAB Back", "Select", "Up", "Del");
}

void MenuDrawer::renderAnnotations() {
  const int panelW = tocDrawerWidth;
  const int totalItems = static_cast<int>(annotationEntries.size());
  const int pageItems = getTocPageItems();

  drawTocBackground();

  const int headerH = LIST_ITEM_HEIGHT;
  const int headerY = tocDrawerY + (headerH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, tocDrawerX + 20, headerY, "Annotations", true,
                       EpdFontFamily::BOLD);

  const int dividerY = tocDrawerY + headerH;
  renderer.line.render(tocDrawerX, dividerY, tocDrawerX + panelW, dividerY, true);

  if (totalItems == 0) {
    const char* line1 = "No highlights yet";
    const char* line2 = "Front right + side Down/Right";
    const char* line3 = "to start annotating.";
    const int lh = renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID);
    const int subLh = renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID);
    const int msgY = dividerY + 48;
    const int w1 = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, line1);
    const int w2 = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, line2);
    const int w3 = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, line3);
    const int x1 = tocDrawerX + (panelW - w1) / 2;
    const int x2 = tocDrawerX + (panelW - w2) / 2;
    const int x3 = tocDrawerX + (panelW - w3) / 2;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, x1, msgY, line1, true);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, x2, msgY + lh + 8, line2, true);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, x3, msgY + lh + 8 + subLh + 4, line3, true);
    drawMappedButtonHints("\xC2\xAB Back", "", "", "");
    return;
  }

  const int pageStartIndex = (annotationSelectedIndex / pageItems) * pageItems;
  int drawY = dividerY + 2;

  for (int i = 0; i < pageItems; i++) {
    const int itemIndex = pageStartIndex + i;
    if (itemIndex >= totalItems) {
      break;
    }

    const int itemY = drawY + (i * LIST_ITEM_HEIGHT);
    const bool isSelected = (itemIndex == annotationSelectedIndex);
    const auto& row = annotationEntries[static_cast<size_t>(itemIndex)];

    if (isSelected) {
      renderer.rectangle.fill(tocDrawerX, itemY, panelW, LIST_ITEM_HEIGHT,
                              static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    const int textY = itemY + (LIST_ITEM_HEIGHT - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
    const int kIndent = tocDrawerX + 20;
    const std::string truncated =
        renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, row.label.c_str(), panelW - 60 - 20);

    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, kIndent, textY, truncated.c_str(), isSelected ? 0 : 1);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + panelW - 30, textY, "›", isSelected ? 0 : 1);
    renderer.line.render(tocDrawerX, itemY + LIST_ITEM_HEIGHT - 1, tocDrawerX + panelW, itemY + LIST_ITEM_HEIGHT - 1,
                         true);
  }

  const int totalPages = (totalItems + pageItems - 1) / pageItems;
  const int currentPageNum = (annotationSelectedIndex / pageItems) + 1;
  char pageStr[24];
  snprintf(pageStr, sizeof(pageStr), "Page %d of %d", currentPageNum, totalPages);
  constexpr int kAnnotationFooterAboveHints = 75;
  const int footerY = std::max(tocDrawerY + 8, tocDrawerY + tocDrawerHeight - kAnnotationFooterAboveHints);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tocDrawerX + 20, footerY, pageStr, true);

  drawMappedButtonHints("\xC2\xAB Back", "Select", "Up", "");
}

/**
 * @brief Renders the "Go to Percent" view in the same drawer panel/chrome as TOC/Bookmarks/Annotations
 */
void MenuDrawer::renderPercent() {
  const int panelW = tocDrawerWidth;

  drawTocBackground();

  // Header band matches TOC/Bookmarks/Annotations exactly.
  const int headerH = LIST_ITEM_HEIGHT;
  const int headerY = tocDrawerY + (headerH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, tocDrawerX + 20, headerY, "Go to Percent", true,
                       EpdFontFamily::BOLD);

  const int dividerY = tocDrawerY + headerH;
  renderer.line.render(tocDrawerX, dividerY, tocDrawerX + panelW, dividerY, true);

  const int centerX = tocDrawerX + panelW / 2;

  const std::string percentText = std::to_string(percentValue_) + "%";
  const int pctWidth =
      renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, percentText.c_str(), EpdFontFamily::BOLD);
  const int pctY = dividerY + 40;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, centerX - pctWidth / 2, pctY, percentText.c_str(), true,
                       EpdFontFamily::BOLD);

  // Slider track, sized to fit the drawer panel (narrower in landscape's half-screen layout).
  const int barWidth = std::min(300, panelW - 80);
  const int barHeight = 16;
  const int barX = centerX - barWidth / 2;
  const int barY = pctY + 60;

  renderer.rectangle.render(barX, barY, barWidth, barHeight, true);

  const int fillWidth = (barWidth - 4) * percentValue_ / 100;
  if (fillWidth > 0) {
    renderer.rectangle.fill(barX + 2, barY + 2, fillWidth, barHeight - 4, true);
  }

  const int knobX = barX + 2 + fillWidth - 2;
  renderer.rectangle.fill(knobX, barY - 4, 4, barHeight + 8, true);

  // Hint text for step sizes, positioned like TOC/Bookmarks/Annotations' footer page counter.
  const std::string hintText =
      renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "Left/Right: +/-1%  Up/Down: +/-10%", panelW - 40);
  const int hintWidth = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, hintText.c_str());
  constexpr int kPercentFooterAboveHints = 75;
  const int footerY = std::max(tocDrawerY + 8, tocDrawerY + tocDrawerHeight - kPercentFooterAboveHints);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerX - hintWidth / 2, footerY, hintText.c_str(), true);

  drawMappedButtonHints("\xC2\xAB Back", "Select", "-", "+");
}

/**
 * @brief Handles input when the percent picker is shown
 * @param input Reference to the input manager
 */
void MenuDrawer::handlePercentInput(const MappedInputManager& input) {
  if (input.wasReleased(MappedInputManager::Button::Back)) {
    exitPercent();
    lastInputTime = xTaskGetTickCount();
    renderWithRefresh();
    return;
  }

  if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    showingPercent = false;
    visible = false;
    if (percentSelectedCallback) {
      percentSelectedCallback(percentValue_);
    }
    lastInputTime = xTaskGetTickCount();
    return;
  }

  const uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  int delta = 0;
  if (input.isPressed(MappedInputManager::Button::Left)) {
    delta = -1;
  } else if (input.isPressed(MappedInputManager::Button::Right)) {
    delta = 1;
  } else if (input.isPressed(MappedInputManager::Button::Up)) {
    delta = 10;
  } else if (input.isPressed(MappedInputManager::Button::Down)) {
    delta = -10;
  }

  if (delta != 0) {
    percentValue_ = std::max(0, std::min(100, percentValue_ + delta));
    lastInputTime = currentTime;
    renderWithRefresh();
  }
}

/**
 * @brief Exits percent view and returns to main menu
 */
void MenuDrawer::exitPercent() {
  showingPercent = false;
  selectedIndex = 0;
  scrollOffset = 0;
}

/**
 * @brief Handles input when TOC is shown
 * @param input Reference to the input manager
 */
void MenuDrawer::handleTocInput(const MappedInputManager& input) {
  if (!epub) return;

  const int totalItems = epub->getTocItemsCount();
  const int pageItems = getTocPageItems();
  const bool skipPage = input.getHeldTime() > 700;

  if (totalItems == 0) {
    if (input.wasReleased(MappedInputManager::Button::Back)) {
      exitToc();
      lastInputTime = xTaskGetTickCount();
      renderWithRefresh();
    }
    return;
  }

  if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    if (tocSelectedIndex >= 0 && tocSelectedIndex < totalItems) {
      const int newSpineIndex = epub->getSpineIndexForTocIndex(tocSelectedIndex);

      showingToc = false;
      visible = false;

      if (tocSelectionCallback) {
        tocSelectionCallback(newSpineIndex);
      }

      lastInputTime = xTaskGetTickCount();
    }
    return;
  }

  if (input.wasReleased(MappedInputManager::Button::Back)) {
    exitToc();
    lastInputTime = xTaskGetTickCount();
    renderWithRefresh();
    return;
  }

  const uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  auto selectPrevItem = [&]() { tocSelectedIndex = (tocSelectedIndex + totalItems - 1) % totalItems; };
  auto selectNextItem = [&]() { tocSelectedIndex = (tocSelectedIndex + 1) % totalItems; };
  auto pageUp = [&]() { tocSelectedIndex = (tocSelectedIndex < pageItems) ? 0 : tocSelectedIndex - pageItems; };
  auto pageDown = [&]() {
    tocSelectedIndex = (tocSelectedIndex + pageItems >= totalItems) ? totalItems - 1 : tocSelectedIndex + pageItems;
  };

  bool moved = false;
  if (isLandscapeReader(renderer)) {
    // Landscape keeps the orientation-aware list mapping (long-hold = page jump).
    if (readDrawerListPrev(input, renderer)) {
      skipPage ? pageUp() : selectPrevItem();
      moved = true;
    } else if (readDrawerListNext(input, renderer)) {
      skipPage ? pageDown() : selectNextItem();
      moved = true;
    }
  } else {
    // Portrait: Up/Down move one chapter, Left/Right jump a whole page.
    if (input.wasPressed(MappedInputManager::Button::Up)) {
      selectPrevItem();
      moved = true;
    } else if (input.wasPressed(MappedInputManager::Button::Down)) {
      selectNextItem();
      moved = true;
    } else if (input.wasPressed(MappedInputManager::Button::Left)) {
      pageUp();
      moved = true;
    } else if (input.wasPressed(MappedInputManager::Button::Right)) {
      pageDown();
      moved = true;
    }
  }

  if (moved) {
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

void MenuDrawer::exitAnnotations() {
  showingAnnotations = false;
  annotationSelectedIndex = 0;
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

void MenuDrawer::handleAnnotationsInput(const MappedInputManager& input) {
  const int totalItems = static_cast<int>(annotationEntries.size());
  const int pageItems = getTocPageItems();
  const bool skipPage = input.getHeldTime() > 700;

  if (totalItems == 0) {
    if (input.wasReleased(MappedInputManager::Button::Back)) {
      exitAnnotations();
      lastInputTime = xTaskGetTickCount();
      renderWithRefresh();
    }
    return;
  }

  if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    if (annotationSelectedIndex >= 0 && annotationSelectedIndex < totalItems && annotationSelectCallback) {
      const int storageIndex = annotationEntries[static_cast<size_t>(annotationSelectedIndex)].storageIndex;
      showingAnnotations = false;
      visible = false;
      annotationSelectCallback(storageIndex);
    }
    lastInputTime = xTaskGetTickCount();
    return;
  }

  if (input.wasReleased(MappedInputManager::Button::Back)) {
    exitAnnotations();
    lastInputTime = xTaskGetTickCount();
    renderWithRefresh();
    return;
  }

  const uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  if (readBookmarkLinePrev(input, renderer)) {
    if (skipPage) {
      annotationSelectedIndex = (annotationSelectedIndex < pageItems) ? 0 : annotationSelectedIndex - pageItems;
    } else {
      annotationSelectedIndex = (annotationSelectedIndex + totalItems - 1) % totalItems;
    }
    lastInputTime = currentTime;
    renderWithRefresh();
  } else if (readBookmarkLineNext(input, renderer)) {
    if (skipPage) {
      annotationSelectedIndex =
          (annotationSelectedIndex + pageItems >= totalItems) ? totalItems - 1 : annotationSelectedIndex + pageItems;
    } else {
      annotationSelectedIndex = (annotationSelectedIndex + 1) % totalItems;
    }
    lastInputTime = currentTime;
    renderWithRefresh();
  }
}

void MenuDrawer::handleBookmarksInput(const MappedInputManager& input) {
  const int totalItems = static_cast<int>(bookmarkEntries.size());
  const int pageItems = getTocPageItems();
  const bool skipPage = input.getHeldTime() > 700;

  if (totalItems == 0) {
    if (input.wasReleased(MappedInputManager::Button::Back)) {
      exitBookmarks();
      lastInputTime = xTaskGetTickCount();
      renderWithRefresh();
    }
    return;
  }

  if ((!isLandscapeReader(renderer) && input.wasReleased(MappedInputManager::Button::Right)) ||
      (isLandscapeReader(renderer) && input.wasReleased(MappedInputManager::Button::Down))) {
    if (bookmarkDeleteCallback && bookmarkSelectedIndex >= 0 && bookmarkSelectedIndex < totalItems) {
      const int storageIndex = bookmarkEntries[static_cast<size_t>(bookmarkSelectedIndex)].storageIndex;
      bookmarkDeleteCallback(storageIndex);
      refreshBookmarkEntriesFromProvider();
      lastInputTime = xTaskGetTickCount();
      renderWithRefresh();
    }
    return;
  }

  if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    if (bookmarkSelectedIndex >= 0 && bookmarkSelectedIndex < totalItems && bookmarkSelectCallback) {
      const int storageIndex = bookmarkEntries[static_cast<size_t>(bookmarkSelectedIndex)].storageIndex;
      showingBookmarks = false;
      visible = false;
      bookmarkSelectCallback(storageIndex);
    }
    lastInputTime = xTaskGetTickCount();
    return;
  }

  if (input.wasReleased(MappedInputManager::Button::Back)) {
    exitBookmarks();
    lastInputTime = xTaskGetTickCount();
    renderWithRefresh();
    return;
  }

  const uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
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

  if (showingAnnotations) {
    handleAnnotationsInput(input);
    return;
  }

  if (showingToc) {
    handleTocInput(input);
    return;
  }

  if (showingPercent) {
    handlePercentInput(input);
    return;
  }

  if (input.wasReleased(MappedInputManager::Button::Back)) {
    hide();
    if (onDismiss) {
      onDismiss();
    }
    lastInputTime = xTaskGetTickCount();
    return;
  }

  if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(menuItems.size())) {
      if (menuItems[selectedIndex].action == MenuAction::SELECT_CHAPTER) {
        showingToc = true;
        tocScrollOffset = 0;
        tocSelectedIndex = 0;
        if (epub && readerSpineIndex_ >= 0) {
          const int ti = epub->getTocIndexForSpineIndex(readerSpineIndex_);
          if (ti >= 0) {
            tocSelectedIndex = ti;
          }
        }
        lastInputTime = xTaskGetTickCount();
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
        lastInputTime = xTaskGetTickCount();
        renderWithRefresh();
      } else if (menuItems[selectedIndex].action == MenuAction::SHOW_ANNOTATIONS) {
        if (annotationListProvider) {
          annotationEntries = annotationListProvider();
        } else {
          annotationEntries.clear();
        }
        annotationSelectedIndex = 0;
        for (int i = 0; i < static_cast<int>(annotationEntries.size()); ++i) {
          if (annotationEntries[static_cast<size_t>(i)].isCurrentPosition) {
            annotationSelectedIndex = i;
            break;
          }
        }
        showingAnnotations = true;
        lastInputTime = xTaskGetTickCount();
        renderWithRefresh();
      } else if (menuItems[selectedIndex].action == MenuAction::GO_TO_PERCENT) {
        percentValue_ = percentProvider ? percentProvider() : 0;
        showingPercent = true;
        lastInputTime = xTaskGetTickCount();
        renderWithRefresh();
      } else {
        hide();
        if (onDismiss) {
          onDismiss();
        }
        lastInputTime = xTaskGetTickCount();

        if (onAction) {
          onAction(menuItems[selectedIndex].action);
        }
      }
    }
    return;
  }

  const uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  if (readDrawerListPrev(input, renderer)) {
    const int previousIndex = selectedIndex;
    if (selectedIndex > 0) {
      selectedIndex--;
      const bool scrolled = selectedIndex < scrollOffset;
      if (scrolled) {
        scrollOffset = selectedIndex;
      }
      lastInputTime = currentTime;
      if (scrolled) {
        renderWithRefresh();
      } else {
        refreshMainMenuSelection(previousIndex, false);
      }
    }
  } else if (readDrawerListNext(input, renderer)) {
    const int previousIndex = selectedIndex;
    if (selectedIndex < static_cast<int>(menuItems.size()) - 1) {
      selectedIndex++;
      int maxScroll = std::max(0, (int)menuItems.size() - itemsPerPage);
      const bool scrolled = selectedIndex > scrollOffset + itemsPerPage - 1;
      if (scrolled) {
        scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      }
      lastInputTime = currentTime;
      if (scrolled) {
        renderWithRefresh();
      } else {
        refreshMainMenuSelection(previousIndex, false);
      }
    }
  }
}
