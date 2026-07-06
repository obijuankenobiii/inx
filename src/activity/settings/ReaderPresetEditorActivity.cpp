/**
 * @file ReaderPresetEditorActivity.cpp
 * @brief Definitions for ReaderPresetEditorActivity.
 */

#include "ReaderPresetEditorActivity.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../reader/Epub/SettingsDrawer.h"
#include "../util/KeyboardEntryActivity.h"
#include "GfxRenderer.h"
#include "state/ReaderPreset.h"
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
    case StatusBarItem::NONE:
    default:
      return "";
  }
}

std::vector<std::string> splitWords(const char* text) {
  std::vector<std::string> words;
  std::string cur;
  for (const char* p = text; *p; ++p) {
    if (*p == ' ') {
      if (!cur.empty()) {
        words.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(*p);
    }
  }
  if (!cur.empty()) words.push_back(cur);
  return words;
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
    name_ = "New Preset";
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

  // Aim for ~48% drawer height, then snap it to a whole number of rows so the menu has no dead space
  // at the bottom; the preview absorbs whatever remains.
  const int drawerRegionHeight = drawer_->snapEmbeddedHeight(screenH - screenH * 52 / 100);
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

  const int statusBarHeight = 28;
  const int bodyTop = 16;
  const int bodyBottom = previewHeight_ - statusBarHeight - 6;
  const int maxWidth = std::max(40, screenW - 2 * margin);
  const int spaceWidth =
      std::max(1, static_cast<int>(std::lround(renderer.text.getSpaceWidth(fontId) *
                                               working_.getReaderWordSpacingFactor())));
  int lineHeight = static_cast<int>(renderer.text.getLineHeight(fontId) * working_.getReaderLineCompression());
  if (lineHeight < 8) lineHeight = renderer.text.getLineHeight(fontId);

  const bool bionic = working_.bionicReadingEnabled != 0;
  const int indentPx = working_.paragraphCssIndentEnabled ? (2 * spaceWidth + 8) : 0;
  const uint8_t align = working_.paragraphAlignment;

  auto renderWord = [&](int x, int y, const std::string& w) {
    if (!bionic || w.size() < 2) {
      renderer.text.render(fontId, x, y, w.c_str(), true, EpdFontFamily::REGULAR);
      return;
    }
    const size_t boldLen = (w.size() + 1) / 2;
    const std::string head = w.substr(0, boldLen);
    const std::string tail = w.substr(boldLen);
    renderer.text.render(fontId, x, y, head.c_str(), true, EpdFontFamily::BOLD);
    const int headW = renderer.text.getWidth(fontId, head.c_str(), EpdFontFamily::BOLD);
    renderer.text.render(fontId, x + headW, y, tail.c_str(), true, EpdFontFamily::REGULAR);
  };

  int y = bodyTop;
  const char* paragraphs[2] = {kLoremParagraph1, kLoremParagraph2};
  const int paragraphGap = working_.extraParagraphSpacing ? (lineHeight / 2 + 4) : 2;

  for (int p = 0; p < 2 && y + lineHeight <= bodyBottom; ++p) {
    const std::vector<std::string> words = splitWords(paragraphs[p]);
    size_t i = 0;
    bool firstLine = true;
    while (i < words.size() && y + lineHeight <= bodyBottom) {
      const int lineIndent = firstLine ? indentPx : 0;
      const int lineMaxWidth = maxWidth - lineIndent;

      // Greedily pack words for this line.
      size_t lineStart = i;
      int naturalWidth = 0;
      std::vector<int> widths;
      while (i < words.size()) {
        const int ww = renderer.text.getWidth(fontId, words[i].c_str());
        const int withWord = naturalWidth + (i > lineStart ? spaceWidth : 0) + ww;
        if (withWord > lineMaxWidth && i > lineStart) break;
        widths.push_back(ww);
        naturalWidth = withWord;
        ++i;
      }
      const int count = static_cast<int>(i - lineStart);
      const bool lastLine = (i >= words.size());

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
        const std::string& w = words[lineStart + k];
        renderWord(x, y, w);
        x += widths[k] + gap;
      }

      y += lineHeight;
      firstLine = false;
    }
    y += paragraphGap;
  }

  renderPreviewStatusBar(previewHeight_ - statusBarHeight, statusBarHeight);
}

void ReaderPresetEditorActivity::renderPreviewStatusBar(int barTop, int barHeight) {
  const int screenW = renderer.getScreenWidth();
  const int margin = std::max<int>(6, working_.screenMargin);
  const int fontId = ATKINSON_HYPERLEGIBLE_8_FONT_ID;

  const int textY = barTop + (barHeight - renderer.text.getLineHeight(fontId)) / 2 + 2;

  const char* left = statusPlaceholder(working_.statusBarLeft.item);
  const char* middle = statusPlaceholder(working_.statusBarMiddle.item);
  const char* right = statusPlaceholder(working_.statusBarRight.item);

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

void ReaderPresetEditorActivity::promptName() {
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
