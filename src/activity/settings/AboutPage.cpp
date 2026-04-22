/**
 * @file AboutPage.cpp
 * @brief Definitions for AboutPage.
 */

#include "AboutPage.h"

#include "system/Fonts.h"

AboutPage::AboutPage(GfxRenderer& renderer, MappedInputManager& mappedInput, DismissCallback onDismiss,
                     CheckForUpdatesCallback onCheckForUpdates)
    : renderer(renderer),
      mappedInput(mappedInput),
      onDismiss(std::move(onDismiss)),
      onCheckForUpdates(std::move(onCheckForUpdates)),
      visible(false),
      dismissed(false),
      lastInputTime(0) {}

AboutPage::~AboutPage() {
  onDismiss = nullptr;
  onCheckForUpdates = nullptr;
}

void AboutPage::show() {
  if (visible) return;
  visible = true;
  dismissed = false;
  renderWithRefresh();
}

void AboutPage::hide() {
  visible = false;
  dismissed = true;
}

void AboutPage::handleInput() {
  if (!visible) return;

  const uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    hide();
    if (onDismiss) {
      onDismiss();
    }
    lastInputTime = currentTime;
    return;
  }

  if (onCheckForUpdates && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    visible = false;
    dismissed = false;
    lastInputTime = currentTime;
    onCheckForUpdates();
    return;
  }
}

void AboutPage::render() {
  if (!visible) return;
  renderWithRefresh();
}

void AboutPage::renderWithRefresh() {
  if (!visible) return;

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  const int popupWidth = 400;
  const int popupHeight = 320;
  const int popupX = (screenWidth - popupWidth) / 2;
  const int popupY = (screenHeight - popupHeight) / 2;

  renderer.fillRect(popupX, popupY, popupWidth, popupHeight, false);
  renderer.drawRect(popupX, popupY, popupWidth, popupHeight, true);

  int yPos = popupY + 28;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_18_FONT_ID, popupX + 24, yPos, "Inx", true, EpdFontFamily::BOLD);
  yPos += 36;

  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, popupX + 24, yPos, "Current version", true, EpdFontFamily::BOLD);
  yPos += 22;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, popupX + 24, yPos, INX_VERSION, true, EpdFontFamily::REGULAR);
  yPos += 36;

  if (onCheckForUpdates) {
    constexpr int btnW = 220;
    constexpr int btnH = 48;
    const int btnX = popupX + (popupWidth - btnW) / 2;
    renderer.fillRect(btnX, yPos, btnW, btnH, false);
    renderer.drawRect(btnX, yPos, btnW, btnH, true);
    const char* updateLabel = "Update";
    const int tw = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, updateLabel, EpdFontFamily::BOLD);
    const int tx = btnX + (btnW - tw) / 2;
    const int ty = yPos + (btnH - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
    renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, tx, ty, updateLabel, true, EpdFontFamily::BOLD);
  }

  const char* confirmHint = onCheckForUpdates ? "Update" : "";
  const auto labels = mappedInput.mapLabels("Close", confirmHint, "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
