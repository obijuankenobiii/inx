/**
 * @file Sidebar.cpp
 * @brief Reusable visual sidebar shell for top-level pages.
 */

#include "Sidebar.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstddef>

#include "images/BookAtlas.h"
#include "images/BookmarkIcon.h"
#include "images/FavoriteIcon.h"
#include "images/HighlightIcon.h"
#include "images/StatisticsIcon.h"
#include "system/Fonts.h"
#include "system/UiLayout.h"

namespace navigation {
namespace {

struct Item {
  const char* label;
  const uint8_t* icon;
};

constexpr Item kItems[] = {
    {"Bookmarks", BookmarkIcon}, {"Highlights", HighlightIcon}, {"Favorites", FavoriteIcon},
    {"Statistics", StatisticsIcon}, {"Dictionary", BookAtlas},
};

constexpr size_t kItemCount = sizeof(kItems) / sizeof(kItems[0]);

}  // namespace

void Sidebar::render(const GfxRenderer& renderer, const char* title) {
  const int width = std::min(UiLayout::SIDEBAR_WIDTH_LIMIT, renderer.getScreenWidth() / 2);
  renderer.rectangle.fill(0, 0, width, renderer.getScreenHeight(), false);
  renderer.line.render(width - 1, 0, width - 1, renderer.getScreenHeight(), true);
  renderer.text.render(MONTSERRAT_16_FONT_ID, UiLayout::MENU_LEFT_MARGIN, 25, title ? title : "", true,
                       EpdFontFamily::BOLD);
  renderer.line.render(UiLayout::MENU_LEFT_MARGIN, UiLayout::HEADER_HEIGHT + 14,
                       width - UiLayout::MENU_LEFT_MARGIN, UiLayout::HEADER_HEIGHT + 14, true);

  constexpr int font = MONTSERRAT_12_FONT_ID;
  const int lineHeight = renderer.text.getLineHeight(font);
  for (size_t i = 0; i < kItemCount; ++i) {
    const int rowY = UiLayout::SIDEBAR_LIST_TOP + UiLayout::SIDEBAR_TOP_PADDING +
                     static_cast<int>(i) * (UiLayout::SIDEBAR_ROW_HEIGHT + UiLayout::SIDEBAR_ROW_GAP);
    const int iconX = UiLayout::SIDEBAR_INNER_PADDING + 8;
    const int iconY = rowY + (UiLayout::SIDEBAR_ROW_HEIGHT - UiLayout::SIDEBAR_ICON_SIZE) / 2;
    renderer.bitmap.icon(kItems[i].icon, iconX, iconY, UiLayout::SIDEBAR_ICON_SIZE, UiLayout::SIDEBAR_ICON_SIZE);
    renderer.text.render(font, iconX + UiLayout::SIDEBAR_ICON_SIZE + 16,
                         rowY + (UiLayout::SIDEBAR_ROW_HEIGHT - lineHeight) / 2, kItems[i].label, true);
  }
}

}  // namespace navigation
