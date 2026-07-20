#include "system/UiTheme.h"

#include <GfxRenderer.h>

#include "images/Library.h"
#include "images/Recent.h"
#include "images/Setting.h"
#include "images/Stats.h"
#include "images/Sync.h"
#include "state/SystemSetting.h"
#include "system/ScreenComponents.h"

namespace {
constexpr int kMainTabCount = 5;
constexpr int kMainTabIconSize = 38;
constexpr int kBottomTabIconSize = 30;
constexpr int kSelectedBorderWidth = 38;
constexpr int kSelectedBorderHeight = 5;
constexpr int kBottomTabIconLift = 5;
constexpr int kBottomSelectedCircleSize = 49;

void fillCircle(const GfxRenderer& renderer, const int cx, const int cy, const int radius, const bool state) {
  const int r2 = radius * radius;
  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      if (x * x + y * y <= r2) {
        renderer.drawPixel(cx + x, cy + y, state);
      }
    }
  }
}
}  // namespace

UiTheme& UiTheme::getInstance() {
  static UiTheme instance;
  return instance;
}

UiTheme::MainTabPlacement UiTheme::mainTabPlacement() const {
  return SETTINGS.uiTheme == SystemSetting::UI_THEME_BOTTOM_TABS ? MainTabPlacement::Bottom : MainTabPlacement::Top;
}

int UiTheme::mainTabBarHeight() const { return mainTabsAtBottom() ? BOTTOM_TAB_BAR_HEIGHT : MAIN_TAB_BAR_HEIGHT; }

int UiTheme::mainTabBarY(const GfxRenderer& renderer) const {
  return mainTabsAtBottom() ? renderer.getScreenHeight() - BOTTOM_TAB_BAR_HEIGHT : 0;
}

int UiTheme::mainContentTop() const { return mainTabsAtBottom() ? TOP_STATUS_HEIGHT : MAIN_TAB_BAR_HEIGHT; }

int UiTheme::mainContentBottom(const GfxRenderer& renderer) const {
  return mainTabsAtBottom() ? renderer.getScreenHeight() - BOTTOM_TAB_BAR_HEIGHT - BOTTOM_CONTENT_PADDING
                            : renderer.getScreenHeight();
}

void UiTheme::drawMainTabBar(const GfxRenderer& renderer, const int selectedIndex,
                             const bool showBatteryPercentage) const {
  const int screenWidth = renderer.getScreenWidth();
  const int tabY = mainTabBarY(renderer);
  const int tabH = mainTabBarHeight();
  const int tabButtonWidth = (screenWidth / kMainTabCount) - 1;

  if (mainTabsAtBottom()) {
    renderer.rectangle.fill(0, tabY, screenWidth, tabH, static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.line.render(0, tabY, screenWidth, tabY, true, LineRender::Style::Dotted);
  }

  for (int i = 0; i < kMainTabCount; ++i) {
    const int buttonX = i * tabButtonWidth;
    const bool isSelected = selectedIndex == i;
    const int iconSize = mainTabsAtBottom() ? kBottomTabIconSize : kMainTabIconSize;
    const int iconX = buttonX + (tabButtonWidth - iconSize) / 2;
    const int iconY = tabY + (tabH - iconSize) / 2 + (mainTabsAtBottom() ? 2 - kBottomTabIconLift : 5);

    if (mainTabsAtBottom() && isSelected) {
      fillCircle(renderer, iconX + iconSize / 2, iconY + iconSize / 2, kBottomSelectedCircleSize / 2, true);
    }

    const bool invertIcon = mainTabsAtBottom() && isSelected;
    auto drawTabIcon = [&](const uint8_t* icon) {
      if (mainTabsAtBottom()) {
        renderer.bitmap.iconScaled(icon, iconX, iconY, kMainTabIconSize, kMainTabIconSize, iconSize, iconSize,
                                   BitmapRender::Orientation::None, invertIcon);
      } else {
        renderer.bitmap.icon(icon, iconX, iconY, iconSize, iconSize, BitmapRender::Orientation::None, invertIcon);
      }
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

    if (isSelected && !mainTabsAtBottom()) {
      const int selectedY = tabY + tabH - 2;
      const int selectedX = iconX + (kMainTabIconSize - kSelectedBorderWidth) / 2;
      renderer.rectangle.fill(selectedX, selectedY, kSelectedBorderWidth, kSelectedBorderHeight,
                              static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    if (!mainTabsAtBottom()) {
      renderer.line.render(buttonX, tabY + tabH, buttonX + tabButtonWidth, tabY + tabH);
    }
  }

  if (mainTabsAtBottom()) {
    ScreenComponents::drawBattery(renderer, renderer.getScreenWidth() - 80, 10, showBatteryPercentage);
    renderer.line.render(0, TOP_STATUS_HEIGHT, renderer.getScreenWidth(), TOP_STATUS_HEIGHT);
  } else {
    ScreenComponents::drawBattery(renderer, renderer.getScreenWidth() - 80, renderer.getScreenHeight() - 30,
                                  showBatteryPercentage);
  }
}

void UiTheme::drawButtonHints(const GfxRenderer& renderer, const int fontId, const char* btn1, const char* btn2,
                              const char* btn3, const char* btn4) const {
  if (!mainTabsAtBottom()) {
    renderer.ui.buttonHints(fontId, btn1, btn2, btn3, btn4);
  }
}
