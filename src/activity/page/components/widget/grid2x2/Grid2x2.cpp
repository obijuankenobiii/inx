#include "Grid2x2.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>

#include "../WidgetRender.h"
#include "../carousel/Carousel.h"
#include "state/RecentBooks.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"

namespace widget::grid2x2 {
namespace {

constexpr int kColumns = 2;
constexpr int kRows = 2;
constexpr int kMarginX = 20;
constexpr int kMarginY = 8;
constexpr int kGapX = 20;
constexpr int kGapY = 10;
constexpr int kThumbnailBottomGap = 10;
constexpr int kSelectionBorderThickness = 2;

struct Geometry {
  int cellWidth;
  int cellHeight;
  int startX;
  int startY;
};

Geometry geometry(const int x, const int y, const int width, const int height) {
  const int contentWidth = std::max(1, width - kMarginX * 2);
  const int contentHeight = std::max(1, height - kMarginY * 2);
  return {
      std::max(1, (contentWidth - kGapX) / kColumns),
      std::max(1, (contentHeight - kGapY) / kRows),
      x + kMarginX,
      y + kMarginY,
  };
}

int cellX(const Geometry& g, const int column) { return g.startX + column * (g.cellWidth + kGapX); }
int cellY(const Geometry& g, const int row) { return g.startY + row * (g.cellHeight + kGapY); }

void drawThumbnail(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                   const int height) {
  if (width <= 0 || height <= 0) return;
  // Same six-pixel gray shadow and white backing used by the Left carousel.
  renderer.rectangle.fill(x + 6, y + 6, width, height, static_cast<int>(GfxRenderer::FillTone::Gray));
  renderer.rectangle.fill(x, y, width, height, false);
  support::drawThumbnail(renderer, book, x, y, width, height, MONTSERRAT_10_FONT_ID, false);
  renderer.rectangle.render(x, y, width, height, true, SETTINGS.bitmapRoundedCorners != 0,
                            SETTINGS.bitmapRoundedCorners == 2);
}

void drawPlaceholderThumbnail(GfxRenderer& renderer, const char* title, const int x, const int y, const int width,
                              const int height) {
  if (width <= 0 || height <= 0) return;
  renderer.rectangle.fill(x + 6, y + 6, width, height, static_cast<int>(GfxRenderer::FillTone::Gray));
  renderer.rectangle.fill(x, y, width, height, false);
  support::drawPlaceholder(renderer, title, x, y, width, height, MONTSERRAT_10_FONT_ID);
  renderer.rectangle.render(x, y, width, height, true, SETTINGS.bitmapRoundedCorners != 0,
                            SETTINGS.bitmapRoundedCorners == 2);
}

void drawSelectionBorder(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  for (int i = 0; i < kSelectionBorderThickness; ++i) {
    renderer.rectangle.render(x - i, y - i, width + i * 2, height + i * 2, true, false, false);
  }
}

void renderCard(GfxRenderer& renderer, const RecentBook& book, const Geometry& g, const int slot,
                const int carouselWidth, const int carouselHeight, const bool selected) {
  const int col = slot % kColumns;
  const int row = slot / kColumns;
  const int x = cellX(g, col);
  const int y = cellY(g, row);
  // Use the carousel-left dimensions directly. Do not fit or rescale them to
  // this card: this widget is intentionally using the same thumbnail size.
  const auto carouselSize = carousel::Carousel::leftThumbnailSize(book, carouselWidth, carouselHeight);
  const int coverWidth = carouselSize.width;
  const int coverHeight = carouselSize.height;
  const int rowBottom = y + g.cellHeight - kThumbnailBottomGap;
  const int coverY = std::max(y, rowBottom - coverHeight);
  const int coverX = x + (g.cellWidth - coverWidth) / 2;

  drawThumbnail(renderer, book, coverX, coverY, coverWidth, coverHeight);
  carousel::Carousel::renderProgressTag(renderer, book, coverX, coverY, coverWidth, coverHeight);
  if (selected) drawSelectionBorder(renderer, coverX, coverY, coverWidth, coverHeight);
}

void renderMockCard(GfxRenderer& renderer, const Geometry& g, const int slot, const char* title, const float progress,
                    const int carouselWidth, const int carouselHeight, const bool selected) {
  const int col = slot % kColumns;
  const int row = slot / kColumns;
  const int x = cellX(g, col);
  const int y = cellY(g, row);
  const RecentBook book("", "", title, "", progress);
  const auto carouselSize = carousel::Carousel::leftThumbnailSize(book,
                                                                    carouselWidth, carouselHeight);
  const int coverWidth = carouselSize.width;
  const int coverHeight = carouselSize.height;
  const int rowBottom = y + g.cellHeight - kThumbnailBottomGap;
  const int coverY = std::max(y, rowBottom - coverHeight);
  const int coverX = x + (g.cellWidth - coverWidth) / 2;
  drawPlaceholderThumbnail(renderer, title, coverX, coverY, coverWidth, coverHeight);
  carousel::Carousel::renderProgressTag(renderer, book, coverX, coverY, coverWidth, coverHeight);
  if (selected) drawSelectionBorder(renderer, coverX, coverY, coverWidth, coverHeight);
}

}  // namespace

void Grid2x2::render(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                     const int selectedIndex) {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) {
    renderer.text.centered(MONTSERRAT_12_FONT_ID, y + height / 2, "No recent books");
    return;
  }

  const Geometry g = geometry(x, y, width, height);
  const int count = static_cast<int>(books.size());
  const int start = std::max(0, selectedIndex / (kColumns * kRows)) * (kColumns * kRows);
  for (int slot = 0; slot < kColumns * kRows; ++slot) {
    const int index = start + slot;
    if (index >= count) break;
    renderCard(renderer, books[static_cast<size_t>(index)], g, slot, width, height, index == selectedIndex);
  }
}

void Grid2x2::preview(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  static constexpr const char* titles[] = {"The Great Gatsby", "A Brief History", "Recent Book", "Another Book"};
  static constexpr float progress[] = {0.42f, 0.68f, 0.18f, 0.0f};
  const Geometry g = geometry(x, y, width, height);
  for (int slot = 0; slot < kColumns * kRows; ++slot) {
    renderMockCard(renderer, g, slot, titles[slot], progress[slot], width, height, slot == 0);
  }
}

}  // namespace widget::grid2x2
