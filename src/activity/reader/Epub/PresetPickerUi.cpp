/**
 * @file PresetPickerUi.cpp
 * @brief Definitions for PresetPickerUi.
 */

#include "PresetPickerUi.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <string>

#include "EpubActivity.h"
#include "state/BookSetting.h"
#include "state/ReaderPreset.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/UiTheme.h"

namespace {
constexpr int kVisibleRows = 6;
}

void PresetPickerUi::enter(EpubActivity& act) {
  mode_ = true;
  const int presetCount = std::max(1, READER_PRESETS.count());
  selected_ = act.bookSettings.readerPresetIndex == BookSettings::kNoReaderPreset ? 0 : act.bookSettings.readerPresetIndex;
  selected_ = std::max(0, std::min(selected_, presetCount - 1));
  scroll_ = std::max(0, selected_ - kVisibleRows / 2);
  clampScroll();
  render(act);
}

void PresetPickerUi::handleInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;
  const int presetCount = std::max(1, READER_PRESETS.count());

  if (m.wasReleased(MappedInputManager::Button::Back)) {
    mode_ = false;
    act.renderScreen(true);
    return;
  }

  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    mode_ = false;
    act.settingsDrawerSnapshot_ = act.bookSettings;
    act.hasSettingsDrawerSnapshot_ = true;
    READER_PRESETS.applyToBook(selected_, act.bookSettings);
    act.saveBookSettings();
    act.applyBookSettings();
    act.startPageTimer();
    return;
  }

  if (m.wasPressed(MenuNav::itemPrev())) {
    selected_ = (selected_ - 1 + presetCount) % presetCount;
    if (selected_ < scroll_) {
      scroll_ = selected_;
    }
    if (selected_ >= scroll_ + kVisibleRows) {
      scroll_ = selected_ - kVisibleRows + 1;
    }
    clampScroll();
    render(act);
    return;
  }

  if (m.wasPressed(MenuNav::itemNext())) {
    selected_ = (selected_ + 1) % presetCount;
    if (selected_ < scroll_) {
      scroll_ = selected_;
    }
    if (selected_ >= scroll_ + kVisibleRows) {
      scroll_ = selected_ - kVisibleRows + 1;
    }
    clampScroll();
    render(act);
    return;
  }
}

void PresetPickerUi::clampScroll() {
  const int presetCount = std::max(1, READER_PRESETS.count());
  const int rows = std::min(kVisibleRows, presetCount);
  const int maxScroll = std::max(0, presetCount - rows);
  scroll_ = std::max(0, std::min(scroll_, maxScroll));
}

void PresetPickerUi::render(EpubActivity& act) {
  GfxRenderer& renderer = act.renderer;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int presetCount = std::max(1, READER_PRESETS.count());
  const int rows = std::min(kVisibleRows, presetCount);

  const int boxW = std::min(screenW - 60, 320);
  constexpr int rowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT - 4;
  const int overlayHeaderH = INX_THEME.drawerHeaderHeight() - 4;
  const int boxH = overlayHeaderH + rows * rowH;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = (screenH - boxH) / 2;

  renderer.rectangle.fill(boxX, boxY, boxW, boxH, false);

  const int titleY = boxY + (overlayHeaderH - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
  renderer.text.render(MONTSERRAT_10_FONT_ID, boxX + 16, titleY, "Apply Preset", true,
                       EpdFontFamily::BOLD);

  clampScroll();
  for (int i = 0; i < rows; ++i) {
    const int presetIndex = scroll_ + i;
    if (presetIndex >= presetCount) {
      break;
    }
    const int rowY = boxY + overlayHeaderH + i * rowH;
    const bool sel = (presetIndex == selected_);
    if (sel) {
      renderer.rectangle.fill(boxX + 1, rowY, boxW - 2, rowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    const std::string name =
        renderer.text.truncate(MONTSERRAT_10_FONT_ID, READER_PRESETS.nameOf(presetIndex).c_str(), boxW - 40);
    const int textY = rowY + (rowH - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
    renderer.text.render(MONTSERRAT_10_FONT_ID, boxX + 20, textY, name.c_str(), sel ? 0 : 1);
    if (i + 1 < rows) {
      renderer.line.render(boxX, rowY + rowH, boxX + boxW, rowY + rowH, !sel, LineRender::Style::Dotted);
    }
  }

  if (presetCount > rows) {
    const int maxScroll = std::max(1, presetCount - rows);
    const int trackX = boxX + boxW - 10;
    const int trackY = boxY + overlayHeaderH;
    const int trackH = rows * rowH;
    const int thumbH = std::max(8, trackH * rows / presetCount);
    const int thumbY = trackY + scroll_ * std::max(1, trackH - thumbH) / maxScroll;
    renderer.rectangle.fill(trackX, trackY, 2, trackH, true);
    renderer.rectangle.fill(trackX - 2, thumbY, 6, thumbH, true);
  }

  renderer.line.render(boxX, boxY + overlayHeaderH, boxX + boxW, boxY + overlayHeaderH, true);
  renderer.rectangle.render(boxX, boxY, boxW, boxH, true);
  renderer.rectangle.render(boxX + 1, boxY + 1, boxW - 2, boxH - 2, true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
