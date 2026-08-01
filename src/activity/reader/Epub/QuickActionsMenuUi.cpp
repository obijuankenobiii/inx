/**
 * @file QuickActionsMenuUi.cpp
 * @brief Definitions for QuickActionsMenuUi.
 */

#include "QuickActionsMenuUi.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstring>

#include "EpubActivity.h"
#include "ReaderButtonBindings.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/UiTheme.h"

namespace {
constexpr int kVisibleRows = 6;
}

void QuickActionsMenuUi::enter(EpubActivity& act) {
  actions_.clear();
  for (int i = 1; i < static_cast<int>(SystemSetting::READER_BUTTON_ACTION_COUNT); ++i) {
    if (i == SystemSetting::BTN_ACTION_QUICK_ACTIONS) {
      continue;
    }
    if (READER_SETTINGS.quickActionsMask & (1u << i)) {
      actions_.push_back(static_cast<uint8_t>(i));
    }
  }
  // A-Z by label, same order as the QuickActionsSettingsActivity checklist this is built from.
  std::sort(actions_.begin(), actions_.end(), [](const uint8_t a, const uint8_t b) {
    return strcmp(SystemSetting::readerButtonActionLabel(a), SystemSetting::readerButtonActionLabel(b)) < 0;
  });
  if (actions_.empty()) {
    act.readerPopup("No quick actions configured");
    return;
  }
  mode_ = true;
  selected_ = 0;
  scroll_ = 0;
  clampScroll();
  render(act);
}

void QuickActionsMenuUi::handleInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;
  const int count = static_cast<int>(actions_.size());
  if (count == 0) {
    mode_ = false;
    act.renderScreen(true);
    return;
  }

  if (m.wasReleased(MappedInputManager::Button::Back)) {
    mode_ = false;
    act.renderScreen(true);
    return;
  }

  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    const uint8_t chosen = actions_[static_cast<size_t>(selected_)];
    mode_ = false;
    act.renderScreen(true);
    act.btnBindings_.dispatch(act, chosen);
    return;
  }

  if (m.wasPressed(MenuNav::itemPrev())) {
    selected_ = (selected_ - 1 + count) % count;
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
    selected_ = (selected_ + 1) % count;
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

void QuickActionsMenuUi::clampScroll() {
  const int count = std::max(1, static_cast<int>(actions_.size()));
  const int rows = std::min(kVisibleRows, count);
  const int maxScroll = std::max(0, count - rows);
  scroll_ = std::max(0, std::min(scroll_, maxScroll));
}

void QuickActionsMenuUi::render(EpubActivity& act) {
  GfxRenderer& renderer = act.renderer;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int count = static_cast<int>(actions_.size());
  const int rows = std::min(kVisibleRows, count);

  const int boxW = std::min(screenW - 60, 320);
  constexpr int rowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT - 4;
  const int overlayHeaderH = INX_THEME.drawerHeaderHeight() - 4;
  const int boxH = overlayHeaderH + rows * rowH;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = (screenH - boxH) / 2;

  renderer.rectangle.fill(boxX, boxY, boxW, boxH, false);

  const int titleY = boxY + (overlayHeaderH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, boxX + 16, titleY, "Quick Actions", true,
                       EpdFontFamily::BOLD);

  clampScroll();
  for (int i = 0; i < rows; ++i) {
    const int actionIdx = scroll_ + i;
    if (actionIdx >= count) {
      break;
    }
    const int rowY = boxY + overlayHeaderH + i * rowH;
    const bool sel = (actionIdx == selected_);
    if (sel) {
      renderer.rectangle.fill(boxX + 1, rowY, boxW - 2, rowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    const char* label = SystemSetting::readerButtonActionLabel(actions_[static_cast<size_t>(actionIdx)]);
    const int textY = rowY + (rowH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, boxX + 20, textY, label, sel ? 0 : 1);
    if (i + 1 < rows) {
      renderer.line.render(boxX, rowY + rowH, boxX + boxW, rowY + rowH, !sel, LineRender::Style::Dotted);
    }
  }

  if (count > rows) {
    const int maxScroll = std::max(1, count - rows);
    const int trackX = boxX + boxW - 10;
    const int trackY = boxY + overlayHeaderH;
    const int trackH = rows * rowH;
    const int thumbH = std::max(8, trackH * rows / count);
    const int thumbY = trackY + scroll_ * std::max(1, trackH - thumbH) / maxScroll;
    renderer.rectangle.fill(trackX, trackY, 2, trackH, true);
    renderer.rectangle.fill(trackX - 2, thumbY, 6, thumbH, true);
  }

  renderer.line.render(boxX, boxY + overlayHeaderH, boxX + boxW, boxY + overlayHeaderH, true);
  renderer.rectangle.render(boxX, boxY, boxW, boxH, true);
  renderer.rectangle.render(boxX + 1, boxY + 1, boxW - 2, boxH - 2, true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
