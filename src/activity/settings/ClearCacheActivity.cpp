/**
 * @file ClearCacheActivity.cpp
 * @brief Definitions for ClearCacheActivity.
 */

#include "ClearCacheActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include "state/BookState.h"
#include "state/NetworkCredential.h"
#include "state/RecentBooks.h"
#include "state/Session.h"
#include "state/SystemSetting.h"
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
              4096,               
              this,               
              1,                  
              &displayTaskHandle  
  );
}

void ClearCacheActivity::onExit() {
  ActivityWithSubactivity::onExit();

  
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
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 15, "Reset device", true, EpdFontFamily::BOLD);

  if (state == WARNING) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 - 60, "This removes /.system and /.metadata", true);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 - 30, "from the SD card (settings, Wi-Fi,", true);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2, "book caches, library index, recent list).", true);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 + 30, "Reading progress in book caches is lost.", true,
                              EpdFontFamily::BOLD);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 + 60, "In‑memory state reloads after reset.", true);

    const auto labels = mappedInput.mapLabels("« Cancel", "Reset", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CLEARING) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2, "Resetting...", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 - 20, "Reset complete", true, EpdFontFamily::BOLD);
    String resultText = String(clearedCount) + " items removed";
    if (failedCount > 0) {
      resultText += ", " + String(failedCount) + " failed";
    }
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 + 10, resultText.c_str());

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 - 20, "Reset failed", true, EpdFontFamily::BOLD);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 + 10, "Check serial output for details");

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void ClearCacheActivity::clearCache() {
  Serial.printf("[%lu] [RESET_DEVICE] Removing /.system and /.metadata...\n", millis());

  clearedCount = 0;
  failedCount = 0;

  const auto tryRemoveTree = [&](const char* path) {
    if (!SdMan.exists(path)) {
      Serial.printf("[%lu] [RESET_DEVICE] %s not present\n", millis(), path);
      return;
    }
    Serial.printf("[%lu] [RESET_DEVICE] Removing %s\n", millis(), path);
    if (SdMan.removeDir(path)) {
      clearedCount++;
      Serial.printf("[%lu] [RESET_DEVICE] Removed %s\n", millis(), path);
    } else {
      failedCount++;
      Serial.printf("[%lu] [RESET_DEVICE] Failed to remove %s\n", millis(), path);
    }
  };

  tryRemoveTree("/.system");
  tryRemoveTree("/.metadata");

  Serial.printf("[%lu] [RESET_DEVICE] Done: %d removed, %d failed\n", millis(), clearedCount, failedCount);

  if (failedCount > 0 && clearedCount == 0) {
    state = FAILED;
    updateRequired = true;
    return;
  }

  (void)SETTINGS.loadFromFile();
  (void)RECENT_BOOKS.loadFromFile();
  (void)BOOK_STATE.loadFromFile();
  (void)APP_STATE.loadFromFile();
  (void)WIFI_STORE.loadFromFile();

  state = SUCCESS;
  updateRequired = true;
}

void ClearCacheActivity::loop() {
  if (state == WARNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      Serial.printf("[%lu] [RESET_DEVICE] User confirmed\n", millis());
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = CLEARING;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      vTaskDelay(10 / portTICK_PERIOD_MS);

      clearCache();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      Serial.printf("[%lu] [RESET_DEVICE] User cancelled\n", millis());
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