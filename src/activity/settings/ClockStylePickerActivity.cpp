/**
 * @file ClockStylePickerActivity.cpp
 * @brief Picker for date/time sleep screen clock designs.
 */

#include "ClockStylePickerActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/SleepClockRenderer.h"

namespace {
SleepClockRenderer::DateTimeView previewDateTime() {
  SleepClockRenderer::DateTimeView dt;
  dt.year = 2026;
  dt.month = 6;
  dt.day = 2;
  dt.hour = 10;
  dt.minute = 24;
  dt.weekday = 2;
  return dt;
}
}  // namespace

void ClockStylePickerActivity::taskTrampoline(void* param) {
  auto* self = static_cast<ClockStylePickerActivity*>(param);
  self->displayTaskLoop();
}

void ClockStylePickerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();

  selectedIndex = SETTINGS.sleepClockStyle;
  if (selectedIndex >= SleepClockRenderer::styleCount()) {
    selectedIndex = 0;
  }

  updateRequired = true;
  xTaskCreate(&ClockStylePickerActivity::taskTrampoline, "ClockStylePickerTask", 4096, this, 1, &displayTaskHandle);
}

void ClockStylePickerActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void ClockStylePickerActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void ClockStylePickerActivity::render() {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  const auto dt = previewDateTime();
  SleepClockRenderer::render(renderer, selectedIndex, dt, true, 0, 0, pageWidth, pageHeight);

  const char* name = SleepClockRenderer::styleName(selectedIndex);
  renderer.rectangle.fill(0, 0, pageWidth, 24, false);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 8, 6, name, true, EpdFontFamily::BOLD);
  char countText[8];
  std::snprintf(countText, sizeof(countText), "%d/%d", selectedIndex + 1, SleepClockRenderer::styleCount());
  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID,
                       pageWidth - renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, countText) - 8, 6, countText,
                       true);

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Prev", "Next");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void ClockStylePickerActivity::applySelection() {
  SETTINGS.sleepClockStyle = static_cast<uint8_t>(selectedIndex);
  SETTINGS.sleepScreen = SystemSetting::SLEEP_SCREEN_MODE::DATETIME;
  SETTINGS.saveToFile();
  onBack();
}

void ClockStylePickerActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    applySelection();
    return;
  }

  bool needRedraw = false;

  if (mappedInput.wasPressed(MenuNav::itemPrev()) || mappedInput.wasPressed(MenuNav::tabPrev())) {
    selectedIndex = (selectedIndex + SleepClockRenderer::styleCount() - 1) % SleepClockRenderer::styleCount();
    needRedraw = true;
  } else if (mappedInput.wasPressed(MenuNav::itemNext()) || mappedInput.wasPressed(MenuNav::tabNext())) {
    selectedIndex = (selectedIndex + 1) % SleepClockRenderer::styleCount();
    needRedraw = true;
  }

  if (needRedraw) {
    updateRequired = true;
  }
}
