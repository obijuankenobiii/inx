#include "EpubPercentSelectionActivity.h"

#include <GfxRenderer.h>

#include "system/MappedInputManager.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"

namespace {
// Fine/coarse slider step sizes for percent adjustments.
constexpr int kSmallStep = 1;
constexpr int kLargeStep = 10;
}  // namespace

void EpubPercentSelectionActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  render();
}

void EpubPercentSelectionActivity::onExit() {
  ActivityWithSubactivity::onExit();
}

void EpubPercentSelectionActivity::adjustPercent(const int delta) {
  // Apply delta and clamp within 0-100.
  percent += delta;
  if (percent < 0) {
    percent = 0;
  } else if (percent > 100) {
    percent = 100;
  }
  render();
}

void EpubPercentSelectionActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onCancel) onCancel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (onPercentSelected) onPercentSelected(percent);
    return;
  }

  unsigned long now = millis();
  
  if (mappedInput.wasPressed(MappedInputManager::Button::Left) || 
      (mappedInput.isPressed(MappedInputManager::Button::Left) && now - lastStepTime > 150)) {
    adjustPercent(-kSmallStep);
    lastStepTime = now;
  }
  else if (mappedInput.wasPressed(MappedInputManager::Button::Right) || 
           (mappedInput.isPressed(MappedInputManager::Button::Right) && now - lastStepTime > 150)) {
    adjustPercent(kSmallStep);
    lastStepTime = now;
  }
  else if (mappedInput.wasPressed(MappedInputManager::Button::Up) || 
           (mappedInput.isPressed(MappedInputManager::Button::Up) && now - lastStepTime > 150)) {
    adjustPercent(kLargeStep);
    lastStepTime = now;
  }
  else if (mappedInput.wasPressed(MappedInputManager::Button::Down) || 
           (mappedInput.isPressed(MappedInputManager::Button::Down) && now - lastStepTime > 150)) {
    adjustPercent(-kLargeStep);
    lastStepTime = now;
  }
}

void EpubPercentSelectionActivity::render() {
  renderer.clearScreen();

  // Title and numeric percent value.
  const char* titleText = "Go to Percent";
  const int titleWidth = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, titleText, EpdFontFamily::BOLD);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, (renderer.getScreenWidth() - titleWidth) / 2, 15, titleText, true, EpdFontFamily::BOLD);

  const std::string percentText = std::to_string(percent) + "%";
  const int pctWidth = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, percentText.c_str(), EpdFontFamily::BOLD);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, (renderer.getScreenWidth() - pctWidth) / 2, 90, percentText.c_str(), true, EpdFontFamily::BOLD);

  // Draw slider track.
  const int screenWidth = renderer.getScreenWidth();
  constexpr int barWidth = 360;
  constexpr int barHeight = 16;
  const int barX = (screenWidth - barWidth) / 2;
  const int barY = 140;

  renderer.rectangle.render(barX, barY, barWidth, barHeight, true);

  // Fill slider based on percent.
  const int fillWidth = (barWidth - 4) * percent / 100;
  if (fillWidth > 0) {
    renderer.rectangle.fill(barX + 2, barY + 2, fillWidth, barHeight - 4, true);
  }

  // Draw a simple knob centered at the current percent.
  const int knobX = barX + 2 + fillWidth - 2;
  renderer.rectangle.fill(knobX, barY - 4, 4, barHeight + 8, true);

  // Hint text for step sizes.
  const char* hintText = "L/R: +/-1%, Up/Down: +/-10%";
  const int hintWidth = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, hintText);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, (screenWidth - hintWidth) / 2, barY + 30, hintText, true);

  // Button hints
  const auto labels = mappedInput.mapLabels("Back", "Select", "-", "+");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
