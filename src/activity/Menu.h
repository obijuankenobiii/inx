#ifndef BASE_TAB_ACTIVITY_H
#define BASE_TAB_ACTIVITY_H

#include <GfxRenderer.h>

#include "system/Battery.h"
#include "state/SystemSetting.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"
#include "system/Fonts.h"

// Include the actual icon images
#include "images/Library.h"
#include "images/Recent.h"
#include "images/Setting.h"
#include "images/Sync.h"
#include "images/Stats.h"

class Menu {
 protected:
  static constexpr int TAB_BAR_HEIGHT = 72;
  static constexpr int TAB_COUNT = 5;
  static constexpr int ICON_SIZE = 40;
  static constexpr int BATTERY_Y = 30;
  static constexpr int SELECTED_BORDER_HEIGHT = 5;
  int tabSelectorIndex = 0;

  Menu() = default;
  virtual ~Menu() = default;

  void renderTabBar(GfxRenderer& renderer, int startY) const {
    const int screenWidth = renderer.getScreenWidth();
    const int tabButtonWidth = screenWidth / TAB_COUNT;

    for (int i = 0; i < TAB_COUNT; ++i) {
      int buttonX = i * tabButtonWidth;
      int buttonY = startY;
      bool isSelected = (tabSelectorIndex == i);
      int iconX = buttonX + (tabButtonWidth - ICON_SIZE) / 2;
      int iconY = buttonY + (TAB_BAR_HEIGHT - ICON_SIZE) / 2 + 5;

      switch (i) {
        case 0:
          renderer.drawIcon(Recent, iconX, iconY, ICON_SIZE, ICON_SIZE, GfxRenderer::Rotate270CW);
          break;
        case 1:
          renderer.drawIcon(Library, iconX, iconY, ICON_SIZE, ICON_SIZE, GfxRenderer::Rotate270CW);
          break;
        case 2:
          renderer.drawIcon(Setting, iconX, iconY, ICON_SIZE, ICON_SIZE, GfxRenderer::Rotate270CW);
          break;
        case 3:
          renderer.drawIcon(Sync, iconX, iconY, ICON_SIZE, ICON_SIZE, GfxRenderer::Rotate270CW);
          break;
        case 4:
          renderer.drawIcon(Stats, iconX, iconY, ICON_SIZE, ICON_SIZE, GfxRenderer::Rotate270CW);
          break;
      }

      int bottomLineY = startY + TAB_BAR_HEIGHT - 5;
      if (isSelected) {
        renderer.fillRect(iconX - 10, bottomLineY - 2, 60, SELECTED_BORDER_HEIGHT, 1);
      }

      renderer.drawLine(buttonX, bottomLineY, buttonX + tabButtonWidth, bottomLineY);
    }
    drawBattery(renderer);
  }

  void drawBattery(GfxRenderer& renderer) const {
    ScreenComponents::drawBattery(
        renderer, renderer.getScreenWidth() - 80, renderer.getScreenHeight() - 30,
        SETTINGS.hideBatteryPercentage != SystemSetting::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);
  }

  virtual void navigateToSelectedMenu() = 0;

  void handleTabNavigation(bool leftPressed, bool rightPressed) {
    if (leftPressed) {
      tabSelectorIndex = (tabSelectorIndex - 1 + TAB_COUNT) % TAB_COUNT;
      navigateToSelectedMenu();
    }
    if (rightPressed) {
      tabSelectorIndex = (tabSelectorIndex + 1) % TAB_COUNT;
      navigateToSelectedMenu();
    }
  }
};

#endif