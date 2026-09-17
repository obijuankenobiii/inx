#include "HomeDescription.h"

#include <Epub/BookMetadataCache.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <utility>

#include "components/widget/WidgetRender.h"
#include "state/RecentBooks.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"
#include "system/UiLayout.h"

namespace {

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
  // Keep bold as regular weight, as requested; retain italic when present.
  (void)bold;
  return italic ? EpdFontFamily::ITALIC : EpdFontFamily::REGULAR;
}

void appendRun(std::vector<DescriptionLine::Run>& runs, const std::string& text,
               const EpdFontFamily::Style style) {
  if (text.empty()) return;
  if (!runs.empty() && runs.back().style == style) {
    runs.back().text += text;
  } else {
    DescriptionLine::Run run;
    run.text = text;
    run.style = style;
    runs.push_back(std::move(run));
  }
}

std::vector<DescriptionLine::Run> parseDescription(const std::string& raw) {
  std::vector<DescriptionLine::Run> runs;
  std::string text;
  bool bold = false;
  bool italic = false;
  const auto flush = [&] {
    appendRun(runs, text, descriptionStyle(bold, italic));
    text.clear();
  };

  for (size_t i = 0; i < raw.size();) {
    if (raw[i] == '<') {
      const size_t close = raw.find('>', i + 1);
      if (close == std::string::npos) {
        text.push_back(raw[i++]);
        continue;
      }
      std::string tag = raw.substr(i + 1, close - i - 1);
      size_t start = 0;
      while (start < tag.size() && std::isspace(static_cast<unsigned char>(tag[start]))) ++start;
      const bool closing = start < tag.size() && tag[start] == '/';
      if (closing) ++start;
      while (start < tag.size() && std::isspace(static_cast<unsigned char>(tag[start]))) ++start;
      size_t end = start;
      while (end < tag.size() && std::isalpha(static_cast<unsigned char>(tag[end]))) ++end;
      std::string name = tag.substr(start, end - start);
      for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      const bool lineBreak = name == "br" || name == "p" || name == "div" || name == "li";
      flush();
      if (lineBreak && (name == "br" || closing)) appendRun(runs, "\n", descriptionStyle(bold, italic));
      if (name == "b" || name == "strong") bold = !closing;
      if (name == "i" || name == "em") italic = !closing;
      i = close + 1;
    } else if (raw[i] == '&') {
      const size_t semi = raw.find(';', i + 1);
      if (semi != std::string::npos && appendHtmlEntity(raw.substr(i + 1, semi - i - 1), text)) {
        i = semi + 1;
      } else {
        text.push_back(raw[i++]);
      }
    } else {
      text.push_back(raw[i++]);
    }
  }
  flush();
  return runs;
}

}  // namespace

HomeDescription::HomeDescription(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                 std::function<void()> onBack)
    : Page("Description", renderer, mappedInput), bookPath_(std::move(bookPath)), onBack_(std::move(onBack)) {}

void HomeDescription::onEnter() {
  Page::onEnter();
  page_ = 0;
  loadDescription();
  paginate();
}

void HomeDescription::loadDescription() {
  RecentBook book;
  book.path = bookPath_;
  for (const RecentBook& recent : RECENT_BOOKS.getBooks()) {
    if (recent.path == bookPath_) {
      book = recent;
      break;
    }
  }

  BookMetadataCache metadata(widget::support::cachePathFor(book));
  description_.clear();
  if (metadata.load()) description_ = metadata.coreMetadata.description;
}

void HomeDescription::paginate() {
  lines_.clear();
  if (description_.empty()) {
    pageCount_ = 1;
    return;
  }

  const int font = systemFontId();
  const int width = std::max(1, renderer.getScreenWidth() - 40);
  const int spaceWidth = renderer.text.getSpaceWidth(font);
  const std::vector<DescriptionLine::Run> runs = parseDescription(description_);
  DescriptionLine line;
  int lineWidth = 0;

  const auto pushLine = [&] {
    if (!line.runs.empty() && !line.runs.back().text.empty() && line.runs.back().text.back() == ' ') {
      line.runs.back().text.pop_back();
    }
    lines_.push_back(std::move(line));
    line = DescriptionLine();
    lineWidth = 0;
  };

  for (const DescriptionLine::Run& run : runs) {
    size_t pos = 0;
    while (pos <= run.text.size()) {
      const size_t newline = run.text.find('\n', pos);
      const size_t end = newline == std::string::npos ? run.text.size() : newline;
      size_t token = pos;
    while (token < end) {
      while (token < end && std::isspace(static_cast<unsigned char>(run.text[token]))) ++token;
      if (token >= end) break;
      size_t tokenEnd = token;
      while (tokenEnd < end && !std::isspace(static_cast<unsigned char>(run.text[tokenEnd]))) ++tokenEnd;
      const std::string word = run.text.substr(token, tokenEnd - token);
      const int wordWidth = renderer.text.getWidth(font, word.c_str(), run.style);
      const int required = line.runs.empty() ? wordWidth : spaceWidth + wordWidth;
      if (!line.runs.empty() && lineWidth + required > width) pushLine();
      if (!line.runs.empty()) {
        appendRun(line.runs, " ", run.style);
        lineWidth += spaceWidth;
      }
      appendRun(line.runs, word, run.style);
      lineWidth += wordWidth;
      token = tokenEnd;
    }
      if (newline == std::string::npos) {
        break;
      }
      pushLine();
      pos = newline + 1;
    }
  }
  if (!line.runs.empty()) pushLine();

  const int bodyTop = UiLayout::PAGE_HEADER_HEIGHT;
  const int bodyBottom = renderer.getScreenHeight() - 70;
  const int visibleLines = std::max(1, (bodyBottom - bodyTop) /
                                           std::max(1, renderer.text.getLineHeight(font)));
  pageCount_ = std::max(1, (static_cast<int>(lines_.size()) + visibleLines - 1) / visibleLines);
}

void HomeDescription::movePage(const int delta) {
  const int next = std::max(0, std::min(pageCount_ - 1, page_ + delta));
  if (next == page_) return;
  page_ = next;
  requestRender();
}

void HomeDescription::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    back();
    return;
  }
  if (mappedInput.wasPressed(itemPrevButton()) || mappedInput.wasPressed(tabPrevButton())) {
    movePage(-1);
    return;
  }
  if (mappedInput.wasPressed(itemNextButton()) || mappedInput.wasPressed(tabNextButton())) {
    movePage(1);
    return;
  }
  Page::loop();
}

bool HomeDescription::back() {
  if (onBack_) onBack_();
  return true;
}

void HomeDescription::title() const { ScreenComponents::drawSubPageHeader(renderer, "Description"); }

void HomeDescription::menu() {
  title();
  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "", page_ > 0 ? "Prev" : "",
                                            page_ + 1 < pageCount_ ? "Next" : "");
  renderer.ui.buttonHints(MONTSERRAT_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void HomeDescription::content() {
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  const int bodyTop = UiLayout::PAGE_HEADER_HEIGHT;
  const int bodyBottom = renderer.getScreenHeight() - 70;
  const int visibleLines = std::max(1, (bodyBottom - bodyTop) / std::max(1, lineHeight));

  if (description_.empty()) {
    renderer.text.centered(font, bodyTop + (bodyBottom - bodyTop) / 2, "No description available.", true,
                           EpdFontFamily::REGULAR);
  } else {
    const int first = page_ * visibleLines;
    const int last = std::min(static_cast<int>(lines_.size()), first + visibleLines);
    for (int i = first; i < last; ++i) {
      int drawX = 20;
      for (const DescriptionLine::Run& run : lines_[static_cast<size_t>(i)].runs) {
        renderer.text.render(font, drawX, bodyTop + (i - first) * lineHeight, run.text.c_str(), true, run.style);
        drawX += renderer.text.getWidth(font, run.text.c_str(), run.style);
      }
    }
  }

  if (pageCount_ > 1) {
    const std::string indicator = std::to_string(page_ + 1) + " / " + std::to_string(pageCount_);
    renderer.text.centered(MONTSERRAT_10_FONT_ID, renderer.getScreenHeight() - 58, indicator.c_str(), true,
                           EpdFontFamily::REGULAR);
  }
}
