/**
 * @file OtaUpdateActivity.cpp
 * @brief Definitions for OtaUpdateActivity.
 */

#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <WiFi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <string>

#include "activity/network/WifiSelectionActivity.h"
#include "network/OtaUpdater.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

// cppcheck-suppress missingInclude
#include "esp_task_wdt.h"

namespace {
constexpr const char* kSdFirmwarePath = "/firmware.bin";
constexpr int kSourceItemHeight = 56;
}

void OtaUpdateActivity::taskTrampoline(void* param) {
  auto* self = static_cast<OtaUpdateActivity*>(param);
  self->displayTaskLoop();
}

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  exitActivity();

  if (!success) {
    Serial.printf("[%lu] [OTA] WiFi connection failed, exiting\n", millis());
    goBack();
    return;
  }

  Serial.printf("[%lu] [OTA] WiFi connected, checking for update\n", millis());

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = CHECKING_FOR_UPDATE;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
  vTaskDelay(pdMS_TO_TICKS(450));
  Serial.printf("[%lu] [OTA] free heap before update check: %u bytes\n", millis(),
                static_cast<unsigned>(ESP.getFreeHeap()));

  const auto res = updater.checkForUpdate();
  if (res != OtaUpdater::OK) {
    Serial.printf("[%lu] [OTA] Update check failed: %d\n", millis(), res);
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = FAILED;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  if (!updater.isUpdateNewer()) {
    Serial.printf("[%lu] [OTA] No new update available\n", millis());
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = NO_UPDATE;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = WAITING_CONFIRMATION;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
}

void OtaUpdateActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  state = SOURCE_SELECTION;
  sourceSelectedIndex = 0;
  updateRequired = true;

  xTaskCreate(&OtaUpdateActivity::taskTrampoline, "OtaUpdateActivityTask",
              4096,               
              this,               
              1,                  
              &displayTaskHandle  
  );

  Serial.printf("[%lu] [OTA] Waiting for update source selection\n", millis());
}

void OtaUpdateActivity::onExit() {
  ActivityWithSubactivity::onExit();

  
  WiFi.disconnect(false);  
  delay(100);              
  WiFi.mode(WIFI_OFF);
  delay(100);  

  
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void OtaUpdateActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired || updater.getRender()) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void OtaUpdateActivity::render() {
  if (subActivity) {
    return;
  }

  float updaterProgress = 0;
  if (state == UPDATE_IN_PROGRESS && updater.getTotalSize() > 0) {
    updaterProgress = static_cast<float>(updater.getProcessedSize()) / static_cast<float>(updater.getTotalSize());
  }

  renderer.clearScreen();
  renderTabBar(renderer);

  const int pageWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int startY = TAB_BAR_HEIGHT;
  const int headerHeight = TAB_BAR_HEIGHT;
  const int headerY = startY;

  const char* headerText = "Update";
  int headerTextY = headerY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, headerTextY - 10, headerText, true, EpdFontFamily::BOLD);

  const char* subtitleText = "";
  switch (state) {
    case SOURCE_SELECTION:
      subtitleText = "Choose update source";
      break;
    case WIFI_SELECTION:
      subtitleText = "Select Wi-Fi to continue";
      break;
    case CHECKING_FOR_UPDATE:
      subtitleText = "Checking for update...";
      break;
    case WAITING_CONFIRMATION:
      subtitleText = "New update available!";
      break;
    case WAITING_SD_CONFIRMATION:
      subtitleText = "Install firmware from SD";
      break;
    case UPDATE_IN_PROGRESS:
      subtitleText = "Downloading firmware...";
      break;
    case NO_UPDATE:
      subtitleText = "No update available";
      break;
    case FAILED:
      subtitleText = "Update failed";
      break;
    case FINISHED:
    case SHUTTING_DOWN:
      subtitleText = "Update complete";
      break;
  }

  int subtitleY = headerY + 40;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true, EpdFontFamily::REGULAR);

  const int dividerY = subtitleY + renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.line.render(0, dividerY, pageWidth, dividerY);

  const int bodyTop = dividerY + 16;

  if (state == SOURCE_SELECTION) {
    constexpr const char* items[] = {"Online update", "SD card firmware"};
    for (int i = 0; i < 2; ++i) {
      const int itemY = bodyTop + i * kSourceItemHeight;
      const bool selected = sourceSelectedIndex == i;
      if (selected) {
        renderer.rectangle.fill(0, itemY, pageWidth, kSourceItemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
      }
      const int textY = itemY + (kSourceItemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, items[i], !selected, EpdFontFamily::REGULAR);
      renderer.line.render(0, itemY + kSourceItemHeight - 1, pageWidth, itemY + kSourceItemHeight - 1);
    }
    const auto labels = mappedInput.mapLabels("« Back", "Select", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == WIFI_SELECTION) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Choose a network above.", true,
                              EpdFontFamily::REGULAR);
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == CHECKING_FOR_UPDATE) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "This may take a moment.", true,
                              EpdFontFamily::REGULAR);
  } else if (state == WAITING_CONFIRMATION) {
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, bodyTop, "Current Version: " INX_VERSION, true,
                      EpdFontFamily::REGULAR);
    const std::string newVer = "New Version: " + updater.getLatestVersion();
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, bodyTop + 28, newVer.c_str(), true, EpdFontFamily::REGULAR);
    const auto labels = mappedInput.mapLabels("Cancel", "Update", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == WAITING_SD_CONFIRMATION) {
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, bodyTop,
                         ("File: " + std::string(kSdFirmwarePath)).c_str(), true, EpdFontFamily::REGULAR);
    if (SdMan.exists(kSdFirmwarePath)) {
      FsFile file;
      size_t firmwareSize = 0;
      if (SdMan.openFileForRead("OTA", kSdFirmwarePath, file)) {
        firmwareSize = file.size();
        file.close();
      }
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, bodyTop + 28,
                           ("Size: " + std::to_string(firmwareSize) + " bytes").c_str(), true,
                           EpdFontFamily::REGULAR);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, bodyTop + 64, "Install this firmware?", true,
                           EpdFontFamily::BOLD);
      const auto labels = mappedInput.mapLabels("Cancel", "Install", "", "");
      renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else {
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, bodyTop + 36, "Put firmware.bin in the SD root.",
                           true, EpdFontFamily::REGULAR);
      const auto labels = mappedInput.mapLabels("« Back", "", "", "");
      renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  } else if (state == UPDATE_IN_PROGRESS) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyTop + 8, "Updating...", true, EpdFontFamily::BOLD);
    renderer.rectangle.render(20, bodyTop + 36, pageWidth - 40, 50);
    renderer.rectangle.fill(24, bodyTop + 40, static_cast<int>(updaterProgress * static_cast<float>(pageWidth - 44)), 42);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyTop + 106,
                              (std::to_string(static_cast<int>(updaterProgress * 100)) + "%").c_str());
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyTop + 130,
                              (std::to_string(updater.getProcessedSize()) + " / " + std::to_string(updater.getTotalSize()))
                                  .c_str());
  } else if (state == NO_UPDATE) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "No update available", true,
                              EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FAILED) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Update failed", true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FINISHED) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Update complete", true, EpdFontFamily::BOLD);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY + 50,
                              "Press and hold power button to turn back on", true, EpdFontFamily::REGULAR);
  } else if (state == SHUTTING_DOWN) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Update complete", true, EpdFontFamily::BOLD);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY + 50,
                              "Press and hold power button to turn back on", true, EpdFontFamily::REGULAR);
  }

  renderer.displayBuffer();

  if (state == FINISHED) {
    state = SHUTTING_DOWN;
  }
}

void OtaUpdateActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (state == SOURCE_SELECTION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      sourceSelectedIndex = sourceSelectedIndex == 0 ? 1 : 0;
      updateRequired = true;
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (sourceSelectedIndex == 0) {
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = WIFI_SELECTION;
        xSemaphoreGive(renderingMutex);
        updateRequired = true;
        Serial.printf("[%lu] [OTA] Turning on WiFi...\n", millis());
        WiFi.mode(WIFI_STA);
        Serial.printf("[%lu] [OTA] Launching WifiSelectionActivity...\n", millis());
        enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                                   [this](const bool connected) { onWifiSelectionComplete(connected); }));
      } else {
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = WAITING_SD_CONFIRMATION;
        xSemaphoreGive(renderingMutex);
        updateRequired = true;
      }
      return;
    }
    return;
  }

  if (state == WIFI_SELECTION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == WAITING_CONFIRMATION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      Serial.printf("[%lu] [OTA] New update available, starting download...\n", millis());
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = UPDATE_IN_PROGRESS;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      vTaskDelay(10 / portTICK_PERIOD_MS);
      const auto res = updater.installUpdate();

      if (res != OtaUpdater::OK) {
        Serial.printf("[%lu] [OTA] Update failed: %d\n", millis(), res);
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = FAILED;
        xSemaphoreGive(renderingMutex);
        updateRequired = true;
        return;
      }

      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = FINISHED;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }

    return;
  }

  if (state == WAITING_SD_CONFIRMATION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = SOURCE_SELECTION;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && SdMan.exists(kSdFirmwarePath)) {
      Serial.printf("[%lu] [OTA] Installing firmware from SD...\n", millis());
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = UPDATE_IN_PROGRESS;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      vTaskDelay(10 / portTICK_PERIOD_MS);
      const auto res = updater.installUpdateFromSd(kSdFirmwarePath);

      if (res != OtaUpdater::OK) {
        Serial.printf("[%lu] [OTA] SD update failed: %d\n", millis(), res);
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = FAILED;
        xSemaphoreGive(renderingMutex);
        updateRequired = true;
        return;
      }

      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = FINISHED;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
    }
    return;
  }

  if (state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == NO_UPDATE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}
