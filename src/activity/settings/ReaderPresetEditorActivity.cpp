/**
 * @file ReaderPresetEditorActivity.cpp
 * @brief Definitions for ReaderPresetEditorActivity.
 */

#include "ReaderPresetEditorActivity.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "../reader/Epub/SettingsDrawer.h"
#include "../reader/Epub/StatusBar.h"
#include "../util/KeyboardEntryActivity.h"
#include "GfxRenderer.h"
#include "state/ReaderPreset.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"

namespace {

const char* kLoremParagraph1 =
    "The quick brown fox jumps over the lazy dog while the printing press hums softly in the "
    "background, setting each line of type with patient, deliberate care.";
const char* kLoremParagraph2 =
    "Good typography is invisible: it carries the words to the reader without ever calling attention "
    "to itself, balancing rhythm, spacing, and contrast on every page.";
constexpr int kPreviewMaxWords = 64;
constexpr size_t kPreviewWordBufferSize = 48;
constexpr size_t kPresetNameMaxLen = 40;

struct WordSlice {
  const char* start = nullptr;
  size_t len = 0;
};

/** Placeholder text for a status-bar item, used purely to illustrate the layout in the preview. */
const char* statusPlaceholder(StatusBarItem item) {
  switch (item) {
    case StatusBarItem::PAGE_NUMBERS:
      return "12/340";
    case StatusBarItem::PERCENTAGE:
      return "45%";
    case StatusBarItem::CHAPTER_TITLE:
      return "Chapter Three";
    case StatusBarItem::BATTERY_ICON:
      return "[||||]";
    case StatusBarItem::BATTERY_PERCENTAGE:
      return "78%";
    case StatusBarItem::BATTERY_ICON_WITH_PERCENT:
      return "[||||] 78%";
    case StatusBarItem::PROGRESS_BAR:
      return "====------";
    case StatusBarItem::PROGRESS_BAR_WITH_PERCENT:
      return "====-- 45%";
    case StatusBarItem::PAGE_BARS:
      return "|||..";
    case StatusBarItem::BOOK_TITLE:
      return "The Example Book";
    case StatusBarItem::AUTHOR_NAME:
      return "Jane Author";
    case StatusBarItem::PAGE_NUMBERS_WITH_PERCENT:
      return "12/340 45%";
    case StatusBarItem::TIME_LEFT_CHAPTER:
      return "12m";
    case StatusBarItem::TIME_LEFT_BOOK:
      return "3h 45m";
    case StatusBarItem::NONE:
    default:
      return "";
  }
}

int splitWords(const char* text, WordSlice* words, const int maxWords) {
  if (!text || !words || maxWords <= 0) {
    return 0;
  }
  int count = 0;
  const char* wordStart = nullptr;
  for (const char* p = text; *p; ++p) {
    if (*p == ' ') {
      if (wordStart && count < maxWords) {
        words[count].start = wordStart;
        words[count].len = static_cast<size_t>(p - wordStart);
        ++count;
      }
      wordStart = nullptr;
    } else {
      if (!wordStart) {
        wordStart = p;
      }
    }
  }
  if (wordStart && count < maxWords) {
    words[count].start = wordStart;
    words[count].len = std::strlen(wordStart);
    ++count;
  }
  return count;
}

const char* wordToBuffer(const WordSlice& word, char* buffer, const size_t bufferSize) {
  if (!buffer || bufferSize == 0) {
    return "";
  }
  const size_t n = std::min(word.len, bufferSize - 1);
  if (word.start && n > 0) {
    std::memcpy(buffer, word.start, n);
  }
  buffer[n] = '\0';
  return buffer;
}

std::string truncatePresetName(const std::string& name) {
  return name.size() > kPresetNameMaxLen ? name.substr(0, kPresetNameMaxLen) : name;
}

std::string defaultPresetNameFor(const BookSettings& settings) {
  const FontManager::FontInfo* info = FontManager::getFontInfo(settings.getReaderFontId());
  if (info != nullptr && !info->name.empty()) {
    return truncatePresetName(info->name);
  }
  return "Preset";
}

}  // namespace

ReaderPresetEditorActivity::ReaderPresetEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       int presetIndex, std::function<void()> onDone)
    : ActivityWithSubactivity("ReaderPresetEditor", renderer, mappedInput),
      presetIndex_(presetIndex),
      isNew_(presetIndex < 0),
      onDone_(std::move(onDone)) {}

ReaderPresetEditorActivity::~ReaderPresetEditorActivity() {}

void ReaderPresetEditorActivity::onEnter() {
  enteredAtMs_ = millis();

  if (isNew_) {
    working_ = READER_PRESETS.settingsOf(0);  // seed from Default
    name_ = defaultPresetNameFor(working_);
  } else {
    working_ = READER_PRESETS.settingsOf(presetIndex_);
    name_ = READER_PRESETS.nameOf(presetIndex_);
  }
  working_.useCustomSettings = true;

  FontManager::ensureFontReady(working_.getReaderFontId(), renderer);

  const int screenH = renderer.getScreenHeight();
  const int screenW = renderer.getScreenWidth();

  drawer_.reset(new SettingsDrawer(renderer, working_, [this]() {
    // A value changed: make sure the (possibly new) reader font is loaded before the preview redraws.
    FontManager::ensureFontReady(working_.getReaderFontId(), renderer);
  }));

  // Aim for ~65% drawer height, then snap it to a whole number of rows so the menu has no dead space
  // at the bottom; the preview absorbs whatever remains.
  const int drawerRegionHeight = drawer_->snapEmbeddedHeight(screenH * 65 / 100);
  previewHeight_ = screenH - drawerRegionHeight;
  drawer_->setEmbeddedRegion(0, previewHeight_, screenW, drawerRegionHeight);
  drawer_->setEmbeddedInvalidate([this]() {
    renderPreview();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  });

  renderer.clearScreen(0xFF);
  drawer_->show();  // draws the embedded region, then invokes the invalidate callback (preview + push)
}

void ReaderPresetEditorActivity::onExit() {
  exitActivity();  // tear down keyboard sub-activity if any
  drawer_.reset();
}

void ReaderPresetEditorActivity::renderPreview() {
  const int screenW = renderer.getScreenWidth();
  const int margin = std::max<int>(6, working_.screenMargin);
  const int fontId = working_.getReaderFontId();

  // Clear the preview region (no header label/tag; the demo text is the focus).
  renderer.rectangle.fill(0, 0, screenW, previewHeight_, false);

  // Mirror EpubActivity::calculateViewport()'s hasStatusBar check and StatusBar::reservedFullBarHeight()
  // so the preview reclaims the same space the real reader would when a bar has no content (global
  // SETTINGS, same as the real reader - see StatusBar::render()'s doc comment for why this isn't
  // per-preset).
  const bool hasStatusBar = (READER_SETTINGS.statusBarLeft != SystemSetting::STATUS_ITEM_NONE ||
                             READER_SETTINGS.statusBarMiddle != SystemSetting::STATUS_ITEM_NONE ||
                             READER_SETTINGS.statusBarRight != SystemSetting::STATUS_ITEM_NONE);
  const int statusBarHeight = hasStatusBar ? 28 : 0;
  const int fullBarHeight = StatusBar::reservedFullBarHeight();
  const int bodyTop = 16;
  const int bodyBottom = previewHeight_ - statusBarHeight - fullBarHeight - 6;
  const int maxWidth = std::max(40, screenW - 2 * margin);
  const int spaceWidth = std::max(
      1, static_cast<int>(std::lround(renderer.text.getSpaceWidth(fontId) * working_.getReaderWordSpacingFactor())));
  int lineHeight = static_cast<int>(renderer.text.getLineHeight(fontId) * working_.getReaderLineCompression());
  if (lineHeight < 8) lineHeight = renderer.text.getLineHeight(fontId);

  const bool bionic = working_.bionicReadingEnabled != 0;
  const int indentPx = working_.paragraphCssIndentEnabled ? (2 * spaceWidth + 8) : 0;
  const uint8_t align = working_.paragraphAlignment;

  auto renderWord = [&](int x, int y, const WordSlice& word) {
    char wordBuf[kPreviewWordBufferSize];
    const char* text = wordToBuffer(word, wordBuf, sizeof(wordBuf));
    const size_t len = std::strlen(text);
    if (!bionic || len < 2) {
      renderer.text.render(fontId, x, y, text, true, EpdFontFamily::REGULAR);
      return;
    }
    const size_t boldLen = (len + 1) / 2;
    char head[kPreviewWordBufferSize];
    char tail[kPreviewWordBufferSize];
    std::memcpy(head, text, boldLen);
    head[boldLen] = '\0';
    std::strncpy(tail, text + boldLen, sizeof(tail) - 1);
    tail[sizeof(tail) - 1] = '\0';
    renderer.text.render(fontId, x, y, head, true, EpdFontFamily::BOLD);
    const int headW = renderer.text.getWidth(fontId, head, EpdFontFamily::BOLD);
    renderer.text.render(fontId, x + headW, y, tail, true, EpdFontFamily::REGULAR);
  };

  int y = bodyTop;
  const char* paragraphs[2] = {kLoremParagraph1, kLoremParagraph2};
  const int paragraphGap = working_.extraParagraphSpacing ? (lineHeight / 2 + 4) : 2;

  for (int p = 0; p < 2 && y + lineHeight <= bodyBottom; ++p) {
    WordSlice words[kPreviewMaxWords];
    const int wordCount = splitWords(paragraphs[p], words, kPreviewMaxWords);
    int i = 0;
    bool firstLine = true;
    while (i < wordCount && y + lineHeight <= bodyBottom) {
      const int lineIndent = firstLine ? indentPx : 0;
      const int lineMaxWidth = maxWidth - lineIndent;

      // Greedily pack words for this line.
      const int lineStart = i;
      int naturalWidth = 0;
      int widths[kPreviewMaxWords] = {};
      int widthCount = 0;
      while (i < wordCount && widthCount < kPreviewMaxWords) {
        char wordBuf[kPreviewWordBufferSize];
        const int ww = renderer.text.getWidth(fontId, wordToBuffer(words[i], wordBuf, sizeof(wordBuf)));
        const int withWord = naturalWidth + (i > lineStart ? spaceWidth : 0) + ww;
        if (withWord > lineMaxWidth && i > lineStart) break;
        widths[widthCount++] = ww;
        naturalWidth = withWord;
        ++i;
      }
      const int count = widthCount;
      const bool lastLine = (i >= wordCount);

      int x = margin + lineIndent;
      int gap = spaceWidth;
      if (align == SystemSetting::JUSTIFIED && !lastLine && count > 1) {
        const int extra = lineMaxWidth - naturalWidth;
        gap = spaceWidth + extra / (count - 1);
      } else if (align == SystemSetting::CENTER_ALIGN) {
        x = margin + lineIndent + (lineMaxWidth - naturalWidth) / 2;
      } else if (align == SystemSetting::RIGHT_ALIGN) {
        x = margin + lineIndent + (lineMaxWidth - naturalWidth);
      }

      for (int k = 0; k < count; ++k) {
        renderWord(x, y, words[lineStart + k]);
        x += widths[k] + gap;
      }

      y += lineHeight;
      firstLine = false;
    }
    y += paragraphGap;
  }

  renderPreviewStatusBar(previewHeight_ - statusBarHeight - fullBarHeight, statusBarHeight);
  if (fullBarHeight > 0) {
    renderPreviewFullBar(previewHeight_ - fullBarHeight, fullBarHeight);
  }
}

void ReaderPresetEditorActivity::renderPreviewStatusBar(int barTop, int barHeight) {
  const int screenW = renderer.getScreenWidth();
  const int margin = std::max<int>(6, working_.screenMargin);
  const int fontId = ATKINSON_HYPERLEGIBLE_8_FONT_ID;

  const int textY = barTop + (barHeight - renderer.text.getLineHeight(fontId)) / 2 + 2;

  // Global SETTINGS, not working_ - the Status Bar entries in the embedded drawer write to
  // READER_SETTINGS.statusBarLeft/Middle/Right (see SettingsDrawer.cpp's setupMenu() comment), not to this
  // per-book working_ copy, so reading working_ here would always show the stale seeded value.
  const char* left = statusPlaceholder(static_cast<StatusBarItem>(READER_SETTINGS.statusBarLeft));
  const char* middle = statusPlaceholder(static_cast<StatusBarItem>(READER_SETTINGS.statusBarMiddle));
  const char* right = statusPlaceholder(static_cast<StatusBarItem>(READER_SETTINGS.statusBarRight));

  if (left && left[0]) {
    renderer.text.render(fontId, margin + 2, textY, left, true);
  }
  if (middle && middle[0]) {
    const int w = renderer.text.getWidth(fontId, middle);
    renderer.text.render(fontId, (screenW - w) / 2, textY, middle, true);
  }
  if (right && right[0]) {
    const int w = renderer.text.getWidth(fontId, right);
    renderer.text.render(fontId, screenW - margin - 2 - w, textY, right, true);
  }
}

void ReaderPresetEditorActivity::renderPreviewFullBar(int barTop, int barHeight) {
  const StatusBarItem style = static_cast<StatusBarItem>(READER_SETTINGS.statusBarFullStyle);
  if (style == StatusBarItem::NONE) {
    return;
  }

  const int screenW = renderer.getScreenWidth();
  const int fontId = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
  const int textY = barTop + (barHeight - renderer.text.getLineHeight(fontId)) / 2 + 2;

  if (style == StatusBarItem::PAGE_BARS) {
    // Mock: a row of small filled/outline bars across the full width, like real page bars.
    constexpr int kBars = 24;
    constexpr int kFilledBars = 9;  // representative "partway through the book" look
    const int barMarginX = 12;
    const int totalW = screenW - 2 * barMarginX;
    const int barW = std::max(2, totalW / kBars - 1);
    for (int i = 0; i < kBars; ++i) {
      const int x = barMarginX + i * (barW + 1);
      if (i < kFilledBars) {
        renderer.rectangle.fill(x, textY, barW, 5, true);
      } else {
        renderer.rectangle.render(x, textY, barW, 5, true);
      }
    }
    return;
  }

  // PROGRESS_BAR / PROGRESS_BAR_WITH_PERCENT: one full-width bar, ~40% filled for a representative look.
  const bool withPercent = style == StatusBarItem::PROGRESS_BAR_WITH_PERCENT;
  const char* pct = "40%";
  const int pctW = withPercent ? renderer.text.getWidth(fontId, pct) : 0;
  const int barMarginX = 12;
  const int barW = screenW - 2 * barMarginX - (withPercent ? pctW + 8 : 0);
  const int barX = barMarginX;
  const int barY = textY + 1;

  renderer.rectangle.render(barX, barY, barW, 6, true);
  const int fillW = barW * 2 / 5;
  if (fillW > 0) {
    renderer.rectangle.fill(barX + 1, barY + 1, fillW - 2, 4, true);
  }
  if (withPercent) {
    renderer.text.render(fontId, barX + barW + 8, textY, pct, true);
  }
}

void ReaderPresetEditorActivity::promptName() {
  if (isNew_) {
    name_ = defaultPresetNameFor(working_);
  }
  auto* keyboard = new KeyboardEntryActivity(
      renderer, mappedInput, "Name this preset", name_, 10, 40, false,
      [this](const std::string& entered) {
        if (!entered.empty()) name_ = entered;
        finishRequested_ = true;
      },
      [this]() { finishRequested_ = true; });
  enterNewActivity(keyboard);
}

void ReaderPresetEditorActivity::beginExit() {
  if (isNew_) {
    promptName();  // name the new preset, then doSaveAndFinish() runs after the keyboard tears down
  } else {
    doSaveAndFinish();
  }
}

void ReaderPresetEditorActivity::doSaveAndFinish() {
  if (isNew_) {
    READER_PRESETS.add(name_, working_);
  } else {
    READER_PRESETS.update(presetIndex_, name_, working_);
  }
  // The parent deletes this activity inside onDone_; copy to a local and touch no members afterward.
  auto done = onDone_;
  if (done) done();
}

void ReaderPresetEditorActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();  // run the keyboard
    if (finishRequested_) {
      finishRequested_ = false;
      exitActivity();  // tear down the keyboard now that its loop returned
      doSaveAndFinish();
    }
    return;
  }

  // Debounce the entry transition so the press that opened the editor isn't read as a Back.
  if (millis() - enteredAtMs_ < 200) {
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    beginExit();
    return;
  }

  if (drawer_) {
    drawer_->handleInput(mappedInput);
  }
}
