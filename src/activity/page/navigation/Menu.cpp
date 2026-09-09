/**
 * @file Menu.cpp
 * @brief Visual-only bottom navigation chrome for the page shell.
 */

#include "Menu.h"

#include <GfxRenderer.h>

#include <cstdint>
#include <cstring>

#include "images/Library.h"
#include "images/Recent.h"
#include "images/Search.h"
#include "images/Setting.h"
#include "images/Sync.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"
#include "system/UiLayout.h"

namespace navigation {
namespace {

}  // namespace

Menu::Menu(GfxRenderer& renderer) : menuRenderer(renderer) {}

void Menu::drawBattery() const {
  const bool showPercentage = SETTINGS.hideBatteryPercentage != SystemSetting::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int textWidth = showPercentage ? menuRenderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, "100%") : 0;
  const int width = showPercentage ? ScreenComponents::BATTERY_ICON_WIDTH + ScreenComponents::BATTERY_TEXT_GAP + textWidth
                                   : ScreenComponents::BATTERY_ICON_WIDTH;
  const int x = menuRenderer.getScreenWidth() - UiLayout::SHELL_BATTERY_RIGHT_MARGIN - width;
  const int y = UiLayout::MENU_TOP_PADDING + (UiLayout::MENU_ICON_SIZE - 18) / 2;
  ScreenComponents::drawBattery(menuRenderer, x, y, showPercentage);
}

void Menu::render() const {
  title();
  drawBattery();

  const int screenWidth = menuRenderer.getScreenWidth();
  const int screenHeight = menuRenderer.getScreenHeight();
  const int left = UiLayout::MENU_LEFT_MARGIN + UiLayout::MENU_ICON_SIZE / 2;
  const int right = screenWidth - UiLayout::MENU_LEFT_MARGIN - UiLayout::MENU_ICON_SIZE / 2;
  const int step = (right - left) / (UiLayout::MENU_ITEM_COUNT - 1);
  const int centerY = screenHeight - UiLayout::MENU_BOTTOM_PADDING - UiLayout::MENU_BOTTOM_SIZE / 2;
  const int iconY = centerY - UiLayout::MENU_ICON_SIZE / 2;

  const uint8_t* icons[] = {Recent, Library, Setting, Sync};
  for (int i = 0; i < UiLayout::MENU_ITEM_COUNT - 1; ++i) {
    const int iconX = left + i * step - UiLayout::MENU_ICON_SIZE / 2;
    menuRenderer.bitmap.icon(icons[i], iconX, iconY, UiLayout::MENU_ICON_SIZE, UiLayout::MENU_ICON_SIZE);

    const char* page = name();
    const bool active = page && ((i == 0 && std::strcmp(page, "Home") == 0) ||
                                 (i == 1 && std::strcmp(page, "Library") == 0) ||
                                 (i == 2 && std::strcmp(page, "Settings") == 0) ||
                                 (i == 3 && (std::strcmp(page, "Sync") == 0 ||
                                             std::strcmp(page, "Device Management") == 0 ||
                                             std::strcmp(page, "Network Settings") == 0)));
    if (active) {
      constexpr int underlineWidth = 36;
      constexpr int underlineHeight = 4;
      menuRenderer.rectangle.fill(left + i * step - underlineWidth / 2, iconY + UiLayout::MENU_ICON_SIZE + 5,
                                  underlineWidth, underlineHeight, true);
    }
  }

  const int searchCenterX = left + (UiLayout::MENU_ITEM_COUNT - 1) * step;
  constexpr int searchRadius = UiLayout::MENU_BOTTOM_SIZE / 2;
  for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
    for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
      if (dx * dx + dy * dy <= searchRadius * searchRadius) {
        menuRenderer.drawPixel(searchCenterX + dx, centerY + dy);
      }
    }
  }
  menuRenderer.bitmap.icon(Search, searchCenterX - UiLayout::MENU_ICON_SIZE / 2, iconY + 5,
                            UiLayout::MENU_ICON_SIZE, UiLayout::MENU_ICON_SIZE,
                            BitmapRender::Orientation::None, true);
}

}  // namespace navigation
