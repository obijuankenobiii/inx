#include "Grid.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "../WidgetRender.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"

namespace widget::grid {
namespace {

constexpr int kColumns = 3;
constexpr int kRows = 2;
constexpr int kGap = 20;
constexpr int kMarginX = 20;
constexpr int kMarginY = 10;
constexpr int kCoverAspectWidth = 170;
constexpr int kCoverAspectHeight = 250;
constexpr int kGridMetaFont = MONTSERRAT_12_FONT_ID;
constexpr int kCoverToTitleGap = 8;
constexpr int kTitleToPercentageGap = 10;

struct Geometry {
  int startY;
  int contentWidth;
  int cellWidth;
  int cellHeight;
  int lineHeight;
  int visibleRows;
  int startRow;
};

Geometry geometry(const int x, const int y, const int width, const int height, const int count,
                  const int selectedIndex, const int lineHeight) {
  const int startY = y + kMarginY;
  const int contentWidth = std::max(1, width - kMarginX * 2);
  const int cellWidth = std::max(1, (contentWidth - kGap * (kColumns - 1)) / kColumns);
  const int contentHeight = std::max(1, height - kMarginY * 2);
  const int cellHeight = std::max(1, (contentHeight - kGap) / kRows);
  const int visibleRows = kRows;
  const int totalRows = std::max(1, (count + kColumns - 1) / kColumns);
  const int selectedRow = std::max(0, std::min(selectedIndex, std::max(0, count - 1))) / kColumns;
  const int startRow = std::max(0, std::min(selectedRow, std::max(0, totalRows - visibleRows)));
  return {startY, contentWidth, cellWidth, cellHeight, lineHeight, visibleRows, startRow};
}

int cellX(const Geometry& g, const int x, const int column) {
  // Keep the grid's actual content bounds exactly 20 pt from both edges. The
  // remainder is distributed between columns so integer division cannot move
  // the right edge inward or outward by a pixel.
  const int extraSpace = g.contentWidth - g.cellWidth * kColumns;
  return x + kMarginX + column * g.cellWidth +
         (kColumns > 1 ? column * extraSpace / (kColumns - 1) : 0);
}

void coverRect(const Geometry& g, const int x, const int column, const int visualRow, int& coverX, int& coverY,
               int& coverW, int& coverH, int& labelY) {
  const int cardX = cellX(g, x, column);
  const int cellY = g.startY + visualRow * (g.cellHeight + kGap);
  const int labelBlockHeight = kCoverToTitleGap + g.lineHeight + kTitleToPercentageGap + g.lineHeight;
  coverH = std::max(1, std::min(g.cellHeight - labelBlockHeight, g.cellWidth * kCoverAspectHeight / kCoverAspectWidth));
  coverW = std::max(1, coverH * kCoverAspectWidth / kCoverAspectHeight);
  coverX = cardX + (g.cellWidth - coverW) / 2;
  const int cardHeight = coverH + labelBlockHeight;
  coverY = cellY + std::max(0, (g.cellHeight - cardHeight) / 2);
  labelY = coverY + coverH + kCoverToTitleGap;
}

void renderPercentage(const GfxRenderer& renderer, const int x, const int y, const int width, float progress,
                      const bool selected) {
  if (progress < 0.0f || progress > 1.0f) progress = 0.0f;
  char percent[8];
  std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(progress * 100.0f + 0.5f));
  const int font = kGridMetaFont;
  const std::string shown = renderer.text.truncate(font, percent, std::max(1, width), EpdFontFamily::REGULAR);
  renderer.text.render(font, x, y, shown.c_str(), !selected, EpdFontFamily::REGULAR);
}

void renderTitle(const GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                 const bool selected) {
  const int font = kGridMetaFont;
  const std::string title = renderer.text.truncate(font, support::titleFor(book).c_str(), std::max(1, width),
                                                    EpdFontFamily::REGULAR);
  renderer.text.render(font, x, y, title.c_str(), !selected, EpdFontFamily::REGULAR);
}

void drawThumbnailBorder(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  if (width <= 0 || height <= 0) return;
  const int thickness = std::max(1, std::min(width, height) / 50);
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  const bool subtle = SETTINGS.bitmapRoundedCorners == 2;
  for (int i = 0; i < thickness; ++i) {
    renderer.rectangle.render(x - i, y - i, width + i * 2, height + i * 2, true, rounded, subtle);
  }
}

void renderMockCard(const GfxRenderer& renderer, const char* title, const int index, const Geometry& g, const int x) {
  const int column = index % kColumns;
  const int row = index / kColumns;
  int coverX, coverY, coverW, coverH, labelY;
  coverRect(g, x, column, row, coverX, coverY, coverW, coverH, labelY);
  if (index == 0) support::drawDitherRect(renderer, coverX - 8, coverY - 8, coverW + 16, coverH + 16);
  support::drawPlaceholder(renderer, title, coverX, coverY, coverW, coverH, MONTSERRAT_10_FONT_ID);
  drawThumbnailBorder(renderer, coverX, coverY, coverW, coverH);
  renderTitle(renderer, RecentBook("", "", title, "", 0.0f), coverX, labelY, coverW, false);
  renderPercentage(renderer, coverX, labelY + renderer.text.getLineHeight(kGridMetaFont) + kTitleToPercentageGap,
                   coverW, index == 1 ? 0.118f : index == 2 ? 0.0f : 0.42f, false);
}

}  // namespace

void Grid::render(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                  const int selectedIndex) {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) {
    renderer.text.centered(MONTSERRAT_12_FONT_ID, y + height / 2, "No recent books");
    return;
  }
  const int count = std::min(static_cast<int>(books.size()), std::max(1, static_cast<int>(SETTINGS.recentVisibleCount)));
  const Geometry g = geometry(x, y, width, height, count, selectedIndex,
                              renderer.text.getLineHeight(kGridMetaFont));
  for (int row = 0; row < g.visibleRows; ++row) {
    for (int column = 0; column < kColumns; ++column) {
      const int index = (g.startRow + row) * kColumns + column;
      if (index >= count) break;
      int coverX, coverY, coverW, coverH, labelY;
      coverRect(g, x, column, row, coverX, coverY, coverW, coverH, labelY);
      const bool selected = index == selectedIndex;
      const int percentageY = labelY + renderer.text.getLineHeight(kGridMetaFont) + kTitleToPercentageGap;
      if (selected) {
        const int selectionTop = coverY - 8;
        const int selectionBottom = percentageY + renderer.text.getLineHeight(kGridMetaFont) + 8;
        support::drawDitherRect(renderer, coverX - 8, selectionTop, coverW + 16,
                                selectionBottom - selectionTop);
      }
      support::drawThumbnail(renderer, books[static_cast<size_t>(index)], coverX, coverY, coverW, coverH,
                              MONTSERRAT_10_FONT_ID, selected);
      drawThumbnailBorder(renderer, coverX, coverY, coverW, coverH);
      renderTitle(renderer, books[static_cast<size_t>(index)], coverX, labelY, coverW, false);
      renderPercentage(renderer, coverX, percentageY, coverW, books[static_cast<size_t>(index)].progress, false);
    }
  }
}

void Grid::preview(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  static constexpr const char* titles[] = {"As a Man Thinketh", "Boyfriend", "Northern Tales", "The Beautiful Mind",
                                           "The Handbook", "The Modern Reader"};
  const Geometry g = geometry(x, y, width, height, kRows * kColumns, 0,
                              renderer.text.getLineHeight(kGridMetaFont));
  for (int index = 0; index < kRows * kColumns; ++index) renderMockCard(renderer, titles[index], index, g, x);
}

}  // namespace widget::grid
