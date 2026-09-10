#include "Carousel.h"

#include <BitmapRender.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/UiLayout.h"

namespace widget {
namespace {

std::string thumbnailPath(const std::string& cacheDir) {
  if (cacheDir.empty()) {
    return {};
  }
  char path[192];
  for (const char* extension : {"thumb.jpg", "thumb.png", "thumb.bmp"}) {
    snprintf(path, sizeof(path), "%s/%s", cacheDir.c_str(), extension);
    if (SdMan.exists(path)) {
      return path;
    }
  }
  return {};
}

std::string bookTitle(const RecentBook& book) {
  if (!book.title.empty()) {
    return book.title;
  }
  const size_t slash = book.path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = book.path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot < start ? book.path.size() : dot;
  return book.path.substr(start, end - start);
}

void renderBackdrop(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  if (width <= 0 || height <= 0) {
    return;
  }

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  // Match the legacy Flow backdrop and the sparse-ink-aligned rounded image corners.
  for (int py = (y - 5 + 1) & ~1; py < y + height + 10; py += 2) {
    if (py < 0 || py >= screenH) {
      continue;
    }
    for (int px = (x - 5 + 1) & ~1; px < x + width + 10; px += 2) {
      if (px >= 0 && px < screenW) {
        renderer.drawPixel(px, py, true);
      }
    }
  }
}

}  // namespace

void Carousel::render(const int index, const int x, const int y, const int width, const int height) const {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) {
    if (width > 0 && height > 0) {
      renderer_.text.centered(MONTSERRAT_12_FONT_ID, y + height / 2, "No recent books");
    }
    return;
  }
  render(books, index, x, y, width, height, &Carousel::renderDefaultThumbnail,
         const_cast<Carousel*>(this));
}

void Carousel::renderDefaultThumbnail(void* context, const RecentBook& book, const int x, const int y,
                                      const int width, const int height, const int placeholderFontId,
                                      const bool /*roundedCornerBackdropIsDither*/) {
  auto* self = static_cast<Carousel*>(context);
  if (self == nullptr || width <= 0 || height <= 0) {
    return;
  }
  GfxRenderer& renderer = self->renderer_;

  std::string cacheDir = book.cachePath;
  if (cacheDir.empty()) {
    cacheDir = "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(book.path));
  }
  const std::string path = thumbnailPath(cacheDir);
  if (!path.empty()) {
    ImageRender::Options options;
    options.cropToFill = true;
    options.useDisplayCache = true;
    if (SETTINGS.bitmapRoundedCorners == 0) {
      options.roundedOutside = BitmapRender::RoundedOutside::None;
    } else if (SETTINGS.bitmapRoundedCorners == 2) {
      options.roundedOutside = BitmapRender::RoundedOutside::SubtleSparseInkAlignedOutside;
    } else {
      options.roundedOutside = BitmapRender::RoundedOutside::SparseInkAlignedOutside;
    }
    if (ImageRender::create(renderer, path).render(x, y, width, height, options)) {
      return;
    }
  }

  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(x, y, width, height, false, rounded);
  renderer.rectangle.render(x, y, width, height, true, rounded, SETTINGS.bitmapRoundedCorners == 2);
  const std::string title = bookTitle(book);
  const std::string shown = renderer.text.truncate(placeholderFontId, title.c_str(), std::max(1, width - 8));
  const int textWidth = renderer.text.getWidth(placeholderFontId, shown.c_str());
  const int lineHeight = renderer.text.getLineHeight(placeholderFontId);
  renderer.text.render(placeholderFontId, x + std::max(4, (width - textWidth) / 2),
                       y + std::max(4, (height - lineHeight) / 2), shown.c_str(), true, EpdFontFamily::REGULAR);
}

void Carousel::render(const std::vector<RecentBook>& books, const int index, const int x, const int y, const int width,
                      const int height, const ThumbnailRenderer thumbnailRenderer, void* const context) const {
  if (books.empty() || width <= 0 || height <= 0 || thumbnailRenderer == nullptr) {
    return;
  }

  const int currentIndex = std::max(0, std::min(index, static_cast<int>(books.size()) - 1));
  const int carouselHeight = std::min(height, UiLayout::FLOW_CAROUSEL_HEIGHT);
  const int centerWidth = std::min(width, UiLayout::FLOW_CAROUSEL_CENTER_WIDTH);
  const int centerHeight = std::min(carouselHeight, UiLayout::FLOW_CAROUSEL_CENTER_HEIGHT);
  const int centerX = x + (width - centerWidth) / 2;
  const int centerY = y + (carouselHeight - centerHeight) / 2 + 4;
  const int sideWidth = centerWidth * UiLayout::FLOW_CAROUSEL_SIDE_SCALE_PERCENT / 100;
  const int sideHeight = centerHeight * UiLayout::FLOW_CAROUSEL_SIDE_SCALE_PERCENT / 100;
  const int sideY = centerY + (centerHeight - sideHeight) / 2;
  const int leftX = centerX - sideWidth - UiLayout::FLOW_CAROUSEL_CARD_GAP;
  const int rightX = centerX + centerWidth + UiLayout::FLOW_CAROUSEL_CARD_GAP;

  renderBackdrop(renderer_, x, y, width, carouselHeight);
  const bool roundedCorners = SETTINGS.bitmapRoundedCorners != 0;

  if (books.size() > 1) {
    // RecentActivity's legacy Flow wraps at both ends, so the preview remains
    // a real carousel instead of disappearing when the first/last book is selected.
    const int leftIndex = currentIndex == 0 ? static_cast<int>(books.size()) - 1 : currentIndex - 1;
    const RecentBook& leftBook = books[static_cast<size_t>(leftIndex)];
    renderer_.rectangle.fill(leftX, sideY, sideWidth, sideHeight, false, roundedCorners);
    thumbnailRenderer(context, leftBook, leftX, sideY, sideWidth, sideHeight, MONTSERRAT_10_FONT_ID, true);

    const int rightIndex = currentIndex + 1 >= static_cast<int>(books.size()) ? 0 : currentIndex + 1;
    const RecentBook& rightBook = books[static_cast<size_t>(rightIndex)];
    renderer_.rectangle.fill(rightX, sideY, sideWidth, sideHeight, false, roundedCorners);
    thumbnailRenderer(context, rightBook, rightX, sideY, sideWidth, sideHeight, MONTSERRAT_10_FONT_ID, true);
  }

  const RecentBook& currentBook = books[static_cast<size_t>(currentIndex)];
  renderer_.rectangle.fill(centerX, centerY, centerWidth, centerHeight, false, roundedCorners);
  thumbnailRenderer(context, currentBook, centerX, centerY, centerWidth, centerHeight,
                    MONTSERRAT_14_FONT_ID, true);
}

}  // namespace widget
