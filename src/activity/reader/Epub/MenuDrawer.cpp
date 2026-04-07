#include "MenuDrawer.h"

#include "Epub.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"

constexpr int LIST_ITEM_HEIGHT = 60;
constexpr float TOC_DRAWER_HEIGHT_PERCENT = 0.8f;  // TOC takes 80% of screen height

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
  drawerHeight = renderer.getScreenHeight() * 80 / 100;
  drawerY = renderer.getScreenHeight() - drawerHeight;
  itemHeight = LIST_ITEM_HEIGHT;
  itemsPerPage = (drawerHeight - 100) / itemHeight;

  // Initialize TOC drawer dimensions
  tocDrawerHeight = renderer.getScreenHeight() * TOC_DRAWER_HEIGHT_PERCENT;
  tocDrawerY = renderer.getScreenHeight() - tocDrawerHeight;

  menuItems = {{"Table of Contents", MenuAction::SELECT_CHAPTER},
               {"Show Bookmarks", MenuAction::SHOW_BOOKMARKS},
               {"Delete Cache", MenuAction::DELETE_CACHE},
               {"Delete Progress", MenuAction::DELETE_PROGRESS},
               {"Delete Book", MenuAction::DELETE_BOOK},
               {"Generate Full Data", MenuAction::GENERATE_FULL_DATA},
               {"Go Home", MenuAction::GO_HOME}};
}

/**
 * @brief Destructor
 */
MenuDrawer::~MenuDrawer() {
  onAction = nullptr;
  onDismiss = nullptr;
  tocSelectionCallback = nullptr;
  epub = nullptr;
}

/**
 * @brief Shows the menu drawer
 */
void MenuDrawer::show() {
  if (visible) return;
  visible = true;
  dismissed = false;
  showingToc = false;
  selectedIndex = 0;
  scrollOffset = 0;
  tocSelectedIndex = 0;
  tocScrollOffset = 0;
  renderWithRefresh();
}

/**
 * @brief Hides the menu drawer
 */
void MenuDrawer::hide() {
  visible = false;
  dismissed = true;
  showingToc = false;
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

  if (showingToc) {
    renderToc();
  } else {
    drawBackground();
    drawMenuItems();
    drawScrollIndicator();
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "« Back", "Select", "", "");
  }
  renderer.displayBuffer();
}

/**
 * @brief Draws the background panel of the menu drawer
 */
void MenuDrawer::drawBackground() {
  int screenW = renderer.getScreenWidth();
  renderer.fillRect(0, drawerY, screenW, drawerHeight, false);
  renderer.drawRect(0, drawerY, screenW, drawerHeight, true);

  int currentY = drawerY + 10;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, currentY, "Reader Menu", true, EpdFontFamily::BOLD);

  std::string displayTitle = bookTitle;
  if (displayTitle.length() > 30) {
    displayTitle.replace(27, std::string::npos, "...");
  }

  currentY += 25;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, currentY, displayTitle.c_str(), true);

  int dividerY = currentY + 30;
  renderer.drawLine(0, dividerY, screenW, dividerY, true);
}

/**
 * @brief Draws all menu items in the current scroll view
 */
void MenuDrawer::drawMenuItems() {
  int startY = drawerY + 65;
  int screenW = renderer.getScreenWidth();

  for (int i = 0; i < itemsPerPage && (i + scrollOffset) < static_cast<int>(menuItems.size()); i++) {
    int index = i + scrollOffset;
    int itemY = startY + (i * itemHeight);
    const auto& item = menuItems[index];
    bool isSelected = (index == selectedIndex);

    if (isSelected) {
      renderer.fillRect(0, itemY, screenW, itemHeight, true);
    }

    int textX = 23;
    int textY = itemY + (itemHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, item.label.c_str(), isSelected ? 0 : 1);

    // Draw arrow indicator
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenW - 30, textY, "›", isSelected ? 0 : 1);

    renderer.drawLine(0, itemY + itemHeight - 1, screenW, itemY + itemHeight - 1, true);
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

  renderer.fillRect(renderer.getScreenWidth() - 4, thumbY, 2, thumbH, true);
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
  int screenW = renderer.getScreenWidth();

  // Draw the TOC drawer background
  renderer.fillRect(0, tocDrawerY, screenW, tocDrawerHeight, false);
  renderer.drawRect(0, tocDrawerY, screenW, tocDrawerHeight, true);
}

/**
 * @brief Renders the Table of Contents view as a drawer
 */
void MenuDrawer::renderToc() {
  if (!epub) return;

  const int screenWidth = renderer.getScreenWidth();
  const int totalItems = epub->getTocItemsCount();
  const int pageItems = getTocPageItems();

  // Draw TOC background
  drawTocBackground();

  // Header
  const int headerY = tocDrawerY + 10;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, headerY, "Table of Contents", true, EpdFontFamily::BOLD);

  const char* subtitleText = "Select a chapter to begin reading";
  int subtitleY = headerY + 30;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, screenWidth, dividerY, true);

  const int pageStartIndex = (tocSelectedIndex / pageItems) * pageItems;
  int drawY = dividerY + 2;

  for (int i = 0; i < pageItems; i++) {
    int itemIndex = pageStartIndex + i;
    if (itemIndex >= totalItems) break;

    int itemY = drawY + (i * LIST_ITEM_HEIGHT);
    bool isSelected = (itemIndex == tocSelectedIndex);

    if (isSelected) {
      renderer.fillRect(0, itemY, screenWidth, LIST_ITEM_HEIGHT, true);
    }

    int textY = itemY + (LIST_ITEM_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

    auto item = epub->getTocItem(itemIndex);
    int indentSize = 20 + (item.level - 1) * 20;
    const std::string truncatedName =
        renderer.truncatedText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, item.title.c_str(), screenWidth - 60 - indentSize);

    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, indentSize, textY, truncatedName.c_str(), isSelected ? 0 : 1);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenWidth - 30, textY, "›", isSelected ? 0 : 1);
    renderer.drawLine(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1, true);
  }

  // Footer with page indicator
  const int totalPages = (totalItems + pageItems - 1) / pageItems;
  const int currentPageNum = (tocSelectedIndex / pageItems) + 1;
  char pageStr[24];
  snprintf(pageStr, sizeof(pageStr), "Page %d of %d", currentPageNum, totalPages);
  int pageStrWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageStr);
  int footerY = tocDrawerY + tocDrawerHeight - 35;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, (screenWidth - pageStrWidth) / 2, footerY, pageStr, true);

  // Button hints for TOC view
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "« Back", "Select", "«", "»");
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

  if (input.wasReleased(MappedInputManager::Button::Up)) {
    if (skipPage) {
      tocSelectedIndex = (tocSelectedIndex < pageItems) ? 0 : tocSelectedIndex - pageItems;
    } else {
      tocSelectedIndex = (tocSelectedIndex + totalItems - 1) % totalItems;
    }
    lastInputTime = currentTime;
    renderWithRefresh();
  } else if (input.wasReleased(MappedInputManager::Button::Down)) {
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

/**
 * @brief Handles input for the menu drawer
 * @param input Reference to the input manager
 */
void MenuDrawer::handleInput(MappedInputManager& input) {
  if (!visible) return;

  if (showingToc) {
    handleTocInput(input);
    return;
  }

  uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  if (input.wasReleased(MappedInputManager::Button::Up)) {
    if (selectedIndex > 0) {
      selectedIndex--;
      if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
      lastInputTime = currentTime;
      renderWithRefresh();
    }
  }
  if (input.wasReleased(MappedInputManager::Button::Down)) {
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