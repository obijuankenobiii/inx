/**
 * @file SyncActivity.cpp
 * @brief Definitions for SyncActivity.
 */

#include "../page/SyncActivity.h"

#include <GfxRenderer.h>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/UiTheme.h"

namespace {
constexpr int MENU_ITEM_COUNT = 4;
const char* MENU_ITEMS[MENU_ITEM_COUNT] = {"Join a Network", "Connect to Calibre", "Create Hotspot", "OPDS Browser"};
constexpr int LIST_ITEM_HEIGHT = UiTheme::DRAWER_LIST_ITEM_HEIGHT;
}  // namespace

/**
 * Lifecycle hook called when entering the activity.
 */
void SyncActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;

  render();
  SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Sync);
}

/**
 * Main loop for handling user input and updating the display state.
 * Processes button presses for menu navigation and tab switching.
 */
void SyncActivity::loop() {
  if (tabSelectorIndex == 3 && updateRequired) {
    updateRequired = false;
    render();
  }

  const bool confirmPressed = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool upPressed = mappedInput.wasPressed(MenuNav::itemPrev());
  const bool downPressed = mappedInput.wasPressed(MenuNav::itemNext());

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() >= 300 && onRecentOpen) {
      vTaskDelay(pdMS_TO_TICKS(300));
      onRecentOpen();
    }
    return;
  }

  if (mappedInput.wasPressed(MenuNav::tabPrev())) {
    tabSelectorIndex = 2;
    navigateToSelectedMenu();
    return;
  }

  if (mappedInput.wasPressed(MenuNav::tabNext())) {
    tabSelectorIndex = 4;
    navigateToSelectedMenu();
    return;
  }

  if (tabSelectorIndex != 3) {
    return;
  }

  if (confirmPressed) {
    NetworkMode mode = NetworkMode::JOIN_NETWORK;

    if (selectedIndex == 1) {
      mode = NetworkMode::CONNECT_CALIBRE;
    }

    if (selectedIndex == 2) {
      mode = NetworkMode::CREATE_HOTSPOT;
    }

    if (selectedIndex == 3) {
      mode = NetworkMode::OPDS_BROWSER;
    }

    if (onModeSelected) {
      onModeSelected(mode);
    }
    return;
  }

  bool needUpdate = false;

  if (upPressed) {
    selectedIndex = (selectedIndex + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
    needUpdate = true;
  }

  if (downPressed) {
    selectedIndex = (selectedIndex + 1) % MENU_ITEM_COUNT;
    needUpdate = true;
  }

  if (needUpdate) {
    updateRequired = true;
  }
}

/**
 * Renders the complete sync activity view including menu items and tab bar.
 */
void SyncActivity::render() const {
  renderer.clearScreen();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int contentBottom = mainContentBottom(renderer);

  renderTabBar(renderer);

  const int headerY = mainContentTop();
  const int headerHeight = TAB_BAR_HEIGHT;
  const int headerTextY = headerY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, headerTextY, "Device connections", true,
                       EpdFontFamily::BOLD);

  const int dividerY = headerY + headerHeight;
  renderer.line.render(0, dividerY, screenWidth, dividerY);

  const int listStartY = dividerY;
  const int visibleAreaHeight = (INX_THEME.mainTabsAtBottom() ? contentBottom : screenHeight - 80) - listStartY;

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    const int itemY = listStartY + i * LIST_ITEM_HEIGHT;

    if (itemY < listStartY + visibleAreaHeight && itemY + LIST_ITEM_HEIGHT > listStartY) {
      const bool isSelected = (i == selectedIndex);

      if (isSelected) {
        renderer.rectangle.fill(0, itemY, screenWidth, LIST_ITEM_HEIGHT, static_cast<int>(GfxRenderer::FillTone::Ink));
      }

      const int textX = 20;
      const int titleY = itemY + (LIST_ITEM_HEIGHT - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, titleY, MENU_ITEMS[i], !isSelected);
      const int caretW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "›");
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenWidth - caretW - 30, titleY, "›", !isSelected);

      if (i < MENU_ITEM_COUNT - 1) {
        renderer.line.render(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1, true,
                             LineRender::Style::Dotted);
      }
    }
  }

  const auto labels = mappedInput.mapLabels("« Recent", "Select", "", "");
  renderButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

/**
 * Lifecycle hook called when exiting the activity.
 */
void SyncActivity::onExit() { Activity::onExit(); }
