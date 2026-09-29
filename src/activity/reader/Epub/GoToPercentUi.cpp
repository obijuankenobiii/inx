/**
 * @file GoToPercentUi.cpp
 * @brief Definitions for the in-book percentage navigation popup.
 */

#include "GoToPercentUi.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstdio>

#include "EpubActivity.h"
#include "images/LibraryFilterLeft.h"
#include "images/LibraryFilterRight.h"
#include "system/Fonts.h"
#include "system/MenuNav.h"

namespace {
constexpr int kPopupWidth = 420;
constexpr int kPopupHeight = 170;
constexpr int kTrackMargin = 66;
constexpr int kCaretSize = 30;
constexpr int kCaretGap = 8;
constexpr int kTrackHeight = 4;
constexpr int kKnobRadius = 22;
constexpr int kFineStep = 1;
constexpr int kCoarseStep = 10;

struct PopupBounds {
  int x;
  int y;
  int width;
  int height;
  int trackLeft;
  int trackRight;
  int trackY;
};

PopupBounds popupBounds(const GfxRenderer& renderer) {
  const int width = std::min(kPopupWidth, std::max(1, renderer.getScreenWidth() - 30));
  const int height = std::min(kPopupHeight, std::max(1, renderer.getScreenHeight() - 30));
  const int x = (renderer.getScreenWidth() - width) / 2;
  const int y = (renderer.getScreenHeight() - height) / 2;
  const int trackLeft = x + std::min(kTrackMargin, width / 4);
  const int trackRight = x + width - std::min(kTrackMargin, width / 4);
  return {x, y, width, height, trackLeft, trackRight, y + height - 46};
}

void drawKnob(const GfxRenderer& renderer, const int x, const int y) {
  for (int dy = -kKnobRadius; dy <= kKnobRadius; ++dy) {
    for (int dx = -kKnobRadius; dx <= kKnobRadius; ++dx) {
      if (dx * dx + dy * dy <= kKnobRadius * kKnobRadius) renderer.drawPixel(x + dx, y + dy, true);
    }
  }
  const int innerRadius = kKnobRadius - 2;
  for (int dy = -innerRadius; dy <= innerRadius; ++dy) {
    for (int dx = -innerRadius; dx <= innerRadius; ++dx) {
      if (dx * dx + dy * dy <= innerRadius * innerRadius) renderer.drawPixel(x + dx, y + dy, false);
    }
  }
}

void adjustPercent(int& percent, const int delta, bool& changed) {
  const int next = std::max(0, std::min(100, percent + delta));
  if (next == percent) return;
  percent = next;
  changed = true;
}
}  // namespace

void GoToPercentUi::enter(EpubActivity& act) {
  percent_ = 0;
  if (act.epub && act.section && act.section->pageCount > 0) {
    const float spineProgress = static_cast<float>(act.section->currentPage) /
                                static_cast<float>(act.section->pageCount);
    const float bookProgress = act.epub->calculateProgress(act.currentSpineIndex, spineProgress);
    percent_ = std::max(0, std::min(100, static_cast<int>(bookProgress * 100.0f + 0.5f)));
  }
  active_ = true;
  changed_ = false;
  render(act);
}

void GoToPercentUi::handleInput(EpubActivity& act) {
  if (!active_) return;

  const auto closeAndCommit = [&]() {
    if (changed_) {
      act.jumpToPercent(percent_);
      act.updateRequired = true;
      act.startPageTimer();
    } else {
      act.renderScreen(true);
    }
    active_ = false;
    changed_ = false;
  };

  if (act.mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      act.mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    closeAndCommit();
    return;
  }

  if (act.mappedInput.wasPressed(MenuNav::itemPrev())) {
    adjustPercent(percent_, -kFineStep, changed_);
    render(act);
    return;
  }
  if (act.mappedInput.wasPressed(MenuNav::itemNext())) {
    adjustPercent(percent_, kFineStep, changed_);
    render(act);
    return;
  }
  // The front left/right buttons should also act as fine-grained slider controls. Check these
  // explicitly after the menu-navigation aliases so both front-button layouts remain intuitive.
  if (act.mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    adjustPercent(percent_, -kFineStep, changed_);
    render(act);
    return;
  }
  if (act.mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    adjustPercent(percent_, kFineStep, changed_);
    render(act);
    return;
  }
  if (act.mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    adjustPercent(percent_, -kCoarseStep, changed_);
    render(act);
    return;
  }
  if (act.mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
    adjustPercent(percent_, kCoarseStep, changed_);
    render(act);
    return;
  }
}

void GoToPercentUi::render(EpubActivity& act) {
  GfxRenderer& renderer = act.renderer;
  const PopupBounds bounds = popupBounds(renderer);
  renderer.rectangle.fill(bounds.x, bounds.y, bounds.width, bounds.height, false);

  const int titleFont = systemFontId();
  const int titleHeight = renderer.text.getLineHeight(titleFont);
  renderer.text.centered(titleFont, bounds.y + 25 + titleHeight / 2, "Go to Percent", true,
                         EpdFontFamily::BOLD);

  renderer.bitmap.icon(LibraryFilterLeft, bounds.trackLeft - kCaretGap - kCaretSize,
                       bounds.trackY - kCaretSize / 2, kCaretSize, kCaretSize);
  renderer.bitmap.icon(LibraryFilterRight, bounds.trackRight + kCaretGap,
                       bounds.trackY - kCaretSize / 2, kCaretSize, kCaretSize);
  renderer.rectangle.fill(bounds.trackLeft, bounds.trackY - kTrackHeight / 2,
                          bounds.trackRight - bounds.trackLeft, kTrackHeight, true, true);

  const int knobX = bounds.trackLeft + (bounds.trackRight - bounds.trackLeft) * percent_ / 100;
  drawKnob(renderer, knobX, bounds.trackY);

  char percentText[6];
  std::snprintf(percentText, sizeof(percentText), "%d%%", percent_);
  const int textWidth = renderer.text.getWidth(MONTSERRAT_8_FONT_ID, percentText, EpdFontFamily::BOLD);
  const int textHeight = renderer.text.getLineHeight(MONTSERRAT_8_FONT_ID);
  renderer.text.render(MONTSERRAT_8_FONT_ID, knobX - textWidth / 2, bounds.trackY - textHeight / 2,
                       percentText, true, EpdFontFamily::BOLD);

  renderer.rectangle.render(bounds.x, bounds.y, bounds.width, bounds.height, true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
