#include "Flow.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "../Carousel.h"
#include "../WidgetRender.h"
#include "state/Statistics.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/UiLayout.h"

namespace widget::flow {
namespace {

std::string formatTime(const uint32_t milliseconds) {
  char buffer[32];
  const float hours = milliseconds / (1000.0f * 3600.0f);
  if (hours >= 1.0f) {
    std::snprintf(buffer, sizeof(buffer), "%.1f h", hours);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%u m", milliseconds / (1000U * 60U));
  }
  return buffer;
}

void drawMockProgress(const GfxRenderer& renderer, const int x, const int y, const int width, const float progress) {
  support::drawMockProgress(renderer, x, y, width, progress);
}

void drawProgress(const GfxRenderer& renderer, const int x, const int y, const int width, const int percentage) {
  constexpr int barHeight = 5;
  constexpr int percentageFont = MONTSERRAT_8_FONT_ID;
  constexpr int gap = 8;
  const int barWidth = std::max(1, width / 2);
  const std::string percentageText = std::to_string(percentage) + "%";
  renderer.rectangle.render(x, y, barWidth, barHeight, true);
  renderer.rectangle.fill(x + 1, y + 1, std::max(1, barWidth - 2), barHeight - 2, false);
  if (percentage > 0) {
    renderer.rectangle.fill(x + 1, y + 1, std::max(1, (barWidth - 2) * percentage / 100), barHeight - 2, true);
  }
  const int percentageY = y + (barHeight - renderer.text.getLineHeight(percentageFont)) / 2;
  renderer.text.render(percentageFont, x + barWidth + gap, percentageY, percentageText.c_str(), true);
}

}  // namespace

void Flow::render(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                  const int selectedIndex) {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) {
    renderer.text.centered(MONTSERRAT_12_FONT_ID, y + height / 2, "No recent books");
    return;
  }

  const int currentIndex = std::max(0, std::min(selectedIndex, static_cast<int>(books.size()) - 1));
  const int carouselY = y + 5;
  Carousel carousel(renderer);
  carousel.render(currentIndex, x, carouselY, width, Carousel::kHeight);

  const RecentBook& currentBook = books[static_cast<size_t>(currentIndex)];
  constexpr int statsX = 20;
  const int statsY = carouselY + Carousel::kHeight + 20;
  renderer.line.render(0, carouselY + Carousel::kHeight + 10, width,
                       carouselY + Carousel::kHeight + 10, true);
  constexpr int titleFont = MONTSERRAT_16_FONT_ID;
  constexpr int authorFont = MONTSERRAT_12_FONT_ID;
  const int textWidth = std::max(1, width - statsX * 2);
  const std::string title = renderer.text.truncate(titleFont, support::titleFor(currentBook).c_str(), textWidth,
                                                     EpdFontFamily::BOLD);
  renderer.text.render(titleFont, statsX, statsY, title.c_str(), true, EpdFontFamily::BOLD);
  const int titleLineHeight = renderer.text.getLineHeight(titleFont);
  int textBottom = statsY + titleLineHeight;
  if (!currentBook.author.empty()) {
    const int authorY = textBottom + 6;
    renderer.text.renderGray(authorFont, statsX, authorY, currentBook.author.c_str(), true,
                             EpdFontFamily::REGULAR);
    textBottom = authorY + renderer.text.getLineHeight(authorFont);
  }

  BookReadingStats stats;
  const bool hasStats = loadBookStats(support::cachePathFor(currentBook).c_str(), stats);
  const float progress = hasStats ? stats.progressPercent : currentBook.progress * 100.0f;
  const int barY = textBottom + 10;
  if (progress >= 0.0f) {
    const int percentage = std::max(0, std::min(100, static_cast<int>(progress + 0.5f)));
    drawProgress(renderer, statsX, barY, width, percentage);
  }

  if (!hasStats) return;
  char buffer[32];
  const int valueY = progress >= 0.0f ? barY + 5 + 20 : textBottom + 30;
  renderer.text.render(MONTSERRAT_16_FONT_ID, statsX, valueY, formatTime(stats.totalReadingTimeMs).c_str(), true,
                       EpdFontFamily::BOLD);
  renderer.text.render(MONTSERRAT_10_FONT_ID, statsX, valueY + 40, "Reading Time", true);
  std::snprintf(buffer, sizeof(buffer), "%u", stats.totalPagesRead);
  renderer.text.render(MONTSERRAT_16_FONT_ID, width / 2, valueY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(MONTSERRAT_10_FONT_ID, width / 2, valueY + 40, "Pages", true);
  const int row2Y = valueY + 95;
  std::snprintf(buffer, sizeof(buffer), "%u", stats.totalChaptersRead);
  renderer.text.render(MONTSERRAT_16_FONT_ID, statsX, row2Y, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(MONTSERRAT_10_FONT_ID, statsX, row2Y + 40, "Chapters", true);
  if (stats.avgPageTimeMs > 0) {
    std::snprintf(buffer, sizeof(buffer), "%u s", stats.avgPageTimeMs / 1000);
  } else {
    std::snprintf(buffer, sizeof(buffer), "-");
  }
  renderer.text.render(MONTSERRAT_16_FONT_ID, width / 2, row2Y, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(MONTSERRAT_10_FONT_ID, width / 2, row2Y + 40, "Average / Page", true);
}

void Flow::preview(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  static constexpr const char* titles[] = {"The Great Gatsby", "A Brief History", "Recent Book"};
  static constexpr float progress[] = {0.42f, 0.68f, 0.18f};
  const int carouselY = y + 5;
  const int carouselHeight = Carousel::kHeight;
  support::drawDitherRect(renderer, x, carouselY, width, carouselHeight);

  const int centerWidth = std::min(width, UiLayout::FLOW_CAROUSEL_CENTER_WIDTH);
  const int centerHeight = std::min(carouselHeight, UiLayout::FLOW_CAROUSEL_CENTER_HEIGHT);
  const int centerX = x + (width - centerWidth) / 2;
  const int centerY = carouselY + (carouselHeight - centerHeight) / 2 + 4;
  const int sideWidth = centerWidth * UiLayout::FLOW_CAROUSEL_SIDE_SCALE_PERCENT / 100;
  const int sideHeight = centerHeight * UiLayout::FLOW_CAROUSEL_SIDE_SCALE_PERCENT / 100;
  const int sideY = centerY + (centerHeight - sideHeight) / 2;
  support::drawPlaceholder(renderer, titles[1], centerX - sideWidth - UiLayout::FLOW_CAROUSEL_CARD_GAP, sideY,
                           sideWidth, sideHeight, MONTSERRAT_10_FONT_ID);
  support::drawPlaceholder(renderer, titles[0], centerX, centerY, centerWidth, centerHeight, MONTSERRAT_14_FONT_ID);
  support::drawPlaceholder(renderer, titles[2], centerX + centerWidth + UiLayout::FLOW_CAROUSEL_CARD_GAP, sideY,
                           sideWidth, sideHeight, MONTSERRAT_10_FONT_ID);

  constexpr int statsX = 20;
  const int statsY = carouselY + Carousel::kHeight + 20;
  renderer.line.render(0, carouselY + Carousel::kHeight + 10, width,
                       carouselY + Carousel::kHeight + 10, true);
  constexpr int titleFont = MONTSERRAT_16_FONT_ID;
  constexpr int authorFont = MONTSERRAT_12_FONT_ID;
  const int textWidth = std::max(1, width - statsX * 2);
  const std::string title = renderer.text.truncate(titleFont, titles[0], textWidth, EpdFontFamily::BOLD);
  renderer.text.render(titleFont, statsX, statsY, title.c_str(), true, EpdFontFamily::BOLD);
  const int authorY = statsY + renderer.text.getLineHeight(titleFont) + 6;
  renderer.text.renderGray(authorFont, statsX, authorY, "F. Scott Fitzgerald", true, EpdFontFamily::REGULAR);
  const int barY = authorY + renderer.text.getLineHeight(authorFont) + 10;
  drawProgress(renderer, statsX, barY, width, 42);
  const int valueY = barY + 5 + 20;
  renderer.text.render(MONTSERRAT_16_FONT_ID, statsX, valueY, "1.2 h", true, EpdFontFamily::BOLD);
  renderer.text.render(MONTSERRAT_10_FONT_ID, statsX, valueY + 40, "Reading Time", true);
  renderer.text.render(MONTSERRAT_16_FONT_ID, width / 2, valueY, "128", true, EpdFontFamily::BOLD);
  renderer.text.render(MONTSERRAT_10_FONT_ID, width / 2, valueY + 40, "Pages", true);
  renderer.text.render(MONTSERRAT_16_FONT_ID, statsX, valueY + 95, "6", true, EpdFontFamily::BOLD);
  renderer.text.render(MONTSERRAT_10_FONT_ID, statsX, valueY + 135, "Chapters", true);
  renderer.text.render(MONTSERRAT_16_FONT_ID, width / 2, valueY + 95, "42 s", true, EpdFontFamily::BOLD);
  renderer.text.render(MONTSERRAT_10_FONT_ID, width / 2, valueY + 135, "Average / Page", true);
}

}  // namespace widget::flow
