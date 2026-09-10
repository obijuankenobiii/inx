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

constexpr int kInnerPadding = UiLayout::SIDEBAR_INNER_PADDING;
constexpr int kTopPadding = UiLayout::SIDEBAR_TOP_PADDING;

void renderFrame(const GfxRenderer& renderer, const char* title) {
  const int width = std::min(UiLayout::SIDEBAR_WIDTH_LIMIT, renderer.getScreenWidth() / 2);
  renderer.rectangle.fill(0, 0, width, renderer.getScreenHeight(), false);
  renderer.line.render(width - 1, 0, width - 1, renderer.getScreenHeight(), true);
  renderer.text.render(MONTSERRAT_16_FONT_ID, UiLayout::MENU_LEFT_MARGIN, 25, title ? title : "", true,
                       EpdFontFamily::BOLD);
  renderer.line.render(UiLayout::MENU_LEFT_MARGIN, UiLayout::HEADER_HEIGHT + 14,
                       width - UiLayout::MENU_LEFT_MARGIN, UiLayout::HEADER_HEIGHT + 14, true);
}

void renderTextList(const GfxRenderer& renderer, const char* const* labels, const size_t count,
                    const int selected = -1) {
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  const int x = kInnerPadding + 16;
  const int drawerWidth = std::min(UiLayout::SIDEBAR_WIDTH_LIMIT, renderer.getScreenWidth() / 2);
  for (size_t i = 0; i < count; ++i) {
    const int rowY = UiLayout::SIDEBAR_LIST_TOP + kTopPadding +
                     static_cast<int>(i) * (UiLayout::SIDEBAR_ROW_HEIGHT + UiLayout::SIDEBAR_ROW_GAP);
    const bool isSelected = static_cast<int>(i) == selected;
    if (isSelected) {
      renderer.rectangle.fill(kInnerPadding, rowY, drawerWidth - kInnerPadding * 2,
                              UiLayout::SIDEBAR_ROW_HEIGHT, true, true, true);
    }
    renderer.text.render(font, x, rowY + (UiLayout::SIDEBAR_ROW_HEIGHT - lineHeight) / 2,
                         labels[i] ? labels[i] : "", !isSelected);
  }
}

}  // namespace

int Sidebar::width(const GfxRenderer& renderer) {
  return std::min(UiLayout::SIDEBAR_WIDTH_LIMIT, renderer.getScreenWidth() / 2);
}

void Sidebar::render(const GfxRenderer& renderer, const char* title, const int selected) {
  renderFrame(renderer, title);

  const int drawerWidth = Sidebar::width(renderer);
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  for (size_t i = 0; i < kItemCount; ++i) {
    const int rowY = UiLayout::SIDEBAR_LIST_TOP + UiLayout::SIDEBAR_TOP_PADDING +
                     static_cast<int>(i) * (UiLayout::SIDEBAR_ROW_HEIGHT + UiLayout::SIDEBAR_ROW_GAP);
    const bool active = static_cast<int>(i) == selected;
    if (active) {
      renderer.rectangle.fill(UiLayout::SIDEBAR_INNER_PADDING, rowY,
                              drawerWidth - UiLayout::SIDEBAR_INNER_PADDING * 2, UiLayout::SIDEBAR_ROW_HEIGHT, true,
                              true, true);
    }
    const int iconX = UiLayout::SIDEBAR_INNER_PADDING + 8;
    const int iconY = rowY + (UiLayout::SIDEBAR_ROW_HEIGHT - UiLayout::SIDEBAR_ICON_SIZE) / 2;
    renderer.bitmap.icon(kItems[i].icon, iconX, iconY, UiLayout::SIDEBAR_ICON_SIZE, UiLayout::SIDEBAR_ICON_SIZE,
                         BitmapRender::Orientation::None, active);
    renderer.text.render(font, iconX + UiLayout::SIDEBAR_ICON_SIZE + 16,
                         rowY + (UiLayout::SIDEBAR_ROW_HEIGHT - lineHeight) / 2, kItems[i].label, !active);
  }
}

void Sidebar::renderLibrary(const GfxRenderer& renderer, const bool allBooksMode, const int selected) {
  const char* labels[] = {allBooksMode ? "Folders" : "All books", "Favorites", "Reading", "Finished", "Author"};
  renderFrame(renderer, "Library");
  renderTextList(renderer, labels, sizeof(labels) / sizeof(labels[0]), selected);
}

int Sidebar::hitTest(const GfxRenderer& renderer, const int tapX, const int tapY, const size_t count) {
  const int width = std::min(UiLayout::SIDEBAR_WIDTH_LIMIT, renderer.getScreenWidth() / 2);
  const int contentTop = UiLayout::SIDEBAR_LIST_TOP + kTopPadding;
  const int contentBottom = renderer.getScreenHeight() - kInnerPadding;
  if (tapX < kInnerPadding || tapX >= width - kInnerPadding || tapY < contentTop || tapY >= contentBottom) {
    return -1;
  }
  const int index = (tapY - contentTop) / (UiLayout::SIDEBAR_ROW_HEIGHT + UiLayout::SIDEBAR_ROW_GAP);
  if (index < 0 || static_cast<size_t>(index) >= count) return -1;
  const int rowY = contentTop + index * (UiLayout::SIDEBAR_ROW_HEIGHT + UiLayout::SIDEBAR_ROW_GAP);
  return tapY < rowY + UiLayout::SIDEBAR_ROW_HEIGHT ? index : -1;
}

}  // namespace navigation
