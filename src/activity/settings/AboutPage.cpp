#include "AboutPage.h"

#include "system/Fonts.h"
#include "system/ScreenComponents.h"

AboutPage::AboutPage(GfxRenderer& renderer, MappedInputManager& mappedInput, DismissCallback onDismiss)
    : renderer(renderer),
      mappedInput(mappedInput),
      onDismiss(onDismiss),
      visible(false),
      dismissed(false),
      lastInputTime(0) {}

AboutPage::~AboutPage() { onDismiss = nullptr; }

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

  uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    hide();
    if (onDismiss) {
      onDismiss();
    }
    lastInputTime = currentTime;
    renderWithRefresh();
  }
}

void AboutPage::render() {
  if (!visible) return;
  renderWithRefresh();
}

void AboutPage::renderWithRefresh() {
  if (!visible) return;

  int screenWidth = renderer.getScreenWidth();
  int screenHeight = renderer.getScreenHeight();

  int popupWidth = 300;
  int popupHeight = 200;
  int popupX = (screenWidth - popupWidth) / 2;
  int popupY = (screenHeight - popupHeight) / 2;

  renderer.fillRect(popupX, popupY, popupWidth, popupHeight, false);
  renderer.drawRect(popupX, popupY, popupWidth, popupHeight, true);

  int yPos = popupY + 20;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_18_FONT_ID, popupX + 20, yPos, "Inx", true, EpdFontFamily::BOLD);
  renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, popupX + 90, yPos + 20, INX_VERSION, true);

  renderer.drawRect(popupX + 20, yPos + 110, 130, 45, true);
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, popupX + 40, yPos + 120, "Update");
  renderer.displayBuffer();
}