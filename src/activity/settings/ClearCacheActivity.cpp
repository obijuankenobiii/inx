#include "ClearCacheActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include "system/MappedInputManager.h"
#include "system/Fonts.h"

void ClearCacheActivity::taskTrampoline(void* param) {
  auto* self = static_cast<ClearCacheActivity*>(param);
  self->displayTaskLoop();
}

void ClearCacheActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  state = WARNING;
  updateRequired = true;

  xTaskCreate(&ClearCacheActivity::taskTrampoline, "ClearCacheActivityTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void ClearCacheActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void ClearCacheActivity::displayTaskLoop() {
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

void ClearCacheActivity::render() {
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 15, "Clear Cache", true, EpdFontFamily::BOLD);

  if (state == WARNING) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 - 60, "This will clear all cached book data.", true);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 - 30, "All reading progress will be lost!", true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 + 10, "Books will need to be re-indexed", true);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 + 30, "when opened again.", true);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 + 60, "App state will also be reset.", true,
                              EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels("« Cancel", "Clear", "", "");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CLEARING) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2, "Clearing cache...", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 - 20, "Cache Cleared", true, EpdFontFamily::BOLD);
    String resultText = String(clearedCount) + " items removed";
    if (failedCount > 0) {
      resultText += ", " + String(failedCount) + " failed";
    }
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 + 10, resultText.c_str());

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 - 20, "Failed to clear cache", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 + 10, "Check serial output for details");

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void ClearCacheActivity::clearCache() {
  Serial.printf("[%lu] [CLEAR_CACHE] Clearing cache...\n", millis());

  clearedCount = 0;
  failedCount = 0;

  // Delete .metadata directory completely
  if (SdMan.exists("/.system")) {
    Serial.printf("[%lu] [CLEAR_CACHE] Removing /.metadata directory\n", millis());
    if (SdMan.removeDir("/.system")) {
      clearedCount++;
      Serial.printf("[%lu] [CLEAR_CACHE] Successfully removed /.metadata\n", millis());
    } else {
      failedCount++;
      Serial.printf("[%lu] [CLEAR_CACHE] Failed to remove /.metadata\n", millis());
    }
  } else {
    Serial.printf("[%lu] [CLEAR_CACHE] /.metadata directory not found\n", millis());
  }

  // Delete .metadata directory completely
  if (SdMan.exists("/.metadata")) {
    Serial.printf("[%lu] [CLEAR_CACHE] Removing /.metadata directory\n", millis());
    if (SdMan.rmdir("/.metadata")) {
      clearedCount++;
      Serial.printf("[%lu] [CLEAR_CACHE] Successfully removed /.metadata\n", millis());
    } else {
      failedCount++;
      Serial.printf("[%lu] [CLEAR_CACHE] Failed to remove /.metadata\n", millis());
    }
  } else {
    Serial.printf("[%lu] [CLEAR_CACHE] /.metadata directory not found\n", millis());
  }


  Serial.printf("[%lu] [CLEAR_CACHE] Cache cleared: %d removed, %d failed\n", millis(), clearedCount, failedCount);

  state = SUCCESS;
  updateRequired = true;
}

void ClearCacheActivity::loop() {
  if (state == WARNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      Serial.printf("[%lu] [CLEAR_CACHE] User confirmed, starting cache clear\n", millis());
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = CLEARING;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      vTaskDelay(10 / portTICK_PERIOD_MS);

      clearCache();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      Serial.printf("[%lu] [CLEAR_CACHE] User cancelled\n", millis());
      goBack();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }
}