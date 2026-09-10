#include "Carousel.h"

#include <Epub/BookMetadataCache.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

#include "../WidgetRender.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/UiLayout.h"

namespace widget::carousel {
namespace {

struct CardBounds {
  int x;
  int y;
  int width;
  int height;
};

std::string thumbnailPath(const RecentBook& book) {
  const std::string cache = support::cachePathFor(book);
  char path[192] = {};
  for (const char* extension : {"thumb.jpg", "thumb.png", "thumb.bmp"}) {
    std::snprintf(path, sizeof(path), "%s/%s", cache.c_str(), extension);
    if (SdMan.exists(path)) return path;
  }
  return {};
}

CardBounds leftCardBounds(const RecentBook& book, const int cardX, const int y, const int width,
                         const int height) {
  const int contentHeight = std::max(24, height - UiLayout::CAROUSEL_TOP_PADDING - UiLayout::CAROUSEL_BOTTOM_PADDING);
  int sourceWidth = 2;
  int sourceHeight = 3;
  const std::string path = thumbnailPath(book);
  if (!path.empty()) {
    int detectedWidth = 0;
    int detectedHeight = 0;
    if (ImageRender::getDimensions(path, &detectedWidth, &detectedHeight) && detectedWidth > 0 && detectedHeight > 0) {
      sourceWidth = detectedWidth;
      sourceHeight = detectedHeight;
    }
  }

  const int naturalWidth = std::max(24, static_cast<int>(std::lround(
                                             static_cast<float>(contentHeight) * sourceWidth / sourceHeight)));
  const int maxCardWidth = std::max(
      24, (width - UiLayout::CAROUSEL_LEFT_CARD_MARGIN - UiLayout::CAROUSEL_LEFT_CARD_GAP * 2) * 9 / 20);
  const int cardWidth = std::min(naturalWidth, maxCardWidth);
  const int cardHeight = std::max(
      24, std::min(contentHeight, static_cast<int>(std::lround(
                                      static_cast<float>(cardWidth) * sourceHeight / sourceWidth))));
  return {cardX, y + height - UiLayout::CAROUSEL_BOTTOM_PADDING - cardHeight, cardWidth, cardHeight};
}

void renderProgress(GfxRenderer& renderer, const int x, const int y, const int width, const int percentage) {
  constexpr int barHeight = 5;
  constexpr int percentageFont = MONTSERRAT_8_FONT_ID;
  constexpr int gap = 8;
  // The bar is exactly half of the bottom container width; the text begins
  // after it and remains inside the container's right margin.
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

std::string descriptionFor(const RecentBook& book) {
  BookMetadataCache metadata(support::cachePathFor(book));
  if (!metadata.load()) return {};
  return metadata.coreMetadata.description;
}

struct DescriptionRun {
  std::string text;
  EpdFontFamily::Style style;
};

void appendUtf8(std::string& output, const uint32_t codepoint) {
  if (codepoint <= 0x7F) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

bool appendHtmlEntity(const std::string& entity, std::string& output) {
  if (entity == "amp") output += '&';
  else if (entity == "lt") output += '<';
  else if (entity == "gt") output += '>';
  else if (entity == "quot") output += '"';
  else if (entity == "apos" || entity == "#39") output += '\'';
  else if (entity == "nbsp") output += ' ';
  else if (entity == "ndash") appendUtf8(output, 0x2013);
  else if (entity == "mdash") appendUtf8(output, 0x2014);
  else if (entity == "hellip") appendUtf8(output, 0x2026);
  else if (entity == "ldquo") appendUtf8(output, 0x201C);
  else if (entity == "rdquo") appendUtf8(output, 0x201D);
  else if (entity == "lsquo") appendUtf8(output, 0x2018);
  else if (entity == "rsquo") appendUtf8(output, 0x2019);
  else if (entity.size() > 1 && entity[0] == '#') {
    char* end = nullptr;
    const int base = entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X') ? 16 : 10;
    const char* number = entity.c_str() + (base == 16 ? 2 : 1);
    const unsigned long codepoint = std::strtoul(number, &end, base);
    if (!end || *end != '\0' || codepoint > 0x10FFFF) return false;
    appendUtf8(output, static_cast<uint32_t>(codepoint));
  } else {
    return false;
  }
  return true;
}

EpdFontFamily::Style descriptionStyle(const bool bold, const bool italic) {
  // Descriptions intentionally use regular weight for <b>/<strong>; keep
  // italic emphasis when a bold-italic span is encountered.
  (void)bold;
  if (italic) return EpdFontFamily::ITALIC;
  return EpdFontFamily::REGULAR;
}

void appendDescriptionRun(std::vector<DescriptionRun>& runs, const std::string& text,
                          const EpdFontFamily::Style style) {
  if (text.empty()) return;
  if (!runs.empty() && runs.back().style == style) {
    runs.back().text += text;
  } else {
    runs.push_back({text, style});
  }
}

std::vector<DescriptionRun> parseDescription(const std::string& rawDescription) {
  std::vector<DescriptionRun> runs;
  std::string text;
  bool bold = false;
  bool italic = false;
  const auto flush = [&] {
    appendDescriptionRun(runs, text, descriptionStyle(bold, italic));
    text.clear();
  };

  for (size_t i = 0; i < rawDescription.size();) {
    if (rawDescription[i] == '<') {
      const size_t close = rawDescription.find('>', i + 1);
      if (close == std::string::npos) {
        text.push_back(rawDescription[i++]);
        continue;
      }
      std::string tag = rawDescription.substr(i + 1, close - i - 1);
      size_t tagStart = 0;
      while (tagStart < tag.size() && std::isspace(static_cast<unsigned char>(tag[tagStart]))) ++tagStart;
      const bool closing = tagStart < tag.size() && tag[tagStart] == '/';
      if (closing) ++tagStart;
      while (tagStart < tag.size() && std::isspace(static_cast<unsigned char>(tag[tagStart]))) ++tagStart;
      size_t tagEnd = tagStart;
      while (tagEnd < tag.size() && std::isalpha(static_cast<unsigned char>(tag[tagEnd]))) ++tagEnd;
      std::string name = tag.substr(tagStart, tagEnd - tagStart);
      for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      const bool lineBreak = name == "br" || name == "p" || name == "div" || name == "li";

      flush();
      if (lineBreak && (name == "br" || closing)) appendDescriptionRun(runs, "\n", descriptionStyle(bold, italic));
      if (name == "b" || name == "strong") bold = !closing;
      if (name == "i" || name == "em") italic = !closing;
      i = close + 1;
    } else if (rawDescription[i] == '&') {
      const size_t semi = rawDescription.find(';', i + 1);
      if (semi != std::string::npos && appendHtmlEntity(rawDescription.substr(i + 1, semi - i - 1), text)) {
        i = semi + 1;
      } else {
        text.push_back(rawDescription[i++]);
      }
    } else {
      text.push_back(rawDescription[i++]);
    }
  }
  flush();
  return runs;
}

void renderDescription(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                       const std::string& rawDescription) {
  if (width <= 0 || height <= 0 || rawDescription.empty()) return;
  const int font = MONTSERRAT_12_FONT_ID;
  const int lineHeight = renderer.text.getLineHeight(font);
  const int maxLines = std::max(1, height / std::max(1, lineHeight));
  const std::vector<DescriptionRun> runs = parseDescription(rawDescription);
  std::vector<DescriptionRun> lineRuns;
  int lineWidth = 0;
  int lineIndex = 0;

  const auto flushLine = [&] {
    int drawX = x;
    for (const DescriptionRun& run : lineRuns) {
      renderer.text.render(font, drawX, y + lineIndex * lineHeight, run.text.c_str(), true, run.style);
      drawX += renderer.text.getWidth(font, run.text.c_str(), run.style);
    }
    lineRuns.clear();
    lineWidth = 0;
    ++lineIndex;
  };

  for (const DescriptionRun& run : runs) {
    size_t start = 0;
    while (start <= run.text.size() && lineIndex < maxLines) {
      const size_t newline = run.text.find('\n', start);
      const size_t end = newline == std::string::npos ? run.text.size() : newline;
      size_t tokenStart = start;
      while (tokenStart < end && lineIndex < maxLines) {
        while (tokenStart < end && run.text[tokenStart] == ' ') ++tokenStart;
        if (tokenStart >= end) break;
        size_t tokenEnd = run.text.find(' ', tokenStart);
        if (tokenEnd == std::string::npos || tokenEnd > end) tokenEnd = end;
        const std::string token = run.text.substr(tokenStart, tokenEnd - tokenStart);
        const int tokenWidth = renderer.text.getWidth(font, token.c_str(), run.style);
        if (!lineRuns.empty() && lineWidth + renderer.text.getSpaceWidth(font) + tokenWidth > width) {
          flushLine();
          if (lineIndex >= maxLines) break;
        }
        const std::string withSpace = lineRuns.empty() ? token : " " + token;
        lineRuns.push_back({withSpace, run.style});
        lineWidth += renderer.text.getWidth(font, withSpace.c_str(), run.style);
        tokenStart = tokenEnd;
      }
      if (newline != std::string::npos && lineIndex < maxLines) {
        flushLine();
        start = newline + 1;
      } else {
        start = run.text.size() + 1;
      }
    }
  }
  if (!lineRuns.empty() && lineIndex < maxLines) flushLine();
}

void renderCover(GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                 const int height) {
  if (width <= 0 || height <= 0) return;
  // Match inx-pro's thumbnail shadow: a 6 px offset gray block is drawn first,
  // then the cover and its normal border are drawn over it.
  renderer.rectangle.fill(x + 6, y + 6, width, height, static_cast<int>(GfxRenderer::FillTone::Gray));
  support::drawThumbnail(renderer, book, x, y, width, height, MONTSERRAT_10_FONT_ID, false);
  renderer.rectangle.render(x, y, width, height, true, SETTINGS.bitmapRoundedCorners != 0,
                            SETTINGS.bitmapRoundedCorners == 2);
}

void renderLeft(GfxRenderer& renderer, const std::vector<RecentBook>& books, const int index, const int x,
                const int y, const int width, const int height) {
  if (books.empty()) {
    renderer.text.centered(systemFontId(), y + height / 2, "No recent");
    return;
  }

  const int current = ((index % static_cast<int>(books.size())) + static_cast<int>(books.size())) %
                      static_cast<int>(books.size());
  const int visible = std::min(UiLayout::CAROUSEL_MAX_VISIBLE, static_cast<int>(books.size()));
  int cardX = x + UiLayout::CAROUSEL_LEFT_CARD_MARGIN;
  for (int offset = 0; offset < visible; ++offset) {
    const int bookIndex = (current + offset) % static_cast<int>(books.size());
    const CardBounds card = leftCardBounds(books[static_cast<size_t>(bookIndex)], cardX, y, width, height);
    if (card.x >= x + width) break;
    const int visibleWidth = std::min(card.width, x + width - card.x);
    if (visibleWidth <= 0) break;
    renderCover(renderer, books[static_cast<size_t>(bookIndex)], card.x, card.y, visibleWidth, card.height);
    cardX += card.width + UiLayout::CAROUSEL_LEFT_CARD_GAP;
  }
}

}  // namespace

void Carousel::render(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                      const int selectedIndex) {
  if (width <= 0 || height <= 0) return;
  renderer.rectangle.fill(x, y, width, height, false);
  renderLeft(renderer, RECENT_BOOKS.getBooks(), selectedIndex, x, y, width, height);
}

void Carousel::preview(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  if (width <= 0 || height <= 0) return;
  renderer.rectangle.fill(x, y, width, height, false);
  const RecentBook placeholder;
  const int cardHeight = std::max(24, height - UiLayout::CAROUSEL_TOP_PADDING - UiLayout::CAROUSEL_BOTTOM_PADDING);
  const CardBounds first = leftCardBounds(placeholder, x + UiLayout::CAROUSEL_LEFT_CARD_MARGIN, y, width, height);
  const int cardWidth = first.width;
  int cardX = x + UiLayout::CAROUSEL_LEFT_CARD_MARGIN;
  for (int offset = 0; offset < 3; ++offset) {
    const int cardY = y + height - UiLayout::CAROUSEL_BOTTOM_PADDING - cardHeight;
    const int visibleWidth = std::min(cardWidth, x + width - cardX);
    if (visibleWidth <= 0) break;
    renderCover(renderer, placeholder, cardX, cardY, visibleWidth, cardHeight);
    cardX += cardWidth + UiLayout::CAROUSEL_LEFT_CARD_GAP;
  }
}

void Carousel::renderBottom(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                            const int selectedIndex) {
  if (width <= 0 || height <= 0) return;
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty()) return;

  const int current = ((selectedIndex % static_cast<int>(books.size())) + static_cast<int>(books.size())) %
                      static_cast<int>(books.size());
  const RecentBook& book = books[static_cast<size_t>(current)];
  constexpr int marginX = 20;
  constexpr int marginTop = 20;
  const int titleFont = MONTSERRAT_16_FONT_ID;
  const int authorFont = MONTSERRAT_12_FONT_ID;
  const int textWidth = std::max(1, width - marginX * 2);
  const std::string title = renderer.text.truncate(titleFont, support::titleFor(book).c_str(), textWidth,
                                                    EpdFontFamily::BOLD);
  renderer.text.render(titleFont, x + marginX, y + marginTop, title.c_str(), true, EpdFontFamily::BOLD);

  const int titleLineHeight = renderer.text.getLineHeight(titleFont);
  int textBottom = y + marginTop + titleLineHeight;
  if (!book.author.empty()) {
    const int authorY = textBottom + 6;
    const std::string author = renderer.text.truncate(authorFont, book.author.c_str(), textWidth,
                                                       EpdFontFamily::REGULAR);
    renderer.text.renderGray(authorFont, x + marginX, authorY, author.c_str(), true, EpdFontFamily::REGULAR);
    textBottom = authorY + renderer.text.getLineHeight(authorFont);
  }

  const float progressPercent = book.progress * 100.0f;
  const int percentage = progressPercent < 0.0f
                             ? 0
                             : std::max(0, std::min(100, static_cast<int>(progressPercent + 0.5f)));
  const int barY = textBottom + 10;
  renderProgress(renderer, x + marginX, barY, width, percentage);
  constexpr int descriptionGap = 40;
  const int descriptionY = barY + 5 + descriptionGap;
  renderDescription(renderer, x + marginX, descriptionY, width - marginX * 2,
                    height - (descriptionY - y), descriptionFor(book));
}

void Carousel::previewBottom(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  if (width <= 0 || height <= 0) return;
  constexpr int marginX = 20;
  constexpr int marginTop = 20;
  const int titleFont = MONTSERRAT_16_FONT_ID;
  const int authorFont = MONTSERRAT_12_FONT_ID;
  const int textWidth = std::max(1, width - marginX * 2);
  const std::string title = renderer.text.truncate(titleFont, "Book title", textWidth, EpdFontFamily::BOLD);
  renderer.text.render(titleFont, x + marginX, y + marginTop, title.c_str(), true, EpdFontFamily::BOLD);
  const int authorY = y + marginTop + renderer.text.getLineHeight(titleFont) + 6;
  renderer.text.renderGray(authorFont, x + marginX, authorY, "Author", true, EpdFontFamily::REGULAR);
  const int barY = authorY + renderer.text.getLineHeight(authorFont) + 10;
  renderProgress(renderer, x + marginX, barY, width, 65);
  constexpr int descriptionGap = 40;
  const int descriptionY = barY + 5 + descriptionGap;
  renderDescription(renderer, x + marginX, descriptionY, width - marginX * 2, height - (descriptionY - y),
                    "A recent book description appears here.");
}

}  // namespace widget::carousel
