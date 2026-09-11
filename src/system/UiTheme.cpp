#include "system/UiTheme.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>

#include "HalGPIO.h"
#include "images/Library.h"
#include "images/Recent.h"
#include "images/Setting.h"
#include "images/Stats.h"
#include "images/Sync.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"

namespace {
int x3ChromeAdjustment() { return gpio.deviceIsX3() ? 2 : 0; }

void drawMenuClockAndBattery(const GfxRenderer& renderer, const int top, const bool showBatteryPercentage) {
  const int batteryX = renderer.getScreenWidth() - UiLayout::MENU_BATTERY_RIGHT_MARGIN;
  ScreenComponents::drawMenuClockAndBattery(renderer, batteryX, top, showBatteryPercentage);
}

void drawLeftClockAndRightBattery(const GfxRenderer& renderer, const int top, const bool showBatteryPercentage) {
  ScreenComponents::drawMenuClock(renderer, UiLayout::BOTTOM_MENU_CLOCK_LEFT_MARGIN, top);
  ScreenComponents::drawBattery(renderer, renderer.getScreenWidth() - UiLayout::MENU_BATTERY_RIGHT_MARGIN, top,
                                showBatteryPercentage);
}
}  // namespace

UiTheme& UiTheme::getInstance() {
  static UiTheme instance;
  return instance;
}

UiTheme::MainTabPlacement UiTheme::mainTabPlacement() const {
  return SETTINGS.uiTheme == SystemSetting::UI_THEME_BOTTOM_TABS ? MainTabPlacement::Bottom : MainTabPlacement::Top;
}

int UiTheme::mainHeaderHeight() const { return MAIN_TAB_BAR_HEIGHT - x3ChromeAdjustment(); }

int UiTheme::mainTabBarHeight() const {
  const int baseHeight = mainTabsAtBottom() ? BOTTOM_TAB_BAR_HEIGHT : MAIN_TAB_BAR_HEIGHT;
  return baseHeight - x3ChromeAdjustment();
}

int UiTheme::mainTabBarY(const GfxRenderer& renderer) const {
  return mainTabsAtBottom() ? renderer.getScreenHeight() - mainTabBarHeight() : 0;
}

int UiTheme::mainContentTop() const { return mainTabsAtBottom() ? TOP_STATUS_HEIGHT : mainHeaderHeight(); }

int UiTheme::mainContentBottom(const GfxRenderer& renderer) const {
  return mainTabsAtBottom() ? renderer.getScreenHeight() - mainTabBarHeight() - BOTTOM_CONTENT_PADDING
                            : renderer.getScreenHeight();
}

int UiTheme::drawerHeaderHeight() const { return DRAWER_HEADER_HEIGHT - x3ChromeAdjustment(); }

int UiTheme::drawerPageHeaderHeight() const { return DRAWER_PAGE_HEADER_HEIGHT - x3ChromeAdjustment(); }

void UiTheme::drawMainTabBar(const GfxRenderer& renderer, const int selectedIndex,
                             const bool showBatteryPercentage) const {
  const int screenWidth = renderer.getScreenWidth();
  const int tabY = mainTabBarY(renderer);
  const int tabH = mainTabBarHeight();
  const int tabButtonWidth = (screenWidth / UiLayout::MAIN_TAB_COUNT) - 1;

  if (mainTabsAtBottom()) {
    renderer.rectangle.fill(0, tabY, screenWidth, tabH, static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.line.render(0, tabY, screenWidth, tabY);
  }

  for (int i = 0; i < UiLayout::MAIN_TAB_COUNT; ++i) {
    const int buttonX = i * tabButtonWidth;
    const bool isSelected = selectedIndex == i;
    const int iconX = buttonX + (tabButtonWidth - UiLayout::MAIN_TAB_ICON_SIZE) / 2;
    const int iconY = tabY + (tabH - UiLayout::MAIN_TAB_ICON_SIZE) / 2 +
                      (mainTabsAtBottom() ? UiLayout::BOTTOM_TAB_ICON_NUDGE_Y : 5);

    auto drawTabIcon = [&](const uint8_t* icon) {
      renderer.bitmap.icon(icon, iconX, iconY, UiLayout::MAIN_TAB_ICON_SIZE, UiLayout::MAIN_TAB_ICON_SIZE,
                           BitmapRender::Orientation::None,
                           false);
    };
    switch (i) {
      case 0:
        drawTabIcon(Recent);
        break;
      case 1:
        drawTabIcon(Library);
        break;
      case 2:
        drawTabIcon(Setting);
        break;
      case 3:
        drawTabIcon(Sync);
        break;
      case 4:
        drawTabIcon(Stats);
        break;
    }

    if (isSelected) {
      const int selectedY = mainTabsAtBottom() ? tabY : tabY + tabH - 2;
      const int selectedX = iconX + (UiLayout::MAIN_TAB_ICON_SIZE - UiLayout::MAIN_TAB_SELECTED_BORDER_WIDTH) / 2;
      renderer.rectangle.fill(selectedX, selectedY, UiLayout::MAIN_TAB_SELECTED_BORDER_WIDTH,
                              UiLayout::MAIN_TAB_SELECTED_BORDER_HEIGHT,
                              static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    renderer.line.render(buttonX, tabY + tabH, buttonX + tabButtonWidth, tabY + tabH);
  }

  if (mainTabsAtBottom()) {
    drawLeftClockAndRightBattery(renderer, 10, showBatteryPercentage);
    renderer.line.render(0, UiLayout::CONTENT_TOP, renderer.getScreenWidth(), UiLayout::CONTENT_TOP);
  } else {
    drawMenuClockAndBattery(renderer, renderer.getScreenHeight() - 30, showBatteryPercentage);
  }
}

int UiTheme::drawPageHeader(const GfxRenderer& renderer, const char* title, const int startY, const char* trailingText,
                            const int titleX) const {
  (void)startY;
  (void)titleX;
  return ScreenComponents::drawSubPageHeader(renderer, title, trailingText);
}

void UiTheme::drawButtonHints(const GfxRenderer& renderer, const int fontId, const char* btn1, const char* btn2,
                              const char* btn3, const char* btn4) const {
  // Hide-button-hints is enforced inside UiRender::buttonHints (global policy).
  // Hub chrome: when main tabs sit on the bottom row, they replace the hint bar.
  if (!mainTabsAtBottom()) {
    renderer.ui.buttonHints(fontId, btn1, btn2, btn3, btn4);
  }
}
