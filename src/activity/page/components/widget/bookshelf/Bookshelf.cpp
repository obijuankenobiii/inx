#include "Bookshelf.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <array>

#include "../WidgetRender.h"
#include "state/RecentBooks.h"
#include "system/Fonts.h"

namespace widget::bookshelf {
namespace {

constexpr int kTopBookSlots = 4;
constexpr int kBottomBookSlots = 4;
constexpr int kShelfCount = 3;
constexpr int kOuterMargin = 20;
constexpr int kShelfFrontHeight = 10;
constexpr int kShelfGap = 4;
constexpr int kBookGap = 15;
constexpr int kTopPadding = 4;
constexpr int kBottomShelfLift = 20;

struct Geometry {
  int left;
  int width;
  int shelfWidth;
  int shelfY[kShelfCount];
  int maxBookHeight;
};

int shelfBookSlots(const int shelf) {
  (void)shelf;
  return kBottomBookSlots;
}

Geometry geometry(const int x, const int y, const int width, const int height) {
  const int innerWidth = std::max(1, width - kOuterMargin * 2);
  const int usableTop = y + kTopPadding;
  const int bottomShelfY = y + height - kShelfFrontHeight - 2 - kBottomShelfLift;
  const int shelfSpan = std::max(60, bottomShelfY - usableTop);
  constexpr int topMargin = 4;
  constexpr int bottomMargin = 4;
  const int shelfSpacing = kShelfGap + kShelfFrontHeight;
  const int maxBookHeight = std::max(
      24, (shelfSpan - topMargin - bottomMargin - shelfSpacing * (kShelfCount - 1)) / kShelfCount);
  int shelfY[kShelfCount] = {};
  shelfY[0] = usableTop + topMargin + maxBookHeight;
  for (int shelf = 1; shelf < kShelfCount - 1; ++shelf) {
    shelfY[shelf] = shelfY[shelf - 1] + shelfSpacing + maxBookHeight;
  }
  shelfY[kShelfCount - 1] = bottomShelfY;
  return {x + kOuterMargin, innerWidth, innerWidth, {shelfY[0], shelfY[1], shelfY[2]}, maxBookHeight};
}

int bookHeight(const Geometry& g, const int shelf, const int slot) {
  // Deliberately varied heights make the rows read as real books instead of a
  // rigid grid, while remaining deterministic so selection never shifts the layout.
  static constexpr int kHeightPercent[kShelfCount][kBottomBookSlots] = {
      {70, 78, 74, 82},
      {76, 70, 84, 72},
      {72, 68, 80, 74},
  };
  return std::max(24, g.maxBookHeight * kHeightPercent[shelf][slot] / 100);
}

int bookIndexForSlot(const int shelf, const int slot, const int count) {
  const int index = shelf * kBottomBookSlots + slot;
  return index < count ? index : -1;
}

void drawShelf(const GfxRenderer& renderer, const Geometry& g, const int shelfY) {
  const int shelfX = g.left;
  const int shelfW = g.shelfWidth;
  renderer.rectangle.fill(shelfX + 4, shelfY + 4, shelfW, kShelfFrontHeight,
                          static_cast<int>(GfxRenderer::FillTone::Gray));
  renderer.rectangle.fill(shelfX, shelfY, shelfW, kShelfFrontHeight,
                          static_cast<int>(GfxRenderer::FillTone::Paper));
  renderer.rectangle.render(shelfX, shelfY, shelfW, kShelfFrontHeight, true);
  renderer.line.render(shelfX + 3, shelfY + 2, shelfX + shelfW - 4, shelfY + 2);
}

void drawSelection(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  const int pad = 6;
  support::drawDitherRect(renderer, x - pad, y - pad, width + pad * 2, height + pad * 2);
  renderer.rectangle.render(x - pad, y - pad, width + pad * 2, height + pad * 2, true, false, false);
}

void drawBook(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
              const int height, const bool selected) {
  if (width <= 0 || height <= 0) return;
  if (selected) drawSelection(renderer, x, y, width, height);

  // A small offset shadow and crisp border give the covers a physical depth on
  // the shelf while keeping the source thumbnail fully visible.
  renderer.rectangle.fill(x + 4, y + 4, width, height, static_cast<int>(GfxRenderer::FillTone::Gray));
  renderer.rectangle.fill(x, y, width, height, false);
  support::drawThumbnail(renderer, book, x, y, width, height, MONTSERRAT_10_FONT_ID, false, true, true, true);
  renderer.rectangle.render(x, y, width, height, true, false, false);
}

void renderBookshelf(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                     const RecentBook* books, const int count, const int selectedIndex, const bool drawSelection) {
  if (width <= 0 || height <= 0) return;
  const Geometry g = geometry(x, y, width, height);

  for (int shelf = 0; shelf < kShelfCount; ++shelf) {
    const int slots = shelfBookSlots(shelf);
    std::array<int, kBottomBookSlots> indices{};
    std::array<int, kBottomBookSlots> widths{};
    std::array<int, kBottomBookSlots> heights{};
    int rowWidth = 0;
    int visibleBooks = 0;
    for (int slot = 0; slot < slots; ++slot) {
      const int index = bookIndexForSlot(shelf, slot, count);
      indices[static_cast<size_t>(slot)] = index;
      if (index < 0) continue;
      const int coverH = std::min(g.maxBookHeight, bookHeight(g, shelf, slot) + 10);
      const int coverW = std::max(18, coverH * 2 / 3);
      widths[static_cast<size_t>(slot)] = coverW;
      heights[static_cast<size_t>(slot)] = coverH;
      rowWidth += coverW;
      ++visibleBooks;
    }
    if (visibleBooks > 1) rowWidth += kBookGap * (visibleBooks - 1);
    const int gapWidth = visibleBooks > 1 ? kBookGap * (visibleBooks - 1) : 0;
    const int maxBookRowWidth = std::max(1, g.width - 12);
    if (rowWidth > maxBookRowWidth && visibleBooks > 0) {
      const int targetCoverWidth = std::max(1, maxBookRowWidth - gapWidth);
      const int sourceCoverWidth = std::max(1, rowWidth - gapWidth);
      for (int slot = 0; slot < slots; ++slot) {
        if (indices[static_cast<size_t>(slot)] < 0) continue;
        const int scaledHeight = std::max(24, heights[static_cast<size_t>(slot)] * targetCoverWidth /
                                                   sourceCoverWidth);
        heights[static_cast<size_t>(slot)] = scaledHeight;
        widths[static_cast<size_t>(slot)] = std::max(18, scaledHeight * 2 / 3);
      }
      rowWidth = gapWidth;
      for (int slot = 0; slot < slots; ++slot) {
        if (indices[static_cast<size_t>(slot)] >= 0) rowWidth += widths[static_cast<size_t>(slot)];
      }
    }
    int cursorX = g.left + 6 + std::max(0, (g.width - 12 - rowWidth) / 2);
    for (int slot = 0; slot < slots; ++slot) {
      const int index = indices[static_cast<size_t>(slot)];
      if (index < 0) continue;
      const int coverW = widths[static_cast<size_t>(slot)];
      const int coverH = heights[static_cast<size_t>(slot)];
      const int coverY = g.shelfY[shelf] - coverH;
      drawBook(renderer, books[index], cursorX, coverY, coverW, coverH,
               drawSelection && index == selectedIndex);
      cursorX += coverW + kBookGap;
    }
    drawShelf(renderer, g, g.shelfY[shelf]);
  }
}

void renderShelfSelection(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                          const int count, const int selectedIndex) {
  if (width <= 0 || height <= 0 || selectedIndex < 0 || selectedIndex >= count) return;
  const Geometry g = geometry(x, y, width, height);

  for (int shelf = 0; shelf < kShelfCount; ++shelf) {
    const int slots = shelfBookSlots(shelf);
    std::array<int, kBottomBookSlots> indices{};
    std::array<int, kBottomBookSlots> widths{};
    std::array<int, kBottomBookSlots> heights{};
    int rowWidth = 0;
    int visibleBooks = 0;
    for (int slot = 0; slot < slots; ++slot) {
      const int index = bookIndexForSlot(shelf, slot, count);
      indices[static_cast<size_t>(slot)] = index;
      if (index < 0) continue;
      const int coverH = std::min(g.maxBookHeight, bookHeight(g, shelf, slot) + 10);
      const int coverW = std::max(18, coverH * 2 / 3);
      widths[static_cast<size_t>(slot)] = coverW;
      heights[static_cast<size_t>(slot)] = coverH;
      rowWidth += coverW;
      ++visibleBooks;
    }
    if (visibleBooks > 1) rowWidth += kBookGap * (visibleBooks - 1);
    const int gapWidth = visibleBooks > 1 ? kBookGap * (visibleBooks - 1) : 0;
    const int maxBookRowWidth = std::max(1, g.width - 12);
    if (rowWidth > maxBookRowWidth && visibleBooks > 0) {
      const int targetCoverWidth = std::max(1, maxBookRowWidth - gapWidth);
      const int sourceCoverWidth = std::max(1, rowWidth - gapWidth);
      for (int slot = 0; slot < slots; ++slot) {
        if (indices[static_cast<size_t>(slot)] < 0) continue;
        const int scaledHeight = std::max(24, heights[static_cast<size_t>(slot)] * targetCoverWidth /
                                                   sourceCoverWidth);
        heights[static_cast<size_t>(slot)] = scaledHeight;
        widths[static_cast<size_t>(slot)] = std::max(18, scaledHeight * 2 / 3);
      }
      rowWidth = gapWidth;
      for (int slot = 0; slot < slots; ++slot) {
        if (indices[static_cast<size_t>(slot)] >= 0) rowWidth += widths[static_cast<size_t>(slot)];
      }
    }
    int cursorX = g.left + 6 + std::max(0, (g.width - 12 - rowWidth) / 2);
    for (int slot = 0; slot < slots; ++slot) {
      const int index = indices[static_cast<size_t>(slot)];
      if (index < 0) continue;
      const int coverW = widths[static_cast<size_t>(slot)];
      const int coverH = heights[static_cast<size_t>(slot)];
      if (index == selectedIndex) drawSelection(renderer, cursorX, g.shelfY[shelf] - coverH, coverW, coverH);
      cursorX += coverW + kBookGap;
    }
  }
}

}  // namespace

void Bookshelf::render(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                       const int selectedIndex, const bool drawSelection) {
  const auto& books = RECENT_BOOKS.getBooks();
  renderer.rectangle.fill(x, y, width, height, false);
  if (books.empty()) {
    renderer.text.centered(MONTSERRAT_12_FONT_ID, y + height / 2, "No recent books");
    return;
  }
  renderBookshelf(renderer, x, y, width, height, books.data(),
                  std::min(static_cast<int>(books.size()), kShelfCount * kBottomBookSlots), selectedIndex,
                  drawSelection);
}

void Bookshelf::renderSelection(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                                const int selectedIndex) {
  const auto& books = RECENT_BOOKS.getBooks();
  renderShelfSelection(renderer, x, y, width, height,
                       std::min(static_cast<int>(books.size()), kShelfCount * kBottomBookSlots), selectedIndex);
}

void Bookshelf::preview(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  static constexpr const char* kTitles[] = {"Design",       "The Exquisite", "A Field Guide", "Modern Type",
                                             "The Long Way", "New Voices",    "Small Worlds",  "Collected Works",
                                             "New Fiction",  "Night Reading", "Open Water",    "New Essays"};
  static constexpr int kCount = sizeof(kTitles) / sizeof(kTitles[0]);
  std::array<RecentBook, kCount> books;
  for (int i = 0; i < kCount; ++i) books[static_cast<size_t>(i)] = RecentBook("", "", kTitles[i], "", 0.0f);
  renderer.rectangle.fill(x, y, width, height, false);
  renderBookshelf(renderer, x, y, width, height, books.data(), kCount, 0, true);
}

}  // namespace widget::bookshelf
