#include "WidgetRender.h"

#include <BitmapRender.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>

#include "state/SystemSetting.h"
#include "system/Fonts.h"

namespace widget::support {
namespace {

std::string thumbnailPath(const RecentBook& book) {
  char path[192] = {};
  const std::string cache = cachePathFor(book);
  for (const char* extension : {"thumb.jpg", "thumb.png", "thumb.bmp"}) {
    std::snprintf(path, sizeof(path), "%s/%s", cache.c_str(), extension);
    if (SdMan.exists(path)) return path;
  }
  return {};
}

}  // namespace

std::string titleFor(const RecentBook& book) {
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = book.path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot < start ? book.path.size() : dot;
  std::string title = book.path.substr(start, end - start);
  bool capitalize = true;
  for (char& c : title) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      capitalize = true;
    } else if (capitalize) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      capitalize = false;
    }
  }
  return title;
}

std::string cachePathFor(const RecentBook& book) {
  if (!book.cachePath.empty()) return book.cachePath;
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(book.path));
}

void drawPlaceholder(const GfxRenderer& renderer, const std::string& title, const int x, const int y, const int width,
                     const int height, const int font) {
  if (width <= 0 || height <= 0) return;
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(x, y, width, height, false, rounded, SETTINGS.bitmapRoundedCorners == 2);
  renderer.rectangle.render(x, y, width, height, true, rounded, SETTINGS.bitmapRoundedCorners == 2);
  const std::string shown = renderer.text.truncate(font, title.c_str(), std::max(1, width - 10));
  const int textWidth = renderer.text.getWidth(font, shown.c_str());
  const int lineHeight = renderer.text.getLineHeight(font);
  renderer.text.render(font, x + std::max(5, (width - textWidth) / 2),
                       y + std::max(5, (height - lineHeight) / 2), shown.c_str(), true,
                       EpdFontFamily::REGULAR);
}

void drawThumbnail(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                   const int height, const int font, const bool roundedCornerBackdropIsDither) {
  if (width <= 0 || height <= 0) return;
  const std::string path = thumbnailPath(book);
  if (!path.empty()) {
    ImageRender::Options options;
    options.cropToFill = true;
    options.useDisplayCache = true;
    if (SETTINGS.bitmapRoundedCorners == 0) {
      options.roundedOutside = BitmapRender::RoundedOutside::None;
    } else if (SETTINGS.bitmapRoundedCorners == 2) {
      options.roundedOutside = roundedCornerBackdropIsDither
                                   ? BitmapRender::RoundedOutside::SubtleSparseInkAlignedOutside
                                   : BitmapRender::RoundedOutside::SubtlePaperOutside;
    } else {
      options.roundedOutside = roundedCornerBackdropIsDither ? BitmapRender::RoundedOutside::SparseInkAlignedOutside
                                                             : BitmapRender::RoundedOutside::PaperOutside;
    }
    if (ImageRender::create(renderer, path).render(x, y, width, height, options)) return;
  }
  drawPlaceholder(renderer, titleFor(book), x, y, width, height, font);
}

void drawDitherRect(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  const int x1 = std::max(0, x);
  const int y1 = std::max(0, y);
  const int x2 = std::min(renderer.getScreenWidth(), x + width);
  const int y2 = std::min(renderer.getScreenHeight(), y + height);
  for (int py = (y1 + 1) & ~1; py < y2; py += 2) {
    for (int px = (x1 + 1) & ~1; px < x2; px += 2) renderer.drawPixel(px, py, true);
  }
}

void drawMockProgress(const GfxRenderer& renderer, const int x, const int y, const int width, const float progress) {
  if (width <= 0) return;
  constexpr int barHeight = 6;
  renderer.rectangle.fill(x, y, width, barHeight, false);
  renderer.rectangle.render(x, y, width, barHeight, true);
  renderer.rectangle.fill(x, y, static_cast<int>(width * progress + 0.5f), barHeight, true);
}

}  // namespace widget::support
