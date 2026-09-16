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
constexpr int kDefaultGapY = 10;
constexpr int kX3GapY = 20;
constexpr int kDefaultThumbnailBottomGap = 10;
constexpr int kX3ThumbnailBottomGap = 0;
constexpr int kSelectionBorderThickness = 2;
constexpr int kSelectionPadding = 8;

struct Geometry {
  int cellWidth;
  int cellHeight;
  int startX;
  int startY;
  int gapY;
  int thumbnailBottomGap;
  bool x3Compact;
};

Geometry geometry(const int x, const int y, const int width, const int height, const bool x3Compact) {
  const int contentWidth = std::max(1, width - kMarginX * 2);
  const int contentHeight = std::max(1, height - kMarginY * 2);
  const int gapY = x3Compact ? kX3GapY : kDefaultGapY;
  return {
      std::max(1, (contentWidth - kGapX) / kColumns),
      std::max(1, (contentHeight - gapY) / kRows),
      x + kMarginX,
      y + kMarginY,
      gapY,
      x3Compact ? kX3ThumbnailBottomGap : kDefaultThumbnailBottomGap,
      x3Compact,
  };
}

int cellX(const Geometry& g, const int column) { return g.startX + column * (g.cellWidth + kGapX); }
int cellY(const Geometry& g, const int row) { return g.startY + row * (g.cellHeight + g.gapY); }

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
  const int selectionX = x - kSelectionPadding;
  const int selectionY = y - kSelectionPadding;
  const int selectionWidth = width + kSelectionPadding * 2;
  const int selectionHeight = height + kSelectionPadding * 2;
  support::drawDitherRect(renderer, selectionX, selectionY, selectionWidth, selectionHeight);
  for (int i = 0; i < kSelectionBorderThickness; ++i) {
    renderer.rectangle.render(selectionX - i, selectionY - i, selectionWidth + i * 2, selectionHeight + i * 2, true,
                              false, false);
  }
}

void drawSelectionOutline(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  const int selectionX = x - kSelectionPadding;
  const int selectionY = y - kSelectionPadding;
  const int selectionWidth = width + kSelectionPadding * 2;
  const int selectionHeight = height + kSelectionPadding * 2;
  for (int i = 0; i < kSelectionBorderThickness; ++i) {
    renderer.rectangle.render(selectionX - i, selectionY - i, selectionWidth + i * 2, selectionHeight + i * 2, true,
                              false, false);
  }
}

void renderCard(GfxRenderer& renderer, const RecentBook& book, const Geometry& g, const int slot,
                const int carouselWidth, const int carouselHeight, const bool selected) {
  const int col = slot % kColumns;
  const int row = slot / kColumns;
  const int x = cellX(g, col);
  const int y = cellY(g, row);
  // Keep the established card width. Only the X3 needs its thumbnail height
  // capped because its shorter display has less room for two rows.
  const auto carouselSize = carousel::Carousel::leftThumbnailSize(book, carouselWidth, carouselHeight);
  const int coverWidth = carouselSize.width;
  const int maxHeight = std::max(24, g.cellHeight - g.thumbnailBottomGap);
  const int coverHeight = g.x3Compact ? std::min(carouselSize.height, maxHeight) : carouselSize.height;
  const int rowBottom = y + g.cellHeight - g.thumbnailBottomGap;
  const int coverY = std::max(y, rowBottom - coverHeight);
  const int coverX = x + (g.cellWidth - coverWidth) / 2;

  if (selected) drawSelectionBorder(renderer, coverX, coverY, coverWidth, coverHeight);
  drawThumbnail(renderer, book, coverX, coverY, coverWidth, coverHeight);
  carousel::Carousel::renderProgressTag(renderer, book, coverX, coverY, coverWidth, coverHeight);
}

void renderMockCard(GfxRenderer& renderer, const Geometry& g, const int slot, const char* title, const float progress,
                    const int carouselWidth, const int carouselHeight, const bool selected) {
  const int col = slot % kColumns;
  const int row = slot / kColumns;
  const int x = cellX(g, col);
  const int y = cellY(g, row);
  const RecentBook book("", "", title, "", progress);
  const auto carouselSize = carousel::Carousel::leftThumbnailSize(book, carouselWidth, carouselHeight);
  const int coverWidth = carouselSize.width;
  const int maxHeight = std::max(24, g.cellHeight - g.thumbnailBottomGap);
  const int coverHeight = g.x3Compact ? std::min(carouselSize.height, maxHeight) : carouselSize.height;
  const int rowBottom = y + g.cellHeight - g.thumbnailBottomGap;
  const int coverY = std::max(y, rowBottom - coverHeight);
  const int coverX = x + (g.cellWidth - coverWidth) / 2;
  if (selected) drawSelectionBorder(renderer, coverX, coverY, coverWidth, coverHeight);
  drawPlaceholderThumbnail(renderer, title, coverX, coverY, coverWidth, coverHeight);
  carousel::Carousel::renderProgressTag(renderer, book, coverX, coverY, coverWidth, coverHeight);
}

void drawSelectionOverlay(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                          const int selectedIndex) {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(books.size())) return;

  const Geometry g = geometry(x, y, width, height, renderer.deviceIsX3());
  const int slot = selectedIndex % (kColumns * kRows);
  const int column = slot % kColumns;
  const int row = slot / kColumns;
  const auto carouselSize =
      carousel::Carousel::leftThumbnailSize(books[static_cast<size_t>(selectedIndex)], width, height);
  const int coverWidth = carouselSize.width;
  const int maxHeight = std::max(24, g.cellHeight - g.thumbnailBottomGap);
  const int coverHeight = g.x3Compact ? std::min(carouselSize.height, maxHeight) : carouselSize.height;
  const int cellTop = cellY(g, row);
  const int rowBottom = cellTop + g.cellHeight - g.thumbnailBottomGap;
  const int coverY = std::max(cellTop, rowBottom - coverHeight);
  const int coverX = cellX(g, column) + (g.cellWidth - coverWidth) / 2;

  const int selectionX = coverX - kSelectionPadding;
  const int selectionY = coverY - kSelectionPadding;
  const int selectionWidth = coverWidth + kSelectionPadding * 2;
  const int selectionHeight = coverHeight + kSelectionPadding * 2;
  // The cached frame already contains the thumbnail and progress tag. Paint
  // only the visible dither around the cover so the overlay never covers the
  // image, then redraw the outline.
  support::drawDitherRect(renderer, selectionX, selectionY, selectionWidth, coverY - selectionY);
  support::drawDitherRect(renderer, selectionX, coverY, coverX - selectionX, coverHeight);
  support::drawDitherRect(renderer, coverX + coverWidth, coverY,
                          selectionX + selectionWidth - coverX - coverWidth, coverHeight);
  drawSelectionOutline(renderer, coverX, coverY, coverWidth, coverHeight);
}

}  // namespace

void Grid2x2::render(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                     const int selectedIndex, const bool drawSelection) {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) {
    renderer.text.centered(MONTSERRAT_12_FONT_ID, y + height / 2, "No recent books");
    return;
  }

  const Geometry g = geometry(x, y, width, height, renderer.deviceIsX3());
  const int count = static_cast<int>(books.size());
  const int start = std::max(0, selectedIndex / (kColumns * kRows)) * (kColumns * kRows);
  for (int slot = 0; slot < kColumns * kRows; ++slot) {
    const int index = start + slot;
    if (index >= count) break;
    renderCard(renderer, books[static_cast<size_t>(index)], g, slot, width, height,
               drawSelection && index == selectedIndex);
  }
}

void Grid2x2::renderSelection(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                             const int selectedIndex) {
  drawSelectionOverlay(renderer, x, y, width, height, selectedIndex);
}

void Grid2x2::preview(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  static constexpr const char* titles[] = {"The Great Gatsby", "A Brief History", "Recent Book", "Another Book"};
  static constexpr float progress[] = {0.42f, 0.68f, 0.18f, 0.0f};
  const Geometry g = geometry(x, y, width, height, renderer.deviceIsX3());
  for (int slot = 0; slot < kColumns * kRows; ++slot) {
    renderMockCard(renderer, g, slot, titles[slot], progress[slot], width, height, slot == 0);
  }
}

}  // namespace widget::grid2x2
