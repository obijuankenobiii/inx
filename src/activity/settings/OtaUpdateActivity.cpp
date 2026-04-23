/**
 * @file OtaUpdateActivity.cpp
 * @brief Definitions for OtaUpdateActivity.
 */

#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <string>

#include "activity/network/WifiSelectionActivity.h"
#include "network/OtaUpdater.h"
#include "state/NetworkCredential.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

// cppcheck-suppress missingInclude
#include "esp_task_wdt.h"

void OtaUpdateActivity::taskTrampoline(void* param) {
  auto* self = static_cast<OtaUpdateActivity*>(param);
  self->displayTaskLoop();
}

bool OtaUpdateActivity::tryConnectUsingStoredCredentials() {
  WIFI_STORE.loadFromFile();
  const auto& creds = WIFI_STORE.getCredentials();
  if (creds.empty()) {
    Serial.printf("[%lu] [OTA] No saved Wi-Fi credentials\n", millis());
    return false;
  }

  WiFi.mode(WIFI_STA);

  // cppcheck-suppress useStlAlgorithm
  for (const auto& cred : creds) {
    if (cred.ssid.empty()) {
      continue;
    }

    WiFi.disconnect(false);
    vTaskDelay(pdMS_TO_TICKS(150));

    if (!cred.password.empty()) {
      WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
    } else {
      WiFi.begin(cred.ssid.c_str());
    }

    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[%lu] [OTA] Connected with saved Wi-Fi: %s\n", millis(), cred.ssid.c_str());
      return true;
    }

    Serial.printf("[%lu] [OTA] Saved Wi-Fi connect failed: %s\n", millis(), cred.ssid.c_str());
    WiFi.disconnect(false);
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  return false;
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

  xTaskCreate(&OtaUpdateActivity::taskTrampoline, "OtaUpdateActivityTask",
              4096,               
              this,               
              1,                  
              &displayTaskHandle  
  );

  Serial.printf("[%lu] [OTA] Turning on WiFi...\n", millis());
  WiFi.mode(WIFI_STA);

  if (tryConnectUsingStoredCredentials()) {
    Serial.printf("[%lu] [OTA] Using saved Wi-Fi (skipped picker)\n", millis());
    onWifiSelectionComplete(true);
  } else {
    Serial.printf("[%lu] [OTA] Launching WifiSelectionActivity...\n", millis());
    enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                                 [this](const bool connected) { onWifiSelectionComplete(connected); }));
  }
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
  int headerTextY = headerY + (headerHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, headerTextY - 10, headerText, true, EpdFontFamily::BOLD);

  const char* subtitleText = "";
  switch (state) {
    case WIFI_SELECTION:
      subtitleText = "Select Wi-Fi to continue";
      break;
    case CHECKING_FOR_UPDATE:
      subtitleText = "Checking for update...";
      break;
    case WAITING_CONFIRMATION:
      subtitleText = "New update available!";
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
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true, EpdFontFamily::REGULAR);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, pageWidth, dividerY);

  const int bodyTop = dividerY + 16;

  if (state == WIFI_SELECTION) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Choose a network above.", true,
                              EpdFontFamily::REGULAR);
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == CHECKING_FOR_UPDATE) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "This may take a moment.", true,
                              EpdFontFamily::REGULAR);
  } else if (state == WAITING_CONFIRMATION) {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, bodyTop, "Current Version: " INX_VERSION, true,
                      EpdFontFamily::REGULAR);
    const std::string newVer = "New Version: " + updater.getLatestVersion();
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, bodyTop + 28, newVer.c_str(), true, EpdFontFamily::REGULAR);
    const auto labels = mappedInput.mapLabels("Cancel", "Update", "", "");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == UPDATE_IN_PROGRESS) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyTop + 8, "Updating...", true, EpdFontFamily::BOLD);
    renderer.drawRect(20, bodyTop + 36, pageWidth - 40, 50);
    renderer.fillRect(24, bodyTop + 40, static_cast<int>(updaterProgress * static_cast<float>(pageWidth - 44)), 42);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyTop + 106,
                              (std::to_string(static_cast<int>(updaterProgress * 100)) + "%").c_str());
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyTop + 130,
                              (std::to_string(updater.getProcessedSize()) + " / " + std::to_string(updater.getTotalSize()))
                                  .c_str());
  } else if (state == NO_UPDATE) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "No update available", true,
                              EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FAILED) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Update failed", true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FINISHED) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Update complete", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY + 50,
                              "Press and hold power button to turn back on", true, EpdFontFamily::REGULAR);
  } else if (state == SHUTTING_DOWN) {
    const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Update complete", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY + 50,
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
