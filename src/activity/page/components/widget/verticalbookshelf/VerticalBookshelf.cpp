#include "VerticalBookshelf.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

#include "../WidgetRender.h"
#include "state/RecentBooks.h"
#include "system/Fonts.h"

namespace widget::verticalbookshelf {
namespace {

constexpr int kShelfCount = 2;
constexpr int kBooksPerShelf = 6;
constexpr int kTotalBooks = kShelfCount * kBooksPerShelf;
constexpr int kOuterMargin = 20;
constexpr int kBookGap = 15;
constexpr int kShelfGap = 4;
constexpr int kShelfFrontHeight = 10;
constexpr int kTopPadding = 4;
constexpr int kBottomShelfLift = 20;
constexpr int kCoverAspectWidth = 170;
constexpr int kCoverAspectHeight = 250;
constexpr int kLeanNumerator = 1;
constexpr int kLeanDenominator = 10;

struct Geometry {
  int left;
  int width;
  int shelfY[kShelfCount];
  int maxBookHeight;
};

Geometry geometry(const int x, const int y, const int width, const int height, const bool compact) {
  const int innerWidth = std::max(1, width - kOuterMargin * 2);
  const int usableTop = y + kTopPadding;
  const int bottomShelfY = y + height - kShelfFrontHeight - 2 - kBottomShelfLift;
  const int shelfSpan = std::max(40, bottomShelfY - usableTop);
  const int shelfSpacing = kShelfGap + kShelfFrontHeight;
  const int naturalBookHeight = (shelfSpan - 8 - shelfSpacing) / kShelfCount;
  const int maxBookHeight = std::max(24, naturalBookHeight - (compact ? 4 : 0));
  return {x + kOuterMargin, innerWidth, {usableTop + 4 + maxBookHeight, bottomShelfY}, maxBookHeight};
}

int topShelfIndex(const int slot, const int count) {
  // Like the reference image: the newest book is the third vertical object,
  // facing forward, with the next-nearest books around it.
  static constexpr int kOrder[kBooksPerShelf] = {1, 2, 0, 3, 4, 5};
  const int index = kOrder[slot];
  return index < count ? index : -1;
}

int bookIndexForSlot(const int shelf, const int slot, const int count) {
  const int index = shelf == 0 ? topShelfIndex(slot, count) : kBooksPerShelf + slot;
  return index < count ? index : -1;
}

int navigationIndex(const int currentIndex, const int count, const bool forward) {
  if (count <= 0) return 0;
  static constexpr int kNavigationOrder[kTotalBooks] = {1, 2, 0, 3, 4, 5, 6, 7, 8, 9, 10, 11};

  int position = -1;
  int visibleCount = 0;
  for (int i = 0; i < kTotalBooks; ++i) {
    if (kNavigationOrder[i] >= count) continue;
    if (kNavigationOrder[i] == currentIndex) position = visibleCount;
    ++visibleCount;
  }
  if (visibleCount <= 0) return 0;
  if (position < 0) position = 0;
  const int offset = forward ? 1 : visibleCount - 1;
  const int targetPosition = (position + offset) % visibleCount;
  int visiblePosition = 0;
  for (int i = 0; i < kTotalBooks; ++i) {
    if (kNavigationOrder[i] >= count) continue;
    if (visiblePosition == targetPosition) return kNavigationOrder[i];
    ++visiblePosition;
  }
  return 0;
}

int bookHeight(const Geometry& g, const int shelf, const int slot) {
  static constexpr int kHeightPercent[kShelfCount][kBooksPerShelf] = {
      {72, 84, 92, 78, 88, 74},
      {82, 70, 88, 76, 84, 72},
  };
  return std::max(24, g.maxBookHeight * kHeightPercent[shelf][slot] / 100);
}

int frontCoverWidth(const Geometry& g) {
  const int referenceHeight = std::max(24, g.maxBookHeight * 88 / 100);
  return std::max(24, referenceHeight * 2 / 3);
}

int frontCoverHeight(const Geometry& g) {
  return std::max(24, frontCoverWidth(g) * kCoverAspectHeight / kCoverAspectWidth);
}

void drawShelf(const GfxRenderer& renderer, const Geometry& g, const int shelfY) {
  renderer.rectangle.fill(g.left + 4, shelfY + 4, g.width, kShelfFrontHeight,
                          static_cast<int>(GfxRenderer::FillTone::Gray));
  renderer.rectangle.fill(g.left, shelfY, g.width, kShelfFrontHeight,
                          static_cast<int>(GfxRenderer::FillTone::Paper));
  renderer.rectangle.render(g.left, shelfY, g.width, kShelfFrontHeight, true);
  renderer.line.render(g.left + 3, shelfY + 2, g.left + g.width - 4, shelfY + 2);
}

void drawSelection(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  constexpr int pad = 6;
  support::drawDitherRect(renderer, x - pad, y - pad, width + pad * 2, height + pad * 2);
  renderer.rectangle.render(x - pad, y - pad, width + pad * 2, height + pad * 2, true, false, false);
}

int leanOffset(const int sourceY, const int height) {
  return ((sourceY * 2 - height + 1) * kLeanNumerator) / (2 * kLeanDenominator);
}

int leanYOffset(const int sourceX, const int width) {
  return -((sourceX * 2 - width + 1) * kLeanNumerator) / (2 * kLeanDenominator);
}

void drawLine(const GfxRenderer& renderer, int x0, int y0, int x1, int y1) {
  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  while (true) {
    renderer.drawPixel(x0, y0, true);
    if (x0 == x1 && y0 == y1) break;
    const int twiceError = 2 * error;
    if (twiceError >= dy) {
      error += dy;
      x0 += sx;
    }
    if (twiceError <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

void drawLeaningOutline(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                        const int padding, const bool includeTop = true) {
  const int topOffset = leanOffset(0, height);
  const int bottomOffset = leanOffset(height - 1, height);
  const int leftYOffset = leanYOffset(0, width);
  const int rightYOffset = leanYOffset(width - 1, width);
  const int topLeftX = x + topOffset - padding;
  const int topLeftY = y + leftYOffset - padding;
  const int topRightX = x + width - 1 + topOffset + padding;
  const int topRightY = y + rightYOffset - padding;
  const int bottomLeftX = x + bottomOffset - padding;
  const int bottomLeftY = y + height - 1 + leftYOffset + padding;
  const int bottomRightX = x + width - 1 + bottomOffset + padding;
  const int bottomRightY = y + height - 1 + rightYOffset + padding;
  if (includeTop) drawLine(renderer, topLeftX, topLeftY, topRightX, topRightY);
  drawLine(renderer, topRightX, topRightY, bottomRightX, bottomRightY);
  drawLine(renderer, bottomRightX, bottomRightY, bottomLeftX, bottomLeftY);
  drawLine(renderer, bottomLeftX, bottomLeftY, topLeftX, topLeftY);
}

void drawLeaningSelectionDither(const GfxRenderer& renderer, const int x, const int y, const int width,
                                const int height) {
  constexpr int padding = 6;
  const int topOffset = leanOffset(0, height);
  const int bottomOffset = leanOffset(height - 1, height);
  const int leftYOffset = leanYOffset(0, width);
  const int rightYOffset = leanYOffset(width - 1, width);
  const int minX = x + std::min(topOffset, bottomOffset) - padding;
  const int minY = y + std::min(leftYOffset, rightYOffset) - padding;
  const int maxX = x + width - 1 + std::max(topOffset, bottomOffset) + padding;
  const int maxY = y + height - 1 + std::max(leftYOffset, rightYOffset) + padding;
  support::drawDitherRect(renderer, minX, minY, maxX - minX + 1, maxY - minY + 1);
}

void drawLeaningSelection(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  constexpr int padding = 6;
  drawLeaningSelectionDither(renderer, x, y, width, height);
  drawLeaningOutline(renderer, x, y, width, height, padding);
}

int fakePageHeight(const int width, const int height) {
  // Match the reference proportions: a front cover has a shallow page edge,
  // while a narrow standing spine has a tall visible page block.
  if (width * 2 > height) return std::max(8, std::min(32, height / 16));
  return std::max(10, std::min(height / 3, width));
}

bool hasThumbnail(const RecentBook& book) {
  const std::string cache = support::cachePathFor(book);
  for (const char* extension : {"thumb.jpg", "thumb.png", "thumb.bmp"}) {
    const std::string path = cache + "/" + extension;
    if (SdMan.exists(path.c_str())) return true;
  }
  return false;
}

void drawMissingSpineTitle(const GfxRenderer& renderer, const RecentBook& book, const int x, const int y,
                           const int width, const int height) {
  if (width <= 0 || height <= 0) return;

  const int font = MONTSERRAT_8_FONT_ID;
  const std::string title = support::titleFor(book);
  const int availableLength = std::max(1, height - 8);
  const std::string shown = renderer.text.truncate(font, title.c_str(), availableLength);
  const int textLength = renderer.text.getWidth(font, shown.c_str());
  const int lineHeight = renderer.text.getFontAscenderSize(font);
  const int textX = x + std::max(0, (width - lineHeight) / 2);
  const int textY = y + (height + textLength) / 2;
  renderer.text.rotated90CW(font, textX, textY, shown.c_str(), true, EpdFontFamily::REGULAR);
}

void drawBookShadow(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  if (width <= 0 || height <= 0) return;
  const bool narrowSpine = width * 2 <= height;
  if (narrowSpine) {
    const int shadowHeight = std::max(1, height - 10);
    renderer.rectangle.fill(x + 4, y + height - shadowHeight, width, shadowHeight,
                            static_cast<int>(GfxRenderer::FillTone::Gray));
    return;
  }
  renderer.rectangle.fill(x + 4, y + 4, width, std::max(1, height - 4),
                          static_cast<int>(GfxRenderer::FillTone::Gray));
}

void drawBookBody(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                 const int height) {
  if (width <= 0 || height <= 0) return;
  const bool narrowSpine = width * 2 <= height;
  if (narrowSpine && !hasThumbnail(book)) {
    renderer.rectangle.fill(x, y, width, height, false);
    drawMissingSpineTitle(renderer, book, x, y, width, height);
    return;
  }
  support::drawThumbnail(renderer, book, x, y, width, height, MONTSERRAT_10_FONT_ID, false, true, true, true);
}

void drawFakePages(const GfxRenderer& renderer, const int x, const int y, const int width, const int pageHeight,
                   const int totalHeight) {
  if (width < 8 || pageHeight < 6) return;
  const int pageX = x;
  const int pageWidth = std::max(4, width);
  // Front-facing covers show the page edges horizontally. Narrow vertical
  // spines show the page grooves vertically, as in the supplied reference.
  if (width * 2 > totalHeight) {
    renderer.rectangle.fill(pageX, y, pageWidth, pageHeight,
                            static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.rectangle.render(pageX, y, pageWidth, pageHeight, true);
    renderer.rectangle.fill(pageX, y, 3, pageHeight,
                            static_cast<int>(GfxRenderer::FillTone::Ink));
    for (int lineY = y + 3; lineY < y + pageHeight - 1; lineY += 3) {
      renderer.rectangle.fill(pageX + 4, lineY, std::max(1, pageWidth - 6), 1,
                              static_cast<int>(GfxRenderer::FillTone::Gray));
    }
  } else {
    constexpr int topBandHeight = 5;
    constexpr int bottomBandHeight = 4;
    const int sideRail = std::max(3, std::min(5, width / 10 + 1));
    const int stripeY = y + topBandHeight;
    const int stripeHeight = std::max(2, pageHeight - topBandHeight - bottomBandHeight);
    const int stripeX = pageX + sideRail;
    const int stripeWidth = std::max(2, pageWidth - sideRail * 2);

    renderer.rectangle.fill(pageX, y, pageWidth, topBandHeight,
                            static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.rectangle.fill(stripeX, stripeY, stripeWidth, stripeHeight,
                            static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.rectangle.fill(pageX, stripeY, sideRail, pageHeight - topBandHeight,
                            static_cast<int>(GfxRenderer::FillTone::Ink));
    renderer.rectangle.fill(pageX + pageWidth - sideRail, stripeY, sideRail,
                            pageHeight - topBandHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
    renderer.rectangle.fill(stripeX, y + pageHeight - bottomBandHeight, stripeWidth, bottomBandHeight,
                            static_cast<int>(GfxRenderer::FillTone::Ink));
    // Small corner blocks give the lower U-shaped join the stepped detail of
    // a real page edge instead of a perfectly square right angle.
    const int cornerSize = std::max(2, std::min(3, sideRail));
    const int cornerY = y + pageHeight - bottomBandHeight - cornerSize + 1;
    renderer.rectangle.fill(stripeX, cornerY, cornerSize, cornerSize,
                            static_cast<int>(GfxRenderer::FillTone::Ink));
    renderer.rectangle.fill(stripeX + stripeWidth - cornerSize, cornerY, cornerSize, cornerSize,
                            static_cast<int>(GfxRenderer::FillTone::Ink));
    static constexpr int stripeSteps[] = {3, 4, 2, 5, 3, 4, 3, 2, 5, 3};
    static constexpr int stripeWidths[] = {1, 2, 1, 1, 2, 1, 1, 2, 1, 1};
    int lineX = stripeX + 2;
    int stripe = 0;
    while (lineX < stripeX + stripeWidth - 1) {
      renderer.rectangle.fill(lineX, stripeY + 2, stripeWidths[stripe], std::max(1, stripeHeight - 4),
                              static_cast<int>(GfxRenderer::FillTone::Gray));
      lineX += stripeSteps[stripe];
      stripe = (stripe + 1) % (sizeof(stripeSteps) / sizeof(stripeSteps[0]));
    }
  }
}

void drawLeaningBook(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                     const int height, const bool selected) {
  if (width <= 0 || height <= 0) return;
  if (selected) drawLeaningSelectionDither(renderer, x, y, width, height);

  const int pageHeight = fakePageHeight(width, height);
  const int bodyY = y + pageHeight;
  const int bodyHeight = std::max(8, height - pageHeight);
  drawBookShadow(renderer, x, y, width, height);
  renderer.rectangle.fill(x, y, width, height, false);
  drawBookBody(renderer, book, x, bodyY, width, bodyHeight);
  drawFakePages(renderer, x, y, width, pageHeight, height);

  std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
  for (int sourceY = 0; sourceY < height; ++sourceY) {
    for (int sourceX = 0; sourceX < width; ++sourceX) {
      pixels[static_cast<size_t>(sourceY) * static_cast<size_t>(width) + static_cast<size_t>(sourceX)] =
          renderer.readPixel(x + sourceX, y + sourceY) ? 1 : 0;
    }
  }

  const int topOffset = leanOffset(0, height);
  const int bottomOffset = leanOffset(height - 1, height);
  const int leftYOffset = leanYOffset(0, width);
  const int rightYOffset = leanYOffset(width - 1, width);
  const int clearX = x + std::min(topOffset, bottomOffset);
  const int clearY = y + std::min(leftYOffset, rightYOffset);
  const int clearWidth = width + std::abs(bottomOffset - topOffset);
  const int clearHeight = height + std::abs(rightYOffset - leftYOffset);
  renderer.rectangle.fill(clearX, clearY, clearWidth, clearHeight, false);

  for (int sourceY = 0; sourceY < height; ++sourceY) {
    const int offset = leanOffset(sourceY, height);
    for (int sourceX = 0; sourceX < width; ++sourceX) {
      if (pixels[static_cast<size_t>(sourceY) * static_cast<size_t>(width) + static_cast<size_t>(sourceX)] != 0) {
        renderer.drawPixel(x + sourceX + offset, y + sourceY + leanYOffset(sourceX, width), true);
      }
    }
  }

  drawLeaningOutline(renderer, x, y, width, height, 0, false);
  if (selected) drawLeaningOutline(renderer, x, y, width, height, 6);
}

void drawBook(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
              const int height, const bool leaning, const bool selected) {
  if (width <= 0 || height <= 0) return;
  if (leaning) {
    drawLeaningBook(renderer, book, x, y, width, height, selected);
    return;
  }
  if (selected) drawSelection(renderer, x, y, width, height);
  const int pageHeight = fakePageHeight(width, height);
  const int bodyY = y + pageHeight;
  const int bodyHeight = std::max(8, height - pageHeight);
  drawBookShadow(renderer, x, y, width, height);
  renderer.rectangle.fill(x, y, width, height, false);
  drawBookBody(renderer, book, x, bodyY, width, bodyHeight);
  drawFakePages(renderer, x, y, width, pageHeight, height);
  renderer.rectangle.render(x, y + pageHeight, width, height - pageHeight, true, false, false);
}

void renderShelf(GfxRenderer& renderer, const Geometry& g, const RecentBook* books, const int count,
                 const int shelf, const int selectedIndex, const bool drawSelection) {
  std::array<int, kBooksPerShelf> indices{};
  std::array<int, kBooksPerShelf> widths{};
  std::array<int, kBooksPerShelf> heights{};
  int rowWidth = 0;
  int visible = 0;
  for (int slot = 0; slot < kBooksPerShelf; ++slot) {
    const int index = bookIndexForSlot(shelf, slot, count);
    indices[static_cast<size_t>(slot)] = index;
    if (index < 0) continue;
    const bool frontFacing = shelf == 0 && index == 0;
    const int height = frontFacing ? frontCoverHeight(g) : bookHeight(g, shelf, slot);
    const int width = frontFacing ? frontCoverWidth(g) : std::max(24, height / 4);
    heights[static_cast<size_t>(slot)] = height;
    widths[static_cast<size_t>(slot)] = width;
    rowWidth += width;
    ++visible;
  }
  if (visible > 1) rowWidth += kBookGap * (visible - 1);

  // Keep the exact 15 pt gaps and scale the vertical spine widths only if a
  // packed row would exceed the 20 pt side margins.
  const int maxRowWidth = std::max(1, g.width - 12);
  if (rowWidth > maxRowWidth && visible > 0) {
    const int gapWidth = kBookGap * (visible - 1);
    const int targetCoverWidth = std::max(1, maxRowWidth - gapWidth);
    const int sourceCoverWidth = std::max(1, rowWidth - gapWidth);
    for (int slot = 0; slot < kBooksPerShelf; ++slot) {
      if (indices[static_cast<size_t>(slot)] < 0) continue;
      const int scaledWidth = std::max(18, widths[static_cast<size_t>(slot)] * targetCoverWidth / sourceCoverWidth);
      widths[static_cast<size_t>(slot)] = scaledWidth;
      if (shelf == 0 && indices[static_cast<size_t>(slot)] == 0) {
        heights[static_cast<size_t>(slot)] = std::max(24, scaledWidth * kCoverAspectHeight / kCoverAspectWidth);
      }
    }
    rowWidth = gapWidth;
    for (int slot = 0; slot < kBooksPerShelf; ++slot) {
      if (indices[static_cast<size_t>(slot)] >= 0) rowWidth += widths[static_cast<size_t>(slot)];
    }
  }

  int cursorX = g.left + 6 + std::max(0, (g.width - 12 - rowWidth) / 2);
  for (int slot = 0; slot < kBooksPerShelf; ++slot) {
    const int index = indices[static_cast<size_t>(slot)];
    if (index < 0) continue;
    const int coverW = widths[static_cast<size_t>(slot)];
    const int coverH = heights[static_cast<size_t>(slot)];
    const int coverY = g.shelfY[shelf] - coverH;
    const bool leaning = shelf == 0 && slot == kBooksPerShelf - 1;
    drawBook(renderer, books[index], cursorX, coverY, coverW, coverH, leaning,
             drawSelection && index == selectedIndex);
    cursorX += coverW + kBookGap;
  }
}

void renderBookshelf(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                     const RecentBook* books, const int count, const int selectedIndex, const bool drawSelection,
                     const bool compact) {
  if (width <= 0 || height <= 0) return;
  const Geometry g = geometry(x, y, width, height, compact);
  for (int shelf = 0; shelf < kShelfCount; ++shelf) {
    renderShelf(renderer, g, books, count, shelf, selectedIndex, drawSelection);
    drawShelf(renderer, g, g.shelfY[shelf]);
  }
}

void renderShelfSelection(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                          const int count, const int selectedIndex, const bool compact) {
  if (width <= 0 || height <= 0 || selectedIndex < 0 || selectedIndex >= count) return;
  const Geometry g = geometry(x, y, width, height, compact);

  for (int shelf = 0; shelf < kShelfCount; ++shelf) {
    std::array<int, kBooksPerShelf> indices{};
    std::array<int, kBooksPerShelf> widths{};
    std::array<int, kBooksPerShelf> heights{};
    int rowWidth = 0;
    int visible = 0;
    for (int slot = 0; slot < kBooksPerShelf; ++slot) {
      const int index = bookIndexForSlot(shelf, slot, count);
      indices[static_cast<size_t>(slot)] = index;
      if (index < 0) continue;
      const bool frontFacing = shelf == 0 && index == 0;
      const int coverH = frontFacing ? frontCoverHeight(g) : bookHeight(g, shelf, slot);
      const int coverW = frontFacing ? frontCoverWidth(g) : std::max(24, coverH / 4);
      heights[static_cast<size_t>(slot)] = coverH;
      widths[static_cast<size_t>(slot)] = coverW;
      rowWidth += coverW;
      ++visible;
    }
    if (visible > 1) rowWidth += kBookGap * (visible - 1);

    const int maxRowWidth = std::max(1, g.width - 12);
    if (rowWidth > maxRowWidth && visible > 0) {
      const int gapWidth = kBookGap * (visible - 1);
      const int targetCoverWidth = std::max(1, maxRowWidth - gapWidth);
      const int sourceCoverWidth = std::max(1, rowWidth - gapWidth);
      for (int slot = 0; slot < kBooksPerShelf; ++slot) {
        if (indices[static_cast<size_t>(slot)] < 0) continue;
        const int scaledWidth = std::max(18, widths[static_cast<size_t>(slot)] * targetCoverWidth / sourceCoverWidth);
        widths[static_cast<size_t>(slot)] = scaledWidth;
        if (shelf == 0 && indices[static_cast<size_t>(slot)] == 0) {
          heights[static_cast<size_t>(slot)] = std::max(24, scaledWidth * kCoverAspectHeight / kCoverAspectWidth);
        }
      }
      rowWidth = gapWidth;
      for (int slot = 0; slot < kBooksPerShelf; ++slot) {
        if (indices[static_cast<size_t>(slot)] >= 0) rowWidth += widths[static_cast<size_t>(slot)];
      }
    }

    int cursorX = g.left + 6 + std::max(0, (g.width - 12 - rowWidth) / 2);
    for (int slot = 0; slot < kBooksPerShelf; ++slot) {
      const int index = indices[static_cast<size_t>(slot)];
      if (index < 0) continue;
      const int coverW = widths[static_cast<size_t>(slot)];
      const int coverH = heights[static_cast<size_t>(slot)];
    if (index == selectedIndex) {
      const bool leaning = shelf == 0 && slot == kBooksPerShelf - 1;
      if (leaning) {
        drawLeaningSelection(renderer, cursorX, g.shelfY[shelf] - coverH, coverW, coverH);
      } else {
        drawSelection(renderer, cursorX, g.shelfY[shelf] - coverH, coverW, coverH);
      }
    }
      cursorX += coverW + kBookGap;
    }
  }
}

}  // namespace

void VerticalBookshelf::render(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                               const int selectedIndex, const bool drawSelection) {
  const auto& books = RECENT_BOOKS.getBooks();
  renderer.rectangle.fill(x, y, width, height, false);
  if (books.empty()) {
    renderer.text.centered(MONTSERRAT_12_FONT_ID, y + height / 2, "No recent books");
    return;
  }
  renderBookshelf(renderer, x, y, width, height, books.data(),
                  std::min(static_cast<int>(books.size()), kTotalBooks), selectedIndex, drawSelection,
                  renderer.deviceIsX3());
}

void VerticalBookshelf::renderSelection(GfxRenderer& renderer, const int x, const int y, const int width,
                                        const int height, const int selectedIndex) {
  const auto& books = RECENT_BOOKS.getBooks();
  renderShelfSelection(renderer, x, y, width, height, std::min(static_cast<int>(books.size()), kTotalBooks),
                       selectedIndex, renderer.deviceIsX3());
}

int VerticalBookshelf::nextSelectionIndex(const int currentIndex, const int count) {
  return navigationIndex(currentIndex, std::min(count, kTotalBooks), true);
}

int VerticalBookshelf::previousSelectionIndex(const int currentIndex, const int count) {
  return navigationIndex(currentIndex, std::min(count, kTotalBooks), false);
}

void VerticalBookshelf::preview(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  static constexpr const char* kTitles[] = {"Just My Type", "The Unpressed", "Design", "The Exquisite",
                                             "A Field Guide", "Modern Type", "New Voices", "Small Worlds",
                                             "Open Water", "Collected Works", "Night Reading", "New Essays"};
  std::array<RecentBook, kTotalBooks> books;
  for (int i = 0; i < kTotalBooks; ++i) books[static_cast<size_t>(i)] = RecentBook("", "", kTitles[i], "", 0.0f);
  renderer.rectangle.fill(x, y, width, height, false);
  renderBookshelf(renderer, x, y, width, height, books.data(), kTotalBooks, 0, true, renderer.deviceIsX3());
}

}  // namespace widget::verticalbookshelf
