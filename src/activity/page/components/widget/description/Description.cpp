#include "Description.h"

#include <Epub/BookMetadataCache.h>
#include <GfxRenderer.h>
#include <BitmapRender.h>

#include <algorithm>
#include <string>

#include "../WidgetRender.h"
#include "../carousel/Carousel.h"
#include "images/CarretFilled.h"
#include "state/RecentBooks.h"
#include "system/Fonts.h"

namespace widget::description {
namespace {

constexpr int kMargin = 20;
constexpr int kGap = 24;
constexpr int kCarouselTopPadding = 16;
constexpr int kCarouselBottomPadding = 16;
constexpr int kCarouselCardGap = 12;
constexpr int kTimelineWidth = 5;
constexpr int kTimelineVerticalInset = 40;
constexpr int kTimelineGap = 10;
constexpr int kCaretSize = 40;
// The rotated 40 px caret has about 10 px of transparent space on its right
// side; align the visible caret edge, not its bitmap bounds, to the timeline.
constexpr int kCaretInkRightOffset = 27;
constexpr int kTitleTop = 20;
constexpr int kAuthorGap = 6;
constexpr int kDescriptionGap = 20;

std::string descriptionFor(const RecentBook& book) {
  BookMetadataCache metadata(support::cachePathFor(book));
  if (!metadata.load()) return {};
  return metadata.coreMetadata.description;
}

void renderDetails(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                   const int height) {
  if (width <= 0 || height <= 0) return;

  constexpr int titleFont = MONTSERRAT_16_FONT_ID;
  constexpr int authorFont = MONTSERRAT_12_FONT_ID;
  const int titleWidth = std::max(1, width);
  const std::string title = renderer.text.truncate(titleFont, support::titleFor(book).c_str(), titleWidth,
                                                    EpdFontFamily::BOLD);
  renderer.text.render(titleFont, x, y + kTitleTop, title.c_str(), true, EpdFontFamily::BOLD);

  int textBottom = y + kTitleTop + renderer.text.getLineHeight(titleFont);
  if (!book.author.empty()) {
    const int authorY = textBottom + kAuthorGap;
    const std::string author = renderer.text.truncate(authorFont, book.author.c_str(), width,
                                                       EpdFontFamily::REGULAR);
    renderer.text.renderGray(authorFont, x, authorY, author.c_str(), true, EpdFontFamily::REGULAR);
    textBottom = authorY + renderer.text.getLineHeight(authorFont);
  }

  const int descriptionY = textBottom + kDescriptionGap;
  carousel::Carousel::renderDescription(renderer, descriptionFor(book), x, descriptionY, width,
                                         std::max(0, y + height - descriptionY));
}

void renderVerticalCarousel(GfxRenderer& renderer, const std::vector<RecentBook>& books, const int selectedIndex,
                            const int x, const int y, const int width, const int height) {
  if (books.empty() || width <= 0 || height <= 0) return;

  const int firstY = y + kCarouselTopPadding;
  const int availableHeight = std::max(1, height - kCarouselTopPadding - kCarouselBottomPadding);
  const int current = ((selectedIndex % static_cast<int>(books.size())) + static_cast<int>(books.size())) %
                      static_cast<int>(books.size());
  // The recent list is circular. Always draw the same number of cards and
  // wrap their source index so moving down never makes the stack shorter.
  const int cardCount = std::min(3, static_cast<int>(books.size()));
  // Keep the card geometry fixed as selection moves. With three or more
  // books, this shows two complete cards and a half-height peek; shorter
  // lists simply leave the unused part empty instead of resizing the cards.
  const int cardHeight = std::max(24, (availableHeight - kCarouselCardGap * 2) * 2 / 5);
  const int cardWidth = std::max(24, std::min(width - kMargin, cardHeight * 2 / 3));
  // The timeline is the divider immediately to the left of the description
  // panel, rather than a line beside the thumbnail itself.
  const int timelineX = x + width;
  const int cardAreaRight = timelineX - kTimelineGap - kCaretSize;
  const int cardAreaWidth = std::max(cardWidth, cardAreaRight - (x + kMargin));
  const int cardX = x + kMargin + std::max(0, (cardAreaWidth - cardWidth) / 2);
  // Shorten the divider by 40 px overall and keep the remaining line centered
  // in the carousel area.
  const int timelineY = firstY + kTimelineVerticalInset / 2;
  const int timelineHeight = std::max(1, availableHeight - kTimelineVerticalInset);
  renderer.rectangle.fill(timelineX, timelineY, kTimelineWidth, timelineHeight,
                          static_cast<int>(GfxRenderer::FillTone::Ink));
  for (int offset = 0; offset < cardCount; ++offset) {
    const int bookIndex = (current + offset) % static_cast<int>(books.size());
    const int cardY = firstY + offset * (cardHeight + kCarouselCardGap);
    const int visibleHeight = cardCount >= 3 && offset == 2 ? cardHeight / 2 : cardHeight;
    carousel::Carousel::renderThumbnail(renderer, books[static_cast<size_t>(bookIndex)], cardX, cardY, cardWidth,
                                         visibleHeight, true, cardCount >= 3 && offset == 2);
  }

  const int markerY = firstY + (cardHeight - kCaretSize) / 2;
  const int caretX = timelineX - kCaretInkRightOffset;
  renderer.bitmap.iconScaled(CarretFilled, caretX, markerY, kCaretSize, kCaretSize, kCaretSize, kCaretSize,
                             BitmapRender::Orientation::Rotate270CW);
  const std::string index = std::to_string(current);
  const int indexWidth = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, index.c_str());
  renderer.text.render(MONTSERRAT_10_FONT_ID, caretX - kTimelineGap - indexWidth,
                       markerY + (kCaretSize - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2,
                       index.c_str(), true, EpdFontFamily::BOLD);
}

}  // namespace

void Description::render(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                         const int selectedIndex) {
  if (width <= 0 || height <= 0) return;
  renderer.rectangle.fill(x, y, width, height, false);

  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) {
    renderer.text.centered(systemFontId(), y + height / 2, "No recent");
    return;
  }

  const int current = ((selectedIndex % static_cast<int>(books.size())) + static_cast<int>(books.size())) %
                      static_cast<int>(books.size());
  const int carouselWidth = std::max(1, (width - kGap) * 45 / 100);
  renderVerticalCarousel(renderer, books, current, x, y, carouselWidth, height);
  const int detailsX = x + carouselWidth + kGap;
  renderDetails(renderer, books[static_cast<size_t>(current)], detailsX, y, width - carouselWidth - kGap - kMargin,
                height);
}

void Description::preview(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  if (width <= 0 || height <= 0) return;
  renderer.rectangle.fill(x, y, width, height, false);

  const int carouselWidth = std::max(1, (width - kGap) * 45 / 100);
  const RecentBook placeholder("", "", "Book title", "Author", 0.65f);
  const std::vector<RecentBook> previewBooks = {placeholder, placeholder, placeholder};
  renderVerticalCarousel(renderer, previewBooks, 0, x, y, carouselWidth, height);

  const int detailsX = x + carouselWidth + kGap;
  const int detailsWidth = std::max(1, width - carouselWidth - kGap - kMargin);
  constexpr int titleFont = MONTSERRAT_16_FONT_ID;
  constexpr int authorFont = MONTSERRAT_12_FONT_ID;
  const std::string title = renderer.text.truncate(titleFont, "Book title", detailsWidth, EpdFontFamily::BOLD);
  renderer.text.render(titleFont, detailsX, y + kTitleTop, title.c_str(), true, EpdFontFamily::BOLD);
  const int authorY = y + kTitleTop + renderer.text.getLineHeight(titleFont) + kAuthorGap;
  renderer.text.renderGray(authorFont, detailsX, authorY, "Author", true, EpdFontFamily::REGULAR);
  const int descriptionY = authorY + renderer.text.getLineHeight(authorFont) + kDescriptionGap;
  carousel::Carousel::renderDescription(renderer, "A recent book description appears here.", detailsX, descriptionY,
                                         detailsWidth, std::max(0, y + height - descriptionY));
}

}  // namespace widget::description
