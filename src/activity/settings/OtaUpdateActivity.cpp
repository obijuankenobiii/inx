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

#include <algorithm>
#include <string>

#include "activity/network/WifiSelectionActivity.h"
#include "network/OtaUpdater.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

// cppcheck-suppress missingInclude
#include "esp_task_wdt.h"

namespace {
constexpr int kSourceItemHeight = 56;
constexpr int kFirmwareItemHeight = 46;
constexpr int kUpdateCardMargin = 24;
constexpr int kUpdateCardBarHeight = 10;
const std::string kEmptyPath;

bool hasBinExtension(const std::string& path) {
  if (path.length() < 4) {
    return false;
  }
  const size_t n = path.length();
  return (path[n - 4] == '.') && (path[n - 3] == 'b' || path[n - 3] == 'B') &&
         (path[n - 2] == 'i' || path[n - 2] == 'I') && (path[n - 1] == 'n' || path[n - 1] == 'N');
}

std::string joinSdPath(const char* dirPath, const char* name) {
  std::string n = name ? name : "";
  if (n.empty()) {
    return "";
  }
  if (n[0] == '/') {
    return n;
  }
  std::string d = dirPath ? dirPath : "/";
  if (d.empty() || d == "/") {
    return "/" + n;
  }
  if (d.back() == '/') {
    return d + n;
  }
  return d + "/" + n;
}

void drawUpdateProgressCard(const GfxRenderer& renderer, const int pageWidth, const int bodyTop, const int screenHeight,
                            const float progress, const size_t processedBytes, const size_t totalBytes) {
  const int cardX = kUpdateCardMargin;
  const int cardW = pageWidth - (kUpdateCardMargin * 2);
  const int cardY = bodyTop + 22;
  const int cardH = std::min(144, screenHeight - cardY - 58);

  renderer.rectangle.fill(cardX, cardY, cardW, cardH, false, true);

  const int titleY = cardY + 18;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, cardX + 18, titleY, "Installing firmware", true,
                       EpdFontFamily::BOLD);

  const int statusY = titleY + 28;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, cardX + 18, statusY, "Please keep the device powered on.",
                       true, EpdFontFamily::REGULAR);

  const int barX = cardX + 18;
  const int barY = statusY + 28;
  const int barW = cardW - 36;
  renderer.rectangle.render(barX, barY, barW, kUpdateCardBarHeight, true);

  const int clamped = std::max(0, std::min(100, static_cast<int>(progress * 100.0f + 0.5f)));
  const int innerW = std::max(1, barW - 2);
  const int fillW = innerW * clamped / 100;
  renderer.rectangle.fill(barX + 1, barY + 1, innerW, kUpdateCardBarHeight - 2, false);
  if (fillW > 0) {
    renderer.rectangle.fill(barX + 1, barY + 1, fillW, kUpdateCardBarHeight - 2, true);
  }

  std::string metaLine;
  if (totalBytes > 0) {
    metaLine = std::to_string(processedBytes) + " / " + std::to_string(totalBytes);
  } else {
    metaLine = "Preparing package";
  }
  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, cardX + 18, barY + 24, metaLine.c_str(), true,
                       EpdFontFamily::REGULAR);
}
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

void OtaUpdateActivity::scanSdFirmwareFiles() {
  sdFirmwareFiles.clear();
  sdFirmwareSelectedIndex = 0;
  sdFirmwareScrollOffset = 0;

  auto scanDir = [this](const char* dirPath) {
    FsFile dir = SdMan.open(dirPath);
    if (!dir || !dir.isDirectory()) {
      if (dir) {
        dir.close();
      }
      return;
    }

    for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (!file.isDirectory()) {
        char name[160] = {};
        file.getName(name, sizeof(name));
        const std::string path = joinSdPath(dirPath, name);
        if (hasBinExtension(path)) {
          sdFirmwareFiles.push_back(path);
        }
      }
      file.close();
    }
    dir.close();
  };

  scanDir("/");
  scanDir("/firmware");

  std::sort(sdFirmwareFiles.begin(), sdFirmwareFiles.end());
  sdFirmwareFiles.erase(std::unique(sdFirmwareFiles.begin(), sdFirmwareFiles.end()), sdFirmwareFiles.end());
}

const std::string& OtaUpdateActivity::selectedSdFirmwarePath() const {
  if (sdFirmwareSelectedIndex < 0 || sdFirmwareSelectedIndex >= static_cast<int>(sdFirmwareFiles.size())) {
    return kEmptyPath;
  }
  return sdFirmwareFiles[static_cast<size_t>(sdFirmwareSelectedIndex)];
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
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, headerTextY, headerText, true, EpdFontFamily::BOLD);

  const int dividerY = headerY + headerHeight;
  renderer.line.render(0, dividerY, pageWidth, dividerY);

  const int bodyTop = dividerY;

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
  } else if (state == WAITING_SD_SELECTION) {
    const int totalFiles = static_cast<int>(sdFirmwareFiles.size());
    if (totalFiles == 0) {
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, bodyTop,
                           "No firmware .bin files found.", true, EpdFontFamily::BOLD);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, bodyTop + 32,
                           "Put .bin files in / or /firmware.", true, EpdFontFamily::REGULAR);
      const auto labels = mappedInput.mapLabels("« Back", "", "", "");
      renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else {
      const int listBottom = screenHeight - 44;
      const int visibleRows = std::max(1, (listBottom - bodyTop) / kFirmwareItemHeight);
      if (sdFirmwareSelectedIndex < sdFirmwareScrollOffset) {
        sdFirmwareScrollOffset = sdFirmwareSelectedIndex;
      } else if (sdFirmwareSelectedIndex >= sdFirmwareScrollOffset + visibleRows) {
        sdFirmwareScrollOffset = sdFirmwareSelectedIndex - visibleRows + 1;
      }
      const int maxScroll = std::max(0, totalFiles - visibleRows);
      sdFirmwareScrollOffset = std::max(0, std::min(sdFirmwareScrollOffset, maxScroll));

      const int endIndex = std::min(totalFiles, sdFirmwareScrollOffset + visibleRows);
      for (int i = sdFirmwareScrollOffset; i < endIndex; ++i) {
        const int itemY = bodyTop + (i - sdFirmwareScrollOffset) * kFirmwareItemHeight;
        const bool selected = sdFirmwareSelectedIndex == i;
        if (selected) {
          renderer.rectangle.fill(0, itemY, pageWidth, kFirmwareItemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
        }
        const std::string label = renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID,
                                                         sdFirmwareFiles[static_cast<size_t>(i)].c_str(), pageWidth - 40);
        const int textY =
            itemY + (kFirmwareItemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
        renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, label.c_str(), !selected,
                             EpdFontFamily::REGULAR);
        renderer.line.render(0, itemY + kFirmwareItemHeight - 1, pageWidth, itemY + kFirmwareItemHeight - 1);
      }
      const auto labels = mappedInput.mapLabels("« Back", "Select", "Up", "Down");
      renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  } else if (state == WAITING_SD_CONFIRMATION) {
    const std::string& firmwarePath = selectedSdFirmwarePath();
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, bodyTop,
                         ("File: " + firmwarePath).c_str(), true, EpdFontFamily::REGULAR);
    if (!firmwarePath.empty() && SdMan.exists(firmwarePath.c_str())) {
      FsFile file;
      size_t firmwareSize = 0;
      if (SdMan.openFileForRead("OTA", firmwarePath, file)) {
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
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, bodyTop + 36, "Firmware file is missing.",
                           true, EpdFontFamily::REGULAR);
      const auto labels = mappedInput.mapLabels("« Back", "", "", "");
      renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  } else if (state == UPDATE_IN_PROGRESS) {
    drawUpdateProgressCard(renderer, pageWidth, bodyTop, screenHeight, updaterProgress, updater.getProcessedSize(),
                           updater.getTotalSize());
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
        scanSdFirmwareFiles();
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = WAITING_SD_SELECTION;
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

  if (state == WAITING_SD_SELECTION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = SOURCE_SELECTION;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      return;
    }

    const int totalFiles = static_cast<int>(sdFirmwareFiles.size());
    if (totalFiles == 0) {
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      sdFirmwareSelectedIndex = (sdFirmwareSelectedIndex + 1) % totalFiles;
      updateRequired = true;
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      sdFirmwareSelectedIndex = (sdFirmwareSelectedIndex + totalFiles - 1) % totalFiles;
      updateRequired = true;
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = WAITING_SD_CONFIRMATION;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      return;
    }

    return;
  }

  if (state == WAITING_SD_CONFIRMATION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = WAITING_SD_SELECTION;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      return;
    }

    const std::string& firmwarePath = selectedSdFirmwarePath();
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && !firmwarePath.empty() &&
        SdMan.exists(firmwarePath.c_str())) {
      Serial.printf("[%lu] [OTA] Installing firmware from SD: %s\n", millis(), firmwarePath.c_str());
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = UPDATE_IN_PROGRESS;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      vTaskDelay(10 / portTICK_PERIOD_MS);
      const auto res = updater.installUpdateFromSd(firmwarePath.c_str());

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
