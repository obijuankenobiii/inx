#include "Dashboard.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "../carousel/Carousel.h"
#include "../WidgetRender.h"
#include "images/CarretFilled.h"
#include "state/RecentBooks.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"

namespace widget::dashboard {
namespace {

constexpr int kInnerPadding = 20;
constexpr int kSplitGap = 1;
constexpr int kTitleAuthorGap = 4;
constexpr int kProgressGap = 10;
constexpr int kCaretSize = 40;
constexpr int kCaretGap = 5;

struct ThumbnailBounds {
  int x;
  int y;
  int width;
  int height;
};

ThumbnailBounds recentThumbnailBounds(const int x, const int y, const int height) {
  const int thumbnailHeight = std::max(40, std::min(300, height - kInnerPadding * 2));
  const int thumbnailWidth = std::max(40, std::min(200, thumbnailHeight * 2 / 3));
  return {x + kInnerPadding, y + (height - thumbnailHeight) / 2, thumbnailWidth, thumbnailHeight};
}

void titleLines(const GfxRenderer& renderer, const std::string& value, const int font, const int width,
                std::string& first, std::string& second) {
  first.clear();
  second.clear();
  if (value.empty()) return;
  if (renderer.text.getWidth(font, value.c_str(), EpdFontFamily::BOLD) <= width) {
    first = value;
    return;
  }

  size_t split = value.find(' ');
  size_t best = std::string::npos;
  while (split != std::string::npos) {
    if (renderer.text.getWidth(font, value.substr(0, split).c_str(), EpdFontFamily::BOLD) > width) break;
    best = split;
    split = value.find(' ', split + 1);
  }
  if (best == std::string::npos) {
    first = renderer.text.truncate(font, value.c_str(), width, EpdFontFamily::BOLD);
    return;
  }
  first = value.substr(0, best);
  while (best < value.size() && value[best] == ' ') ++best;
  second = renderer.text.truncate(font, value.substr(best).c_str(), width, EpdFontFamily::BOLD);
}

void renderProgress(GfxRenderer& renderer, const int x, const int y, const int width, const int percentage) {
  if (width <= 0) return;
  constexpr int barHeight = 5;
  constexpr int percentageFont = MONTSERRAT_8_FONT_ID;
  constexpr int gap = 8;
  const std::string text = std::to_string(percentage) + "%";
  const int percentageWidth = renderer.text.getWidth(percentageFont, text.c_str());
  const int barWidth = std::max(1, width - percentageWidth - gap);
  renderer.rectangle.render(x, y, barWidth, barHeight, true);
  renderer.rectangle.fill(x + 1, y + 1, std::max(1, barWidth - 2), barHeight - 2, false);
  if (percentage > 0) {
    renderer.rectangle.fill(x + 1, y + 1, std::max(1, (barWidth - 2) * percentage / 100), barHeight - 2, true);
  }
  const int textY = y + (barHeight - renderer.text.getLineHeight(percentageFont)) / 2;
  renderer.text.render(percentageFont, x + barWidth + gap, textY, text.c_str(), true);
}

void renderRecent(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                  const int height) {
  if (width <= 0 || height <= 0) return;

  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  const ThumbnailBounds thumbnail = recentThumbnailBounds(x, y, height);
  const int thumbnailX = thumbnail.x;
  const int thumbnailY = thumbnail.y;
  const int thumbnailWidth = thumbnail.width;
  const int thumbnailHeight = thumbnail.height;
  renderer.rectangle.fill(thumbnailX + 6, thumbnailY + 6, thumbnailWidth, thumbnailHeight,
                          static_cast<int>(GfxRenderer::FillTone::Gray));
  support::drawThumbnail(renderer, book, thumbnailX, thumbnailY, thumbnailWidth, thumbnailHeight,
                          MONTSERRAT_10_FONT_ID, false);
  renderer.rectangle.render(thumbnailX, thumbnailY, thumbnailWidth, thumbnailHeight, true,
                            SETTINGS.bitmapRoundedCorners != 0, SETTINGS.bitmapRoundedCorners == 2);

  const int contentX = thumbnailX + thumbnailWidth + 18;
  const int contentWidth = std::max(1, x + width - contentX - kInnerPadding);
  std::string first;
  std::string second;
  titleLines(renderer, support::titleFor(book), font, contentWidth, first, second);
  const int titleCount = second.empty() ? 1 : 2;
  const int titleBlockHeight = titleCount * lineHeight + (book.author.empty() ? 0 : lineHeight + kTitleAuthorGap);
  const int titleY = y + std::max(kInnerPadding / 2, (height - titleBlockHeight) / 2 - 12);
  renderer.text.render(font, contentX, titleY, first.c_str(), true, EpdFontFamily::BOLD);
  if (!second.empty()) renderer.text.render(font, contentX, titleY + lineHeight, second.c_str(), true, EpdFontFamily::BOLD);
  if (!book.author.empty()) {
    renderer.text.render(font, contentX, titleY + titleCount * lineHeight + kTitleAuthorGap, book.author.c_str(), true,
                         EpdFontFamily::REGULAR);
  }

  const int percentage = std::max(0, std::min(100, static_cast<int>(book.progress * 100.0f + 0.5f)));
  const int progressY = std::min(y + height - kInnerPadding - 5,
                                 titleY + titleBlockHeight + kProgressGap);
  renderProgress(renderer, contentX, progressY, contentWidth, percentage);
}

void drawRecentSelectionCaret(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  const ThumbnailBounds thumbnail = recentThumbnailBounds(x, y, height);
  renderer.bitmap.icon(CarretFilled, thumbnail.x + (thumbnail.width - kCaretSize) / 2,
                       thumbnail.y + thumbnail.height + kCaretGap, kCaretSize, kCaretSize);
}

void renderRecentPreview(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  RecentBook book("", "", "Book title", "Author", 0.65f);
  renderRecent(renderer, book, x, y, width, height);
}

}  // namespace

void Dashboard::render(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                       const int selectedIndex, const bool carouselFocused) {
  if (width <= 0 || height <= 0) return;
  renderer.rectangle.fill(x, y, width, height, false);

  const int topHeight = std::max(1, (height - kSplitGap) / 2);
  const int bottomY = y + topHeight + kSplitGap;
  const int bottomHeight = std::max(1, height - topHeight - kSplitGap);
  const auto& books = RECENT_BOOKS.getBooks();
  if (!books.empty()) {
    renderRecent(renderer, books.front(), x, y, width, topHeight);
  } else {
    renderer.text.centered(systemFontId(), y + topHeight / 2, "No recent");
  }

  renderer.line.render(x, bottomY - 1, x + width, bottomY - 1, true, LineRender::Style::Dotted);
  widget::carousel::Carousel::renderRemaining(renderer, x, bottomY, width, bottomHeight, selectedIndex,
                                               carouselFocused);
  if (!books.empty() && !carouselFocused) drawRecentSelectionCaret(renderer, x, y, width, topHeight);
}

void Dashboard::preview(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                        const bool carouselFocused) {
  if (width <= 0 || height <= 0) return;
  renderer.rectangle.fill(x, y, width, height, false);
  const int topHeight = std::max(1, (height - kSplitGap) / 2);
  const int bottomY = y + topHeight + kSplitGap;
  const int bottomHeight = std::max(1, height - topHeight - kSplitGap);
  renderRecentPreview(renderer, x, y, width, topHeight);
  renderer.line.render(x, bottomY - 1, x + width, bottomY - 1, true, LineRender::Style::Dotted);
  widget::carousel::Carousel::previewRemaining(renderer, x, bottomY, width, bottomHeight, carouselFocused);
  if (!carouselFocused) drawRecentSelectionCaret(renderer, x, y, width, topHeight);
}

}  // namespace widget::dashboard
