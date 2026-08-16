#include "DictionaryPickerActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstring>

#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiTheme.h"

namespace {
constexpr int kBodyFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
constexpr int kRowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT;
}  // namespace

void DictionaryPickerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  scanDictionaryFolders();
  render();
}

void DictionaryPickerActivity::scanDictionaryFolders() {
  entries_ = DictionaryRegistry::scan();
  selectedIndex_ = 0;
  scrollOffset_ = 0;

  if (READER_SETTINGS.dictionaryFolder[0] != '\0') {
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (entries_[i].folder == READER_SETTINGS.dictionaryFolder) {
        selectedIndex_ = static_cast<int>(i);
        break;
      }
    }
  }
}

void DictionaryPickerActivity::cycleSelectedLang(const int delta) {
  if (entries_.empty()) {
    return;
  }
  DictionaryRegistry::Entry& entry = entries_[static_cast<size_t>(selectedIndex_)];
  const std::string currentOverride = DictionaryRegistry::overrideFor(entry.folder);
  int idx = DictionaryRegistry::langCycleIndex(currentOverride);
  idx = (idx + delta + DictionaryRegistry::kLangCycleCount) % DictionaryRegistry::kLangCycleCount;
  const char* next = DictionaryRegistry::kLangCycle[idx];
  DictionaryRegistry::setOverride(entry.folder, next);
  if (next[0] == '\0') {
    entry.lang = DictionaryRegistry::readIfoLang(std::string(DictionaryRegistry::kDictionariesRoot) + "/" + entry.folder);
    if (entry.lang.empty()) {
      entry.lang = DictionaryRegistry::inferLangFromName(entry.folder);
    }
  } else {
    entry.lang = next;
  }
  render();
}

void DictionaryPickerActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    goBack_();
    return;
  }

  const int total = static_cast<int>(entries_.size());
  if (total == 0) {
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex_ = (selectedIndex_ + 1) % total;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex_ = (selectedIndex_ + total - 1) % total;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    cycleSelectedLang(-1);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    cycleSelectedLang(1);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    const std::string& chosen = entries_[static_cast<size_t>(selectedIndex_)].folder;
    strncpy(READER_SETTINGS.dictionaryFolder, chosen.c_str(), sizeof(READER_SETTINGS.dictionaryFolder) - 1);
    READER_SETTINGS.dictionaryFolder[sizeof(READER_SETTINGS.dictionaryFolder) - 1] = '\0';
    READER_SETTINGS.saveToFile();
    goBack_();
    return;
  }
}

void DictionaryPickerActivity::render() {
  renderer.clearScreen();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int bodyTop = INX_THEME.drawPageHeader(renderer, "Choose dictionaries");

  const int total = static_cast<int>(entries_.size());
  if (total == 0) {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(kBodyFont, centerY, "No dictionaries found.", true, EpdFontFamily::BOLD);
    renderer.text.centered(kBodyFont, centerY + 32, "Put StarDict folders under /dictionaries/", true,
                           EpdFontFamily::REGULAR);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, centerY + 56, "One folder per language. All of them are used.",
                           true, EpdFontFamily::REGULAR);
    const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
    renderer.ui.buttonHints(kBodyFont, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
    renderer.displayBuffer();
    return;
  }

  const int listBottom = screenH - 44;
  const int visibleRows = std::max(1, (listBottom - bodyTop) / kRowH);
  if (selectedIndex_ < scrollOffset_) {
    scrollOffset_ = selectedIndex_;
  } else if (selectedIndex_ >= scrollOffset_ + visibleRows) {
    scrollOffset_ = selectedIndex_ - visibleRows + 1;
  }
  const int maxScroll = std::max(0, total - visibleRows);
  scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll));
  const int endIndex = std::min(total, scrollOffset_ + visibleRows);

  for (int i = scrollOffset_; i < endIndex; ++i) {
    const int y = bodyTop + (i - scrollOffset_) * kRowH;
    const bool selected = i == selectedIndex_;
    const bool active = entries_[static_cast<size_t>(i)].folder == READER_SETTINGS.dictionaryFolder;
    if (selected) {
      renderer.rectangle.fill(0, y, screenW, kRowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    const int titleY = y + (kRowH - renderer.text.getLineHeight(kBodyFont)) / 2 - 8;
    const int subY = titleY + renderer.text.getLineHeight(kBodyFont) + 2;
    std::string label = entries_[static_cast<size_t>(i)].folder;
    if (active) {
      label += "  \xE2\x9C\x93";
    }
    renderer.text.render(kBodyFont, 20, titleY, label.c_str(), !selected, EpdFontFamily::REGULAR);
    const std::string langLine = DictionaryRegistry::langLabel(entries_[static_cast<size_t>(i)].lang);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 20, subY, langLine.c_str(), !selected, EpdFontFamily::REGULAR);
    renderer.line.render(0, y + kRowH - 1, screenW, y + kRowH - 1, true, LineRender::Style::Dotted);
  }

  const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "Fallback", "Lang", "Lang");
  renderer.ui.buttonHints(kBodyFont, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer();
}
