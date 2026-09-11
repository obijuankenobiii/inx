/**
 * @file KOReaderSyncActivity.cpp
 * @brief Definitions for KOReaderSyncActivity.
 */

#include "KOReaderSyncActivity.h"

#include <algorithm>

#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "activity/network/WifiSelectionActivity.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/SubPage.h"
#include "images/Computer.h"
#include "images/Download.h"
#include "images/Phone.h"
#include "images/Transfer.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

ButtonBounds startButtonBounds(const GfxRenderer& renderer, const int font) {
  const int width = Button::width(renderer, "Start Sync", font);
  return {(renderer.getScreenWidth() - width) / 2, renderer.getScreenHeight() - Button::height - 30, width,
          Button::height};
}

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

struct SyncActionButtons {
  ButtonBounds upload;
  ButtonBounds download;
};

constexpr int kSyncActionIconSize = 40;
constexpr int kSyncActionRightMargin = 20;
constexpr int kSyncActionBottomMargin = 20;
constexpr int kSyncIconSize = 72;
constexpr int kSyncIconGap = 28;
constexpr int kResultLabelHeight = 26;
constexpr int kResultLabelTopMargin = 20;
constexpr int kResultLabelBottomMargin = 20;
constexpr int kResultDetailSpacing = 30;

SyncActionButtons syncActionButtons(const GfxRenderer& renderer, const int remoteY, const int localY) {
  const int width = renderer.getScreenWidth() - kSyncActionRightMargin * 2;
  const int bottom = renderer.getScreenHeight() - kSyncActionBottomMargin;
  return {{kSyncActionRightMargin, localY - 12, width, std::max(0, bottom - (localY - 12))},
          {kSyncActionRightMargin, remoteY, width, std::max(0, localY - 12 - remoteY)}};
}

ButtonBounds singleUploadButton(const GfxRenderer& renderer) {
  return {renderer.getScreenWidth() - kSyncActionRightMargin - kSyncActionIconSize,
          renderer.getScreenHeight() - kSyncActionBottomMargin - kSyncActionIconSize, kSyncActionIconSize,
          kSyncActionIconSize};
}

void renderSyncActionIcon(const GfxRenderer& renderer, const ButtonBounds& bounds,
                          const BitmapRender::Orientation iconOrientation, const int yOffset = 0,
                          const bool invert = false) {
  const int iconX = bounds.x + bounds.width - kSyncActionRightMargin - kSyncActionIconSize;
  const int iconY = bounds.y + std::max(0, (bounds.height - kSyncActionIconSize) / 2) + yOffset;
  renderer.bitmap.icon(Download, iconX, iconY, kSyncActionIconSize, kSyncActionIconSize, iconOrientation, invert);
}

int renderSyncChrome(const GfxRenderer& renderer) {
  const int contentTop = SubPage::header(renderer, "KOReader Sync");
  const int iconGroupWidth = kSyncIconSize * 3 + kSyncIconGap * 2;
  const int iconX = (renderer.getScreenWidth() - iconGroupWidth) / 2;
  const int iconY = contentTop + 58;

  renderer.bitmap.icon(Phone, iconX, iconY, kSyncIconSize, kSyncIconSize);
  renderer.bitmap.icon(Transfer, iconX + kSyncIconSize + kSyncIconGap, iconY, kSyncIconSize, kSyncIconSize);
  renderer.bitmap.icon(Computer, iconX + (kSyncIconSize + kSyncIconGap) * 2, iconY, kSyncIconSize, kSyncIconSize);
  renderer.text.centered(MONTSERRAT_8_FONT_ID, iconY + kSyncIconSize + 12, "KOREADER", true,
                         EpdFontFamily::BOLD);
  return iconY + kSyncIconSize + 12;
}

void renderCenteredListRow(const GfxRenderer& renderer, const int y, const int height, const int font,
                           const char* text, const bool bold = false) {
  const int textY = y + (height - renderer.text.getLineHeight(font)) / 2;
  renderer.text.centered(font, textY, text, true, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}

constexpr int kResultLeft = 20;
void renderLeftListRow(const GfxRenderer& renderer, const int y, const int height, const int font,
                       const char* text, const bool bold = false, const bool ink = true) {
  const int textY = y + (height - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, kResultLeft, textY, text, ink,
                       bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}
}  // namespace

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

  KOReaderProgress progress;
  progress.document = documentHash;
  progress.progress = localProgress.xpath;
  progress.percentage = localProgress.percentage;

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

  xTaskCreate(&KOReaderSyncActivity::taskTrampoline, "KOSyncTask", 4096, this, 1, &displayTaskHandle);

  if (!KOREADER_STORE.hasCredentials()) {
    state = NO_CREDENTIALS;
    updateRequired = true;
    return;
  }

  state = IDLE;
  updateRequired = true;
}

void KOReaderSyncActivity::startSync() {
  if (!KOREADER_STORE.hasCredentials() || state != IDLE) {
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

  renderer.clearScreen();
  const int chromeBottom = renderSyncChrome(renderer);
  const int font = systemFontId();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentCenterY = chromeBottom + (pageHeight - chromeBottom) / 2;

  if (state == NO_CREDENTIALS) {
    renderer.text.centered(font, contentCenterY - 20, "No credentials configured", true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(font, contentCenterY + 20, "Set up KOReader account in Settings");

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == IDLE) {
    renderer.text.centered(font, contentCenterY - 30, "Ready to sync this book", true,
                           EpdFontFamily::BOLD);
    Button::render(renderer, startButtonBounds(renderer, font), "Start Sync", true, font);
    renderer.displayBuffer();
    return;
  }

  if (state == SYNCING || state == UPLOADING) {
    const int rowY = contentCenterY - 33;
    renderCenteredListRow(renderer, rowY, 66, font, statusMessage.c_str(), true);
    renderer.displayBuffer();
    return;
  }

  if (state == SHOWING_RESULT) {
    const int remoteTocIndex = epub->getTocIndexForSpineIndex(remotePosition.spineIndex);
    const std::string remoteChapter = (remoteTocIndex >= 0)
                                          ? epub->getTocItem(remoteTocIndex).title
                                          : ("Section " + std::to_string(remotePosition.spineIndex + 1));
    const std::string localChapter =
        !localChapterName.empty() ? localChapterName : ("Section " + std::to_string(currentSpineIndex + 1));

    const bool hasDevice = !remoteProgress.device.empty();
    (void)hasDevice;
    const int sectionHeight = kResultLabelTopMargin + kResultLabelHeight + kResultLabelBottomMargin +
                              kResultDetailSpacing * 3 + renderer.text.getLineHeight(font) +
                              kResultLabelBottomMargin;
    const int sectionGap = 12;
    const int sectionsBottom = pageHeight - kSyncActionBottomMargin;
    const int available = std::max(0, sectionsBottom - chromeBottom);
    const int totalSectionsHeight = sectionHeight * 2 + sectionGap;
    const int downloadY = chromeBottom + std::max(0, (available - totalSectionsHeight) / 2);
    const bool downloadSelected = selectedOption == 0;
    const bool uploadSelected = !downloadSelected;
    const bool downloadInk = !downloadSelected;
    const bool uploadInk = !uploadSelected;

    const int localY = downloadY + sectionHeight + sectionGap;
    const int dividerY = localY - sectionGap;
    const int downloadLabelY = downloadY + kResultLabelTopMargin;
    const int uploadLabelY = localY + kResultLabelTopMargin;
    const SyncActionButtons actions = {
        {kSyncActionRightMargin, downloadY, pageWidth - kSyncActionRightMargin * 2, sectionHeight},
        {kSyncActionRightMargin, localY, pageWidth - kSyncActionRightMargin * 2, sectionHeight}};
    if (downloadSelected) {
      renderer.rectangle.fill(0, downloadY, pageWidth, sectionHeight,
                              static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    renderLeftListRow(renderer, downloadLabelY, kResultLabelHeight, font, "Download", true, downloadInk);
    renderer.text.render(font, kResultLeft, downloadLabelY + kResultLabelHeight + kResultLabelBottomMargin,
                         remoteChapter.c_str(), downloadInk);
    char remotePageStr[64];
    snprintf(remotePageStr, sizeof(remotePageStr), "Page %d", remotePosition.pageNumber + 1);
    const int remotePageY = downloadLabelY + kResultLabelHeight + kResultLabelBottomMargin + kResultDetailSpacing;
    renderer.text.render(font, kResultLeft, remotePageY, remotePageStr, downloadInk);
    char remotePercentStr[64];
    snprintf(remotePercentStr, sizeof(remotePercentStr), "%.2f%% overall", remoteProgress.percentage * 100);
    renderer.text.render(font, kResultLeft, remotePageY + kResultDetailSpacing, remotePercentStr, downloadInk);

    if (hasDevice) {
      char deviceStr[64];
      snprintf(deviceStr, sizeof(deviceStr), "From: %s", remoteProgress.device.c_str());
      renderer.text.render(font, kResultLeft, remotePageY + kResultDetailSpacing * 2, deviceStr, downloadInk);
    }

    renderer.line.render(20, dividerY, pageWidth - 20, dividerY, true, LineRender::Style::Dotted);
    renderer.line.render(20, dividerY + 1, pageWidth - 20, dividerY + 1, true, LineRender::Style::Dotted);
    if (uploadSelected) {
      renderer.rectangle.fill(0, localY, pageWidth, sectionHeight,
                              static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    renderLeftListRow(renderer, uploadLabelY, kResultLabelHeight, font, "Upload", true, uploadInk);
    renderer.text.render(font, kResultLeft, uploadLabelY + kResultLabelHeight + kResultLabelBottomMargin,
                         localChapter.c_str(), uploadInk);
    char localPageStr[64];
    snprintf(localPageStr, sizeof(localPageStr), "Page %d/%d", currentPage + 1, totalPagesInSpine);
    const int localPageY = uploadLabelY + kResultLabelHeight + kResultLabelBottomMargin + kResultDetailSpacing;
    renderer.text.render(font, kResultLeft, localPageY, localPageStr, uploadInk);
    char localPercentStr[64];
    snprintf(localPercentStr, sizeof(localPercentStr), "%.2f%% overall", localProgress.percentage * 100);
    renderer.text.render(font, kResultLeft, localPageY + kResultDetailSpacing, localPercentStr, uploadInk);
    renderer.text.render(font, kResultLeft, localPageY + kResultDetailSpacing * 2,
                         "From: Current Progress", uploadInk);

    renderSyncActionIcon(renderer, actions.upload, BitmapRender::Orientation::Rotate180, 0, uploadSelected);
    renderSyncActionIcon(renderer, actions.download, BitmapRender::Orientation::None, 0, downloadSelected);

    const auto labels = mappedInput.mapLabels("Back", "Select", "Dir Up", "Dir Down");
    renderer.displayBuffer();
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    renderer.text.centered(font, contentCenterY - 20, "No remote progress found", true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(font, contentCenterY + 20, "Upload current position?");
    renderSyncActionIcon(renderer, singleUploadButton(renderer), BitmapRender::Orientation::Rotate180);

    const auto labels = mappedInput.mapLabels("Cancel", "Upload", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == UPLOAD_COMPLETE) {
    renderer.text.centered(font, contentCenterY, "Progress uploaded!", true, EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  if (state == SYNC_FAILED) {
    renderer.text.centered(font, contentCenterY - 20, "Sync failed", true, EpdFontFamily::BOLD);
    renderer.text.centered(font, contentCenterY + 20, statusMessage.c_str());

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    renderer.displayBuffer();
    return;
  }
}

void KOReaderSyncActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, onCancel)) {
    return;
  }

  if (state == IDLE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      startSync();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onCancel();
    }
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
