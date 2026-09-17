/**
 * @file KeyboardEntryActivity.cpp
 * @brief Definitions for KeyboardEntryActivity.
 */

#include "KeyboardEntryActivity.h"

#include <algorithm>

#include "activity/page/SubPage.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr int PAGE_MARGIN = 18;
/** Stack size (bytes) for xTaskCreate; 2048 overflowed with render() + GfxRenderer on ESP32-C3. */
constexpr uint32_t kDisplayTaskStackBytes = 8192;
}  // namespace

void KeyboardEntryActivity::taskTrampoline(void* param) {
  auto* self = static_cast<KeyboardEntryActivity*>(param);
  self->displayTaskLoop();
}

void KeyboardEntryActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();

  keyboard.reset();
  keyboard.setMaxLength(maxLength);
  renderingMutex = xSemaphoreCreateMutex();

  updateRequired = true;

  xTaskCreate(&KeyboardEntryActivity::taskTrampoline, "KeyboardEntryActivity", kDisplayTaskStackBytes, this, 1,
              &displayTaskHandle);
}

void KeyboardEntryActivity::onExit() {
  Activity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void KeyboardEntryActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    keyboard.moveVertical(-1);
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    keyboard.moveVertical(1);
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    keyboard.moveHorizontal(-1);
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    keyboard.moveHorizontal(1);
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    const SearchKeyboard::Action action = keyboard.activate(text);
    if (action == SearchKeyboard::Action::Go && onComplete) {
      onComplete(text);
    }
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onCancel) {
      onCancel();
    }
    updateRequired = true;
  }
}

void KeyboardEntryActivity::render() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  constexpr int inputFont = MONTSERRAT_12_FONT_ID;
  constexpr int hintFont = MONTSERRAT_10_FONT_ID;

  const int bodyTop = SubPage::header(renderer, title.c_str());

  std::string displayText;
  if (isPassword) {
    displayText = std::string(text.length(), '*');
  } else {
    displayText = text;
  }

  displayText += "_";

  const int inputX = PAGE_MARGIN;
  const int inputY = bodyTop + 2;
  const int inputW = pageWidth - PAGE_MARGIN * 2;
  constexpr int inputH = 56;
  renderer.rectangle.render(inputX, inputY, inputW, inputH, true, true);

  std::string inputLine = renderer.text.truncate(inputFont, displayText.c_str(), inputW - 24);
  const int inputTextY = inputY + (inputH - renderer.text.getLineHeight(inputFont)) / 2;
  renderer.text.render(inputFont, inputX + 12, inputTextY, inputLine.c_str(), true);

  if (maxLength > 0) {
    char countText[24];
    snprintf(countText, sizeof(countText), "%u/%u", static_cast<unsigned>(text.length()),
             static_cast<unsigned>(maxLength));
    const int countW = renderer.text.getWidth(hintFont, countText);
    renderer.text.render(hintFont, inputX + inputW - countW - 10, inputY + inputH + 8, countText, true);
  }

  keyboard.render(renderer, std::max(startY, inputY + inputH + 20), pageHeight);

  renderer.displayBuffer();
}
