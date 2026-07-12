/**
 * @file Menu.h
 * @brief Public interface and types for Menu.
 */

#ifndef BASE_TAB_ACTIVITY_H
#define BASE_TAB_ACTIVITY_H

#include <GfxRenderer.h>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/ScreenComponents.h"
#include "system/UiTheme.h"

class Menu {
 protected:
  static constexpr int TAB_BAR_HEIGHT = 65;
  static constexpr int TAB_COUNT = 5;
  int tabSelectorIndex = 0;

  /**
   * @brief Default constructor
   */
  Menu() = default;
  /**
   * @brief Default destructor
   */
  virtual ~Menu() = default;

  // Main-menu navigation axis (see MenuNav). Front: Left/Right tabs, Up/Down items; side: swapped.
  /**
   * @brief Returns the button mapped to the previous tab
   */
  MappedInputManager::Button tabPrevButton() const { return MenuNav::tabPrev(); }
  /**
   * @brief Returns the button mapped to the next tab
   */
  MappedInputManager::Button tabNextButton() const { return MenuNav::tabNext(); }
  /**
   * @brief Returns the button mapped to the previous item
   */
  MappedInputManager::Button itemPrevButton() const { return MenuNav::itemPrev(); }
  /**
   * @brief Returns the button mapped to the next item
   */
  MappedInputManager::Button itemNextButton() const { return MenuNav::itemNext(); }

  /**
   * @brief Renders the tab bar with icons and selection indicator
   * @param renderer Reference to the graphics renderer (const)
   */
  void renderTabBar(const GfxRenderer& renderer) const {
    INX_THEME.drawMainTabBar(renderer, tabSelectorIndex,
                             SETTINGS.hideBatteryPercentage != SystemSetting::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);
  }

  void renderButtonHints(const GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                         const char* btn4) const {
    INX_THEME.drawButtonHints(renderer, ATKINSON_HYPERLEGIBLE_10_FONT_ID, btn1, btn2, btn3, btn4);
    if (INX_THEME.mainTabsAtBottom()) {
      renderTabBar(renderer);
    }
  }

  int mainContentTop() const { return INX_THEME.mainContentTop(); }
  int mainContentBottom(const GfxRenderer& renderer) const { return INX_THEME.mainContentBottom(renderer); }
  int mainHeaderDividerY() const { return mainContentTop() + TAB_BAR_HEIGHT; }

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
