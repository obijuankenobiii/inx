#pragma once

/**
 * @file Menu.h
 * @brief Visual-only bottom navigation chrome for the page shell.
 */

class GfxRenderer;

#include "system/UiLayout.h"
#include "system/MenuNav.h"

namespace navigation {

/**
 * Bottom menu chrome used by Page and SubPage.
 *
 * This is intentionally presentation-only for the first UI migration step.
 * Input handling and page navigation will be added after the shell layout is
 * stable.
 */
class Menu {
 public:
  // Keep the inx-pro geometry names available while the page shell migrates.
  static constexpr int topPadding = UiLayout::MENU_TOP_PADDING;
  static constexpr int iconSize = UiLayout::MENU_ICON_SIZE;
  static constexpr int leftMargin = UiLayout::MENU_LEFT_MARGIN;
  static constexpr int bottomPadding = UiLayout::MENU_BOTTOM_PADDING;
  static constexpr int bottomSize = UiLayout::MENU_BOTTOM_SIZE;
  static constexpr int bottomHeight = UiLayout::MENU_BOTTOM_HEIGHT;
  static constexpr int height = UiLayout::MENU_HEIGHT;

  explicit Menu(GfxRenderer& renderer);
  virtual ~Menu() = default;

  virtual const char* name() const { return ""; }
  virtual void title() const {}
  virtual void center() const {}
  virtual bool showBattery() const { return true; }

 protected:
  static constexpr int tabCount = UiLayout::MENU_ITEM_COUNT;
  int tabSelectorIndex = 0;

  MappedInputManager::Button tabPrevButton() const { return MenuNav::tabPrev(); }
  MappedInputManager::Button tabNextButton() const { return MenuNav::tabNext(); }
  MappedInputManager::Button itemPrevButton() const { return MenuNav::itemPrev(); }
  MappedInputManager::Button itemNextButton() const { return MenuNav::itemNext(); }

  void handleTabNavigation(bool leftPressed, bool rightPressed) {
    if (leftPressed) {
      tabSelectorIndex = (tabSelectorIndex - 1 + tabCount) % tabCount;
      navigateToSelectedMenu();
    }
    if (rightPressed) {
      tabSelectorIndex = (tabSelectorIndex + 1) % tabCount;
      navigateToSelectedMenu();
    }
  }

  virtual void navigateToSelectedMenu() {}

 void render() const;

 private:
  void drawBattery() const;
  GfxRenderer& menuRenderer;
};

}  // namespace navigation
