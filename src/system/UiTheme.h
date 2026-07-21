#pragma once

#include <cstdint>

class GfxRenderer;

class UiTheme {
 public:
  enum class MainTabPlacement : uint8_t { Top, Bottom };

  static constexpr int DRAWER_LIST_ITEM_HEIGHT = 66;
  static constexpr int MAIN_TAB_BAR_HEIGHT = DRAWER_LIST_ITEM_HEIGHT;
  static constexpr int BOTTOM_TAB_BAR_HEIGHT = DRAWER_LIST_ITEM_HEIGHT;
  static constexpr int BOTTOM_CONTENT_PADDING = 5;
  static constexpr int TOP_STATUS_HEIGHT = 36;
  static constexpr int DRAWER_HEADER_HEIGHT = DRAWER_LIST_ITEM_HEIGHT;
  static constexpr int DRAWER_PAGE_HEADER_HEIGHT = 79;
  static constexpr int DRAWER_LIST_BOTTOM_PADDING = 12;

  static UiTheme& getInstance();

  MainTabPlacement mainTabPlacement() const;
  bool mainTabsAtBottom() const { return mainTabPlacement() == MainTabPlacement::Bottom; }

  int mainTabBarHeight() const;
  int mainTabBarY(const GfxRenderer& renderer) const;
  int mainContentTop() const;
  int mainContentBottom(const GfxRenderer& renderer) const;

  void drawMainTabBar(const GfxRenderer& renderer, int selectedIndex, bool showBatteryPercentage) const;
  int drawPageHeader(const GfxRenderer& renderer, const char* title, int startY = 0, const char* trailingText = nullptr,
                     int titleX = 20) const;
  void drawButtonHints(const GfxRenderer& renderer, int fontId, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const;

 private:
  UiTheme() = default;
};

#define INX_THEME UiTheme::getInstance()
