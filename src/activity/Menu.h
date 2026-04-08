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
  static constexpr int TAB_BAR_HEIGHT = 65;
  static constexpr int TAB_COUNT = 5;
  static constexpr int ICON_SIZE = 40;
  static constexpr int BATTERY_Y = 30;
  static constexpr int SELECTED_BORDER_HEIGHT = 5;
  int tabSelectorIndex = 0;

  Menu() = default;
  virtual ~Menu() = default;

  /**
   * @brief Renders the tab bar with icons and selection indicator
   * @param renderer Reference to the graphics renderer (const)
   */
  void renderTabBar(const GfxRenderer& renderer) const {
    const int screenWidth = renderer.getScreenWidth();
    const int tabButtonWidth = (screenWidth / TAB_COUNT) - 1;

    for (int i = 0; i < TAB_COUNT; ++i) {
      int buttonX = i * tabButtonWidth;
      bool isSelected = (tabSelectorIndex == i);
      int iconX = buttonX + (tabButtonWidth - ICON_SIZE) / 2;
      int iconY = (TAB_BAR_HEIGHT - ICON_SIZE) / 2 + 5;

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

      if (isSelected) {
        renderer.fillRect(iconX - 10, TAB_BAR_HEIGHT - 2, 60, SELECTED_BORDER_HEIGHT, 1);
      }

      renderer.drawLine(buttonX, TAB_BAR_HEIGHT, buttonX + tabButtonWidth, TAB_BAR_HEIGHT);
    }
    drawBattery(renderer);
  }

  /**
   * @brief Draws the battery icon and percentage on the screen
   * @param renderer Reference to the graphics renderer (const)
   */
  void drawBattery(const GfxRenderer& renderer) const {
    ScreenComponents::drawBattery(
        renderer, renderer.getScreenWidth() - 80, renderer.getScreenHeight() - 30,
        SETTINGS.hideBatteryPercentage != SystemSetting::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);
  }

  /**
   * @brief Pure virtual function to navigate to the selected menu tab
   * Must be implemented by derived classes
   */
  virtual void navigateToSelectedMenu() = 0;

  /**
   * @brief Handles left/right navigation between tabs
   * @param leftPressed True if left button is pressed
   * @param rightPressed True if right button is pressed
   */
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