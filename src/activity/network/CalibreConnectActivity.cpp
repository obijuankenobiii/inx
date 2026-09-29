/**
 * @file CalibreConnectActivity.cpp
 * @brief Definitions for CalibreConnectActivity.
 */

#include "CalibreConnectActivity.h"

#include <algorithm>

#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <WiFi.h>

#include "activity/page/SubPage.h"
#include "WifiSelectionActivity.h"
#include "images/Computer.h"
#include "images/Check.h"
#include "images/Phone.h"
#include "images/Transfer.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

namespace {

/**
 * @brief mDNS hostname for device discovery on local network
 */
constexpr const char* HOSTNAME = "inx";
}

/**
 * @brief Destructor - stops web server and cleans up resources
 */
CalibreConnectActivity::~CalibreConnectActivity() { stopWebServer(); }

/**
 * @brief Static trampoline function for FreeRTOS task creation
 * @param param Pointer to CalibreConnectActivity instance
 */
void CalibreConnectActivity::taskTrampoline(void* param) {
  auto* self = static_cast<CalibreConnectActivity*>(param);
  self->displayTaskLoop();
}

/**
 * @brief Initializes the activity when entering, sets up WiFi or web server
 */
void CalibreConnectActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  updateRequired = true;
  state = CalibreConnectState::WIFI_SELECTION;
  connectedIP.clear();
  connectedSSID.clear();
  lastProgressReceived = 0;
  lastProgressTotal = 0;
  currentUploadName.clear();
  lastCompleteName.clear();
  lastCompleteAt = 0;
  lastProcessedCompleteAt = 0;
  receivedFiles.clear();

  const bool alreadyConnected = WiFi.status() == WL_CONNECTED;
  if (!alreadyConnected) {
    enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                               [this](const bool connected) { onWifiSelectionComplete(connected); }));
  } else {
    connectedIP = WiFi.localIP().toString().c_str();
    connectedSSID = WiFi.SSID().c_str();
  }

  xTaskCreate(&CalibreConnectActivity::taskTrampoline, "CalibreConnectTask", 8192, this, 1, &displayTaskHandle);

  if (alreadyConnected) {
    startWebServer();
  }
}

/**
 * @brief Cleans up resources when exiting the activity
 */
void CalibreConnectActivity::onExit() {
  ActivityWithSubactivity::onExit();

  stopWebServer();
  MDNS.end();

  delay(50);
  WiFi.disconnect(false);
  delay(30);
  WiFi.mode(WIFI_OFF);

  if (renderingMutex) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    if (displayTaskHandle) {
      vTaskDelete(displayTaskHandle);
      displayTaskHandle = nullptr;
    }
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
}

/**
 * @brief Callback handler for WiFi selection completion
 * @param connected True if WiFi connection successful
 */
void CalibreConnectActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected || WiFi.status() != WL_CONNECTED) {
    exitActivity();
    onComplete();
    return;
  }

  if (subActivity) {
    connectedIP = static_cast<WifiSelectionActivity*>(subActivity.get())->getConnectedIP();
  } else {
    connectedIP = WiFi.localIP().toString().c_str();
  }
  connectedSSID = WiFi.SSID().c_str();
  exitActivity();
  startWebServer();
}

/**
 * @brief Initializes and starts the web server for Calibre wireless connection
 */
void CalibreConnectActivity::startWebServer() {
  state = CalibreConnectState::SERVER_STARTING;
  updateRequired = true;

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[CAL] mDNS started: http://%s.local/\n", HOSTNAME);
  }

  webServer.reset(new LocalServer());
  webServer->begin();

  if (webServer->isRunning()) {
    lastProcessedCompleteAt = webServer->getWsUploadStatus().lastCompleteAt;
    state = CalibreConnectState::SERVER_RUNNING;
    updateRequired = true;
    Serial.printf("[CAL] Compatible web server started on HTTP 80, WebSocket 81\n");
  } else {
    webServer.reset();
    state = CalibreConnectState::ERROR;
    updateRequired = true;
    Serial.printf("[CAL] Failed to start compatible web server\n");
  }
}

/**
 * @brief Stops the web server and cleans up all related resources
 */
void CalibreConnectActivity::stopWebServer() {
  if (webServer) {
    webServer->stop();
    webServer.reset();
  }
}

/**
 * @brief Main loop processing WiFi connections and HTTP requests
 */
void CalibreConnectActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, onComplete)) return;

  if (webServer && webServer->isRunning() && state == CalibreConnectState::SERVER_RUNNING) {
    webServer->handleClient();

    const LocalServer::WsUploadStatus status = webServer->getWsUploadStatus();
    bool changed = false;
    if (status.inProgress) {
      if (status.received != lastProgressReceived || status.total != lastProgressTotal ||
          status.filename != currentUploadName) {
        lastProgressReceived = status.received;
        lastProgressTotal = status.total;
        currentUploadName = status.filename;
        changed = true;
      }
    } else if (lastProgressReceived != 0 || lastProgressTotal != 0 || !currentUploadName.empty()) {
      lastProgressReceived = 0;
      lastProgressTotal = 0;
      currentUploadName.clear();
      changed = true;
    }

    if (status.lastCompleteAt != 0 && status.lastCompleteAt != lastProcessedCompleteAt) {
      lastCompleteAt = status.lastCompleteAt;
      lastCompleteName = status.lastCompleteName;
      lastProcessedCompleteAt = status.lastCompleteAt;
      if (!lastCompleteName.empty()) receivedFiles.push_back(lastCompleteName);
      changed = true;
    }
    if (lastCompleteAt > 0 && millis() - lastCompleteAt >= 6000) {
      lastCompleteAt = 0;
      lastCompleteName.clear();
      changed = true;
    }
    if (changed) updateRequired = true;
  }

}

/**
 * @brief Background task loop that handles display updates
 */
void CalibreConnectActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      if (state == CalibreConnectState::WIFI_SELECTION) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }
      if (renderingMutex && xSemaphoreTake(renderingMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        render();
        xSemaphoreGive(renderingMutex);
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Main rendering function that dispatches to appropriate state renderers
 */
void CalibreConnectActivity::render() const {
  renderer.clearScreen();

  int screenWidth = renderer.getScreenWidth();
  int screenHeight = renderer.getScreenHeight();
  int startY = 0;

  if (state == CalibreConnectState::SERVER_RUNNING) {
    renderServerRunning(screenWidth, screenHeight, startY);
  } else if (state == CalibreConnectState::SERVER_STARTING) {
    const int contentStart = SubPage::header(renderer, "Calibre File Transfer");

    int centerY = contentStart + (screenHeight - contentStart) / 2;

    renderer.text.centered(systemFontId(), centerY, "Please wait...");

    auto labels = mappedInput.mapLabels("« Back", "", "", "");
  } else if (state == CalibreConnectState::ERROR) {
    const int contentStart = SubPage::header(renderer, "Calibre File Transfer");

    int centerY = contentStart + (screenHeight - contentStart) / 2;

    renderer.text.centered(systemFontId(), centerY - 20, "Could not start server");
    renderer.text.centered(systemFontId(), centerY + 10, "Press Back to return");

    auto labels = mappedInput.mapLabels("« Back", "", "", "");
  }

  renderer.displayBuffer();
}

/**
 * @brief Renders the server running state UI with network info and upload progress
 * @param screenWidth Width of the display
 * @param screenHeight Height of the display
 * @param startY Starting Y coordinate for content
 */
void CalibreConnectActivity::renderServerRunning(int screenWidth, int screenHeight, int startY) const {
  (void)startY;

  const int contentStart = SubPage::header(renderer, "Calibre File Transfer");

  constexpr int iconSize = 72;
  constexpr int iconGap = 28;
  constexpr int rowHeight = UiLayout::LIST_ITEM_HEIGHT;
  constexpr int sideMargin = 20;
  constexpr int actionIconSize = 40;
  const int labelFont = MONTSERRAT_8_FONT_ID;
  const int bodyFont = systemFontId();

  const int iconGroupWidth = iconSize * 3 + iconGap * 2;
  const int iconX = (screenWidth - iconGroupWidth) / 2;
  const int iconY = contentStart + 58;
  renderer.bitmap.icon(Phone, iconX, iconY, iconSize, iconSize);
  renderer.bitmap.icon(Transfer, iconX + iconSize + iconGap, iconY, iconSize, iconSize);
  renderer.bitmap.icon(Computer, iconX + (iconSize + iconGap) * 2, iconY, iconSize, iconSize);

  const int detailsY = iconY + iconSize + 12;
  renderer.text.centered(labelFont, detailsY, "CALIBRE", true, EpdFontFamily::BOLD);

  const bool uploadActive = lastProgressTotal > 0 && lastProgressReceived <= lastProgressTotal;
  if (uploadActive || !receivedFiles.empty()) {
    const int listTop = std::max(iconY + iconSize + 40, detailsY + 26 + 40);
    const int listBottom = screenHeight - 76;
    const int visibleRows = std::max(0, (listBottom - listTop) / rowHeight);
    const int totalFiles = static_cast<int>(receivedFiles.size()) + (uploadActive ? 1 : 0);
    const int visibleFiles = std::min(visibleRows, totalFiles);

    for (int row = 0; row < visibleFiles; ++row) {
      const bool isCurrentUpload = uploadActive && row == 0;
      const int rowY = listTop + row * rowHeight;
      const int textY = rowY + (rowHeight - renderer.text.getLineHeight(bodyFont)) / 2;
      const int iconXRight = screenWidth - sideMargin - actionIconSize;
      const int percent = uploadActive
                              ? std::min(100, static_cast<int>((static_cast<uint64_t>(lastProgressReceived) * 100) /
                                                               lastProgressTotal))
                              : 0;
      const std::string progressText = std::to_string(percent) + "%";
      const int rightContentWidth = isCurrentUpload ? renderer.text.getWidth(bodyFont, progressText.c_str())
                                                     : actionIconSize;
      const int maxNameWidth = screenWidth - (sideMargin * 2) - rightContentWidth - 20;
      const char* fileName = currentUploadName.c_str();
      size_t fileIndex = 0;
      if (!isCurrentUpload) {
        const int completedRow = uploadActive ? row - 1 : row;
        fileIndex = receivedFiles.size() - 1 - static_cast<size_t>(completedRow);
        fileName = receivedFiles[fileIndex].c_str();
      }
      const std::string displayName = renderer.text.truncate(bodyFont, fileName, maxNameWidth);
      renderer.text.render(bodyFont, sideMargin, textY, displayName.c_str());
      if (!isCurrentUpload) {
        renderer.bitmap.icon(Check, iconXRight, rowY + (rowHeight - actionIconSize) / 2, actionIconSize,
                             actionIconSize);
      } else {
        renderer.text.render(bodyFont, screenWidth - sideMargin - renderer.text.getWidth(bodyFont, progressText.c_str()),
                             textY, progressText.c_str());
      }
      if (row + 1 < visibleFiles) {
        renderer.line.render(0, rowY + rowHeight - 1, screenWidth, rowY + rowHeight - 1, true,
                             LineRender::Style::Dotted);
      }
    }
  } else {
    const int instructionCenterY = detailsY + (screenHeight - detailsY - 76) / 2;
    renderer.text.centered(bodyFont, instructionCenterY - 24, "Download and install");
    renderer.text.centered(bodyFont, instructionCenterY, "CrossPoint Calibre");
    renderer.text.centered(bodyFont, instructionCenterY + 24, "plugin");
  }

  renderer.text.centered(MONTSERRAT_8_FONT_ID, screenHeight - 30, "Choose Send to device in Calibre");

  auto labels = mappedInput.mapLabels("« Back", "", "", "");
}
