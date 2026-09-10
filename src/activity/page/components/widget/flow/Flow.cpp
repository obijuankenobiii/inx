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
  const int statsX = 30;
  const int statsY = carouselY + Carousel::kHeight + 20;
  renderer.line.render(0, carouselY + Carousel::kHeight + 10, width,
                       carouselY + Carousel::kHeight + 10, true);
  const std::string title = renderer.text.truncate(MONTSERRAT_18_FONT_ID, support::titleFor(currentBook).c_str(),
                                                     width - 60, EpdFontFamily::BOLD);
  renderer.text.render(MONTSERRAT_16_FONT_ID, statsX, statsY, title.c_str(), true, EpdFontFamily::BOLD);
  const int authorY = statsY + renderer.text.getLineHeight(MONTSERRAT_18_FONT_ID);
  renderer.text.render(MONTSERRAT_12_FONT_ID, statsX, authorY, currentBook.author.c_str());

  BookReadingStats stats;
  const bool hasStats = loadBookStats(support::cachePathFor(currentBook).c_str(), stats);
  const float progress = hasStats ? stats.progressPercent : currentBook.progress * 100.0f;
  if (progress >= 0.0f) {
    const int barY = authorY + renderer.text.getLineHeight(MONTSERRAT_12_FONT_ID) + 20;
    const int barW = (width - 60) / 2;
    renderer.rectangle.fill(statsX, barY, barW, 6, false);
    renderer.rectangle.render(statsX, barY, barW, 6, true);
    if (progress > 0.0f) renderer.rectangle.fill(statsX, barY, static_cast<int>(barW * progress / 100.0f + 0.5f), 6);
    char percent[8];
    std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(progress + 0.5f));
    renderer.text.render(MONTSERRAT_12_FONT_ID, statsX + barW + 12, barY - 13, percent);
  }

  if (!hasStats) return;
  char buffer[32];
  const int valueY = authorY + 100;
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
  const int carouselHeight = std::min(Carousel::kHeight, std::max(1, height - 5));
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

  const int statsX = 30;
  const int statsY = carouselY + Carousel::kHeight + 25;
  renderer.line.render(0, carouselY + Carousel::kHeight + 10, width,
                       carouselY + Carousel::kHeight + 10, true);
  renderer.text.render(MONTSERRAT_18_FONT_ID, statsX, statsY, titles[0], true, EpdFontFamily::BOLD);
  const int authorY = statsY + renderer.text.getLineHeight(MONTSERRAT_18_FONT_ID) - 5;
  renderer.text.render(MONTSERRAT_12_FONT_ID, statsX, authorY, "F. Scott Fitzgerald", true);
  const int barY = authorY + renderer.text.getLineHeight(MONTSERRAT_12_FONT_ID) + 20;
  drawMockProgress(renderer, statsX, barY, (width - 60) / 2, progress[0]);
  renderer.text.render(MONTSERRAT_12_FONT_ID, statsX + (width - 60) / 2 + 12, barY - 13, "42%", true);
  const int valueY = authorY + 100;
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
