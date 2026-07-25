/**
 * @file OrientationPickerUi.cpp
 * @brief Definitions for OrientationPickerUi.
 */

#include "OrientationPickerUi.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>

#include "EpubActivity.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiTheme.h"

namespace {
constexpr const char* kOrientationLabels[] = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"};
constexpr int kOrientationCount = 4;
}  // namespace

void OrientationPickerUi::enter(EpubActivity& act) {
  mode_ = true;
  selected_ = std::min<int>(act.bookSettings.orientation, kOrientationCount - 1);
  render(act);
}

void OrientationPickerUi::handleInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;

  if (m.wasReleased(MappedInputManager::Button::Back)) {
    mode_ = false;
    act.renderScreen(true);  // redraw the clean reader page, erasing the popup
    return;
  }
  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    mode_ = false;
    act.bookSettings.orientation = static_cast<uint8_t>(selected_);
    act.bookSettings.markCustomSettings();
    act.saveBookSettings();
    // Same deferred relayout path the Book Settings drawer uses when orientation changes - the next
    // loop() picks up bookSettings.orientation != bookLayoutAppliedOrientation_ and rebuilds the page.
    act.isToggleClosed = true;
    act.updateRequired = true;
    return;
  }
  if (m.wasPressed(MappedInputManager::Button::Up)) {
    selected_ = (selected_ - 1 + kOrientationCount) % kOrientationCount;
    render(act);
    return;
  }
  if (m.wasPressed(MappedInputManager::Button::Down)) {
    selected_ = (selected_ + 1) % kOrientationCount;
    render(act);
    return;
  }
}

void OrientationPickerUi::render(EpubActivity& act) {
  GfxRenderer& renderer = act.renderer;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  const int boxW = std::min(screenW - 60, 320);
  constexpr int rowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT - 4;
  const int overlayHeaderH = INX_THEME.drawerHeaderHeight() - 4;
  const int boxH = overlayHeaderH + kOrientationCount * rowH;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = (screenH - boxH) / 2;

  renderer.rectangle.fill(boxX, boxY, boxW, boxH, false);

  const int titleY = boxY + (overlayHeaderH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, boxX + 16, titleY, "Change Orientation", true,
                       EpdFontFamily::BOLD);

  for (int i = 0; i < kOrientationCount; ++i) {
    const int rowY = boxY + overlayHeaderH + i * rowH;
    const bool sel = (i == selected_);
    if (sel) {
      renderer.rectangle.fill(boxX + 1, rowY, boxW - 2, rowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    const int textY = rowY + (rowH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, boxX + 20, textY, kOrientationLabels[i], sel ? 0 : 1);
    if (i + 1 < kOrientationCount) {
      renderer.line.render(boxX, rowY + rowH, boxX + boxW, rowY + rowH, !sel, LineRender::Style::Dotted);
    }
  }

  renderer.line.render(boxX, boxY + overlayHeaderH, boxX + boxW, boxY + overlayHeaderH, true);

  renderer.rectangle.render(boxX, boxY, boxW, boxH, true);
  renderer.rectangle.render(boxX + 1, boxY + 1, boxW - 2, boxH - 2, true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
