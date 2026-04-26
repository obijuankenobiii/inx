/**
 * @file SyncActivity.cpp
 * @brief Definitions for SyncActivity.
 */

#include "../page/SyncActivity.h"

#include <GfxRenderer.h>

#include "system/Fonts.h"
#include "system/MappedInputManager.h"

#include "images/Wifi.h"
#include "images/Qr.h"
#include "images/Calibre.h"
#include "images/Sync.h"
#include "state/SystemSetting.h"

namespace {
constexpr int MENU_ITEM_COUNT = 4;
const char* MENU_ITEMS[MENU_ITEM_COUNT] = {"Join a Network", "Connect to Calibre", "Create Hotspot",
                                           "Bluetooth remote"};
constexpr int LIST_ITEM_HEIGHT = 60;
}

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

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    updateRequired = true;
    return;
  }
  const bool confirmPressed = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() >= 300 && onRecentOpen) {
      vTaskDelay(pdMS_TO_TICKS(300));
      onRecentOpen();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    tabSelectorIndex = 2;
    navigateToSelectedMenu();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    tabSelectorIndex = 4;
    navigateToSelectedMenu();
    return;
  }

  if (tabSelectorIndex != 3) {
    return;
  }

  if (confirmPressed) {
    if (selectedIndex == 3) {
      if (onBleRemoteSetup) {
        onBleRemoteSetup();
      }
      return;
    }

    NetworkMode mode = NetworkMode::JOIN_NETWORK;

    if (selectedIndex == 1) {
      mode = NetworkMode::CONNECT_CALIBRE;
    }

    if (selectedIndex == 2) {
      mode = NetworkMode::CREATE_HOTSPOT;
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

  renderTabBar(renderer);

  const int headerY = TAB_BAR_HEIGHT;
  const int headerHeight = TAB_BAR_HEIGHT;
  const int headerTextY =
      headerY + (headerHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, headerTextY, "File Transfer", true, EpdFontFamily::BOLD);

  const int dividerY = headerY + headerHeight;
  renderer.drawLine(0, dividerY, screenWidth, dividerY);

  const int listStartY = dividerY;
  const int visibleAreaHeight = screenHeight - listStartY - 80;

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    const int itemY = listStartY + i * LIST_ITEM_HEIGHT;

    if (itemY < listStartY + visibleAreaHeight && itemY + LIST_ITEM_HEIGHT > listStartY) {
      const bool isSelected = (i == selectedIndex);

      if (isSelected) {
        renderer.fillRect(0, itemY, screenWidth, LIST_ITEM_HEIGHT, GfxRenderer::FillTone::Ink);
      }

      constexpr int kIconSize = 40;
      const int textX = 70;
      const int iconX = (textX - kIconSize) / 2;
      const int titleY =
          itemY + (LIST_ITEM_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
      const int iconY = itemY + (LIST_ITEM_HEIGHT - kIconSize) / 2;

      switch (i) {
        case 0:  
          renderer.drawIcon(Wifi, iconX, iconY, kIconSize, kIconSize, GfxRenderer::None, isSelected);
          break;
        case 1:  
          renderer.drawIcon(Calibre, iconX, iconY, kIconSize, kIconSize, GfxRenderer::None, isSelected);
          break;
        case 2:  
          renderer.drawIcon(Qr, iconX, iconY, kIconSize, kIconSize, GfxRenderer::None, isSelected);
          break;
        case 3:
          renderer.drawIcon(Sync, iconX, iconY, kIconSize, kIconSize, GfxRenderer::None, isSelected);
          break;
      }
      
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, titleY, MENU_ITEMS[i], !isSelected);

      if (i < MENU_ITEM_COUNT - 1) {
        renderer.drawLine(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1);
      }
    }
  }

  const auto labels = mappedInput.mapLabels("« Recent", "Select", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void SyncActivity::onExit() {
  Activity::onExit();
}