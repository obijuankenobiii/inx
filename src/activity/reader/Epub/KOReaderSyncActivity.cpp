/**
 * @file KOReaderSyncActivity.cpp
 * @brief Definitions for KOReaderSyncActivity.
 */

#include "KOReaderSyncActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "system/MappedInputManager.h"
#include "activity/network/WifiSelectionActivity.h"
#include "system/Fonts.h"

namespace {

void wifiOff() {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

void syncTimeWithNTP() {
  
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }

  
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  
  int retry = 0;
  const int maxRetries = 50;  
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry < maxRetries) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    retry++;
  }

  if (retry < maxRetries) {
    Serial.printf("[%lu] [KOSync] NTP time synced\n", millis());
  } else {
    Serial.printf("[%lu] [KOSync] NTP sync timeout, using fallback\n", millis());
  }
}
}  

void KOReaderSyncActivity::taskTrampoline(void* param) {
  auto* self = static_cast<KOReaderSyncActivity*>(param);
  self->displayTaskLoop();
}

void KOReaderSyncActivity::onWifiSelectionComplete(const bool success) {
  exitActivity();

  if (!success) {
    Serial.printf("[%lu] [KOSync] WiFi connection failed, exiting\n", millis());
    onCancel();
    return;
  }

  Serial.printf("[%lu] [KOSync] WiFi connected, starting sync\n", millis());

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = SYNCING;
  statusMessage = "Syncing time...";
  xSemaphoreGive(renderingMutex);
  updateRequired = true;

  
  syncTimeWithNTP();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  statusMessage = "Calculating document hash...";
  xSemaphoreGive(renderingMutex);
  updateRequired = true;

  performSync();
}

void KOReaderSyncActivity::performSync() {
  
  if (KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME) {
    documentHash = KOReaderDocumentId::calculateFromFilename(epubPath);
  } else {
    documentHash = KOReaderDocumentId::calculate(epubPath);
  }
  if (documentHash.empty()) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = SYNC_FAILED;
    statusMessage = "Failed to calculate document hash";
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  Serial.printf("[%lu] [KOSync] Document hash: %s\n", millis(), documentHash.c_str());

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  statusMessage = "Fetching remote progress...";
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
  vTaskDelay(10 / portTICK_PERIOD_MS);

  
  const auto result = KOReaderSyncClient::getProgress(documentHash, remoteProgress);

  if (result == KOReaderSyncClient::NOT_FOUND) {
    
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = NO_REMOTE_PROGRESS;
    hasRemoteProgress = false;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  if (result != KOReaderSyncClient::OK) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = SYNC_FAILED;
    statusMessage = KOReaderSyncClient::errorString(result);
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  
  hasRemoteProgress = true;
  KOReaderPosition koPos = {remoteProgress.progress, remoteProgress.percentage};
  remotePosition = ProgressMapper::toCrossPoint(epub, koPos, currentSpineIndex, totalPagesInSpine);

  
  CrossPointPosition localPos{};
  localPos.spineIndex = currentSpineIndex;
  localPos.pageNumber = currentPage;
  localPos.totalPages = totalPagesInSpine;
  localProgress = ProgressMapper::toKOReader(epub, localPos);

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = SHOWING_RESULT;
  
  if (localProgress.percentage > remoteProgress.percentage) {
    selectedOption = 1;  
  } else {
    selectedOption = 0;  
  }
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
}

void KOReaderSyncActivity::performUpload() {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = UPLOADING;
  statusMessage = "Uploading progress...";
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
  vTaskDelay(10 / portTICK_PERIOD_MS);

  
  CrossPointPosition localPos{};
  localPos.spineIndex = currentSpineIndex;
  localPos.pageNumber = currentPage;
  localPos.totalPages = totalPagesInSpine;
  KOReaderPosition koPos = ProgressMapper::toKOReader(epub, localPos);

  KOReaderProgress progress;
  progress.document = documentHash;
  progress.progress = koPos.xpath;
  progress.percentage = koPos.percentage;

  const auto result = KOReaderSyncClient::updateProgress(progress);

  if (result != KOReaderSyncClient::OK) {
    wifiOff();
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = SYNC_FAILED;
    statusMessage = KOReaderSyncClient::errorString(result);
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  wifiOff();
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = UPLOAD_COMPLETE;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
}

void KOReaderSyncActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  xTaskCreate(&KOReaderSyncActivity::taskTrampoline, "KOSyncTask",
              4096,               
              this,               
              1,                  
              &displayTaskHandle  
  );

  
  if (!KOREADER_STORE.hasCredentials()) {
    state = NO_CREDENTIALS;
    updateRequired = true;
    return;
  }

  
  Serial.printf("[%lu] [KOSync] Turning on WiFi...\n", millis());
  WiFi.mode(WIFI_STA);

  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[%lu] [KOSync] Already connected to WiFi\n", millis());
    state = SYNCING;
    statusMessage = "Syncing time...";
    updateRequired = true;

    
    xTaskCreate(
        [](void* param) {
          auto* self = static_cast<KOReaderSyncActivity*>(param);
          
          syncTimeWithNTP();
          xSemaphoreTake(self->renderingMutex, portMAX_DELAY);
          self->statusMessage = "Calculating document hash...";
          xSemaphoreGive(self->renderingMutex);
          self->updateRequired = true;
          self->performSync();
          vTaskDelete(nullptr);
        },
        "SyncTask", 4096, this, 1, nullptr);
    return;
  }

  
  Serial.printf("[%lu] [KOSync] Launching WifiSelectionActivity...\n", millis());
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void KOReaderSyncActivity::onExit() {
  ActivityWithSubactivity::onExit();

  wifiOff();

  
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void KOReaderSyncActivity::displayTaskLoop() {
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

void KOReaderSyncActivity::render() {
  if (subActivity) {
    return;
  }

  const auto pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 15, "KOReader Sync", true, EpdFontFamily::BOLD);

  if (state == NO_CREDENTIALS) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 280, "No credentials configured", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 320, "Set up KOReader account in Settings");

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SYNCING || state == UPLOADING) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 300, statusMessage.c_str(), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == SHOWING_RESULT) {
    
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 120, "Progress found!", true, EpdFontFamily::BOLD);

    
    const int remoteTocIndex = epub->getTocIndexForSpineIndex(remotePosition.spineIndex);
    const int localTocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    const std::string remoteChapter = (remoteTocIndex >= 0)
                                          ? epub->getTocItem(remoteTocIndex).title
                                          : ("Section " + std::to_string(remotePosition.spineIndex + 1));
    const std::string localChapter = (localTocIndex >= 0) ? epub->getTocItem(localTocIndex).title
                                                          : ("Section " + std::to_string(currentSpineIndex + 1));

    
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, 160, "Remote:", true);
    char remoteChapterStr[128];
    snprintf(remoteChapterStr, sizeof(remoteChapterStr), "  %s", remoteChapter.c_str());
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, 185, remoteChapterStr);
    char remotePageStr[64];
    snprintf(remotePageStr, sizeof(remotePageStr), "  Page %d, %.2f%% overall", remotePosition.pageNumber + 1,
             remoteProgress.percentage * 100);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, 210, remotePageStr);

    if (!remoteProgress.device.empty()) {
      char deviceStr[64];
      snprintf(deviceStr, sizeof(deviceStr), "  From: %s", remoteProgress.device.c_str());
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, 235, deviceStr);
    }

    
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, 270, "Local:", true);
    char localChapterStr[128];
    snprintf(localChapterStr, sizeof(localChapterStr), "  %s", localChapter.c_str());
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, 295, localChapterStr);
    char localPageStr[64];
    snprintf(localPageStr, sizeof(localPageStr), "  Page %d/%d, %.2f%% overall", currentPage + 1, totalPagesInSpine,
             localProgress.percentage * 100);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, 320, localPageStr);

    
    const int optionY = 350;
    const int optionHeight = 30;

    if (selectedOption == 0) {
      renderer.fillRect(0, optionY - 2, pageWidth - 1, optionHeight, GfxRenderer::FillTone::Ink);
    }
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, optionY, "Apply remote progress", selectedOption != 0);

    if (selectedOption == 1) {
      renderer.fillRect(0, optionY + optionHeight - 2, pageWidth - 1, optionHeight, GfxRenderer::FillTone::Ink);
    }
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, optionY + optionHeight, "Upload local progress", selectedOption != 1);

    const auto labels = mappedInput.mapLabels("Back", "Select", "Dir Up", "Dir Down");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 280, "No remote progress found", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 320, "Upload current position?");

    const auto labels = mappedInput.mapLabels("Cancel", "Upload", "", "");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == UPLOAD_COMPLETE) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 300, "Progress uploaded!", true, EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SYNC_FAILED) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 280, "Sync failed", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 320, statusMessage.c_str());

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void KOReaderSyncActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (state == NO_CREDENTIALS || state == SYNC_FAILED || state == UPLOAD_COMPLETE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onCancel();
    }
    return;
  }

  if (state == SHOWING_RESULT) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      selectedOption = (selectedOption + 1) % 2;
      updateRequired = true;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedOption == 0) {
        onSyncComplete(remotePosition.spineIndex, remotePosition.pageNumber);
      } else {
        performUpload();
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onCancel();
    }
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      
      if (documentHash.empty()) {
        if (KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME) {
          documentHash = KOReaderDocumentId::calculateFromFilename(epubPath);
        } else {
          documentHash = KOReaderDocumentId::calculate(epubPath);
        }
      }
      performUpload();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onCancel();
    }
    return;
  }
}
