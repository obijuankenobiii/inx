#include "List.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "../WidgetRender.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"

namespace widget::list {
namespace {
constexpr int kRows = 5;

}  // namespace

int List::visibleStartIndex(const int selectedIndex, const int count) {
  return std::max(0, std::min(std::max(0, selectedIndex - (kRows - 1)), count - kRows));
}

namespace {

void renderRow(GfxRenderer& renderer, const RecentBook& book, const int x, const int rowY, const int rowH,
               const int width, const bool selected, const bool drawDivider) {
  constexpr int padX = 18;
  const int thumbH = std::max(48, rowH - 10);
  const int thumbW = std::min(88, thumbH * 170 / 250);
  if (selected) support::drawDitherRect(renderer, x, rowY, width, rowH);
  const int thumbY = rowY + (rowH - thumbH) / 2;
  support::drawThumbnail(renderer, book, x + padX, thumbY, thumbW, thumbH, MONTSERRAT_10_FONT_ID, false);
  const int textX = x + padX + thumbW + 14;
  const int textRight = x + width - padX;
  const int textW = std::max(40, textRight - textX);
  const int titleFont = MONTSERRAT_12_FONT_ID;
  const int authorFont = MONTSERRAT_8_FONT_ID;
  const int titleLineHeight = renderer.text.getLineHeight(titleFont);
  const int authorLineHeight = renderer.text.getLineHeight(authorFont);
  const std::string title = renderer.text.truncate(titleFont, support::titleFor(book).c_str(), textW,
                                                    EpdFontFamily::BOLD);
  const int titleY = rowY + 20;
  renderer.text.render(titleFont, textX, titleY, title.c_str(), true, EpdFontFamily::BOLD);
  int lastTextBottom = titleY + titleLineHeight;
  if (!book.author.empty()) {
    renderer.text.render(authorFont, textX, titleY + titleLineHeight + 4, book.author.c_str(), true,
                         EpdFontFamily::REGULAR);
    lastTextBottom = titleY + titleLineHeight + 4 + authorLineHeight;
  }
  float progress = book.progress;
  if (progress < 0.0f || progress > 1.0f) progress = 0.0f;
  constexpr int barH = 6;
  int barY = std::max(lastTextBottom + 20, titleY + titleLineHeight + 4);
  barY = std::min(barY, rowY + rowH - barH - 4);
  const int barX = textX;
  const int percentFont = MONTSERRAT_8_FONT_ID;
  char percent[12];
  std::snprintf(percent, sizeof(percent), "%.0f%%", static_cast<double>(progress * 100.0f));
  const int percentW = renderer.text.getWidth(percentFont, percent);
  const int barW = std::max(24, (textRight - percentW - 10 - barX) * 80 / 100);
  support::drawMockProgress(renderer, barX, barY, barW, progress);
  renderer.text.render(percentFont, barX + barW + 6, barY - 7, percent, true);
  if (drawDivider) {
    renderer.line.render(x + padX / 2, rowY + rowH - 1, x + width - padX / 2, rowY + rowH - 1, true,
                         LineRender::Style::Dotted);
  }
}

}  // namespace

void List::render(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                  const int selectedIndex, const bool drawSelection) {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) {
    renderer.text.centered(MONTSERRAT_12_FONT_ID, y + height / 2, "No recent books");
    return;
  }
  const int count = static_cast<int>(books.size());
  constexpr int listPadY = 6;
  const int listTop = y;
  const int contentHeight = std::max(1, height - listPadY);
  const int scrollOffset = visibleStartIndex(selectedIndex, count);
  const int visibleCount = std::min(kRows, count - scrollOffset);

  for (int slot = 0; slot < visibleCount; ++slot) {
    const int index = scrollOffset + slot;
    const int rowY = listTop + (contentHeight * slot) / kRows;
    const int rowBottom = listTop + (contentHeight * (slot + 1)) / kRows;
    const int rowH = std::max(56, rowBottom - rowY);
    renderRow(renderer, books[static_cast<size_t>(index)], x, rowY, rowH, width,
              drawSelection && index == selectedIndex, slot + 1 < visibleCount);
  }
}

void List::renderSelection(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                           const int selectedIndex) {
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(books.size())) return;
  const int count = static_cast<int>(books.size());
  const int scrollOffset = visibleStartIndex(selectedIndex, count);
  const int slot = selectedIndex - scrollOffset;
  const int visibleCount = std::min(kRows, count - scrollOffset);
  if (slot < 0 || slot >= visibleCount) return;

  constexpr int listPadY = 6;
  const int contentHeight = std::max(1, height - listPadY);
  const int rowY = y + (contentHeight * slot) / kRows;
  const int rowBottom = y + (contentHeight * (slot + 1)) / kRows;
  const int rowH = std::max(56, rowBottom - rowY);
  renderRow(renderer, books[static_cast<size_t>(selectedIndex)], x, rowY, rowH, width, true,
            slot + 1 < visibleCount);
}

void List::preview(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  static constexpr const char* titles[] = {"The Great Gatsby", "A Brief History", "Recent Book", "Another Book", "Book Five"};
  static constexpr float progress[] = {0.42f, 0.68f, 0.18f, 0.84f, 0.55f};
  constexpr int padX = 18;
  constexpr int listPadY = 6;
  const int contentHeight = std::max(1, height - listPadY);
  for (int index = 0; index < kRows; ++index) {
    const int rowY = y + (contentHeight * index) / kRows;
    const int rowBottom = y + (contentHeight * (index + 1)) / kRows;
    const int rowH = std::max(56, rowBottom - rowY);
    const int thumbH = std::max(48, rowH - 10);
    const int thumbW = std::min(88, thumbH * 170 / 250);
    const int thumbY = rowY + (rowH - thumbH) / 2;
    support::drawPlaceholder(renderer, titles[index], x + padX, thumbY, thumbW, thumbH, MONTSERRAT_10_FONT_ID);
    const int textX = x + padX + thumbW + 14;
    const int textRight = x + width - padX;
    const int textW = std::max(40, textRight - textX);
    constexpr int titleFont = MONTSERRAT_12_FONT_ID;
    constexpr int authorFont = MONTSERRAT_8_FONT_ID;
    const int titleLineHeight = renderer.text.getLineHeight(titleFont);
    const int authorLineHeight = renderer.text.getLineHeight(authorFont);
    const std::string title = renderer.text.truncate(titleFont, titles[index], textW, EpdFontFamily::BOLD);
    const int titleY = rowY + 20;
    renderer.text.render(titleFont, textX, titleY, title.c_str(), true, EpdFontFamily::BOLD);
    int lastTextBottom = titleY + titleLineHeight;
    renderer.text.render(authorFont, textX, titleY + titleLineHeight + 4, "Author Name", true,
                         EpdFontFamily::REGULAR);
    lastTextBottom = titleY + titleLineHeight + 4 + authorLineHeight;

    const int barH = 6;
    const int barY = std::min(std::max(lastTextBottom + 20, titleY + titleLineHeight + 4), rowY + rowH - barH - 4);
    const int percentFont = MONTSERRAT_8_FONT_ID;
    char percent[12];
    std::snprintf(percent, sizeof(percent), "%.0f%%", static_cast<double>(progress[index] * 100.0f));
    const int percentW = renderer.text.getWidth(percentFont, percent);
    const int barW = std::max(24, (textRight - percentW - 10 - textX) * 80 / 100);
    support::drawMockProgress(renderer, textX, barY, barW, progress[index]);
    renderer.text.render(percentFont, textX + barW + 6, barY - 7, percent, true);
    if (index + 1 < kRows) {
      renderer.line.render(x + padX / 2, rowY + rowH - 1, x + width - padX / 2, rowY + rowH - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

}  // namespace widget::list
