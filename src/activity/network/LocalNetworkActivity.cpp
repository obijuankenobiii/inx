/**
 * @file LocalNetworkActivity.cpp
 * @brief Definitions for LocalNetworkActivity.
 */

#include "LocalNetworkActivity.h"

#include <algorithm>

#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <qrcode.h>

#include "WifiSelectionActivity.h"
#include "activity/page/SubPage.h"
#include "images/Computer.h"
#include "images/Phone.h"
#include "images/Transfer.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"
#include "system/UiTheme.h"

namespace {
constexpr const char* AP_HOSTNAME = "inx";

constexpr int CONTENT_MARGIN = 25;
constexpr int LINE_SPACING = 28;
constexpr int SMALL_SPACING = 25;
constexpr int SECTION_SPACING = 40;
constexpr int BOTTOM_AREA_HEIGHT = 80;
constexpr int ICON_SIZE = 72;
constexpr int ICON_GAP = 28;
constexpr int QR_VERSION = 4;
constexpr int QR_PIXEL_SIZE = 5;
constexpr int QR_SIZE = QR_PIXEL_SIZE * 33;

/**
 * @brief Renders the header section for the activity
 * @param renderer Graphics renderer instance
 * @param startY Starting Y coordinate
 * @param title Header title text
 * @param subtitle Optional subtitle text
 */
int renderActivityHeader(const GfxRenderer& renderer, int startY, const char* title) {
  (void)startY;
  return SubPage::header(renderer, title);
}

/**
 * @brief Truncates a string to a maximum length, adding ellipsis if needed
 * @param str Input string to truncate
 * @param maxLength Maximum allowed length
 * @return Truncated string with ellipsis if original exceeds maxLength
 */
std::string truncateString(const std::string& str, int maxLength) {
  if (str.length() <= maxLength) return str;
  std::string result = str;
  result.replace(maxLength - 3, result.length() - (maxLength - 3), "...");
  return result;
}
}  // namespace

/**
 * @brief Static trampoline function for FreeRTOS task creation
 * @param param Pointer to LocalNetworkActivity instance
 */
void LocalNetworkActivity::taskTrampoline(void* param) {
  auto* self = static_cast<LocalNetworkActivity*>(param);
  self->displayTaskLoop();
}

/**
 * @brief Initializes the activity when entering, launches WiFi selection
 */
void LocalNetworkActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  Serial.printf("[%lu] [LOCALNET] Starting local network mode\n", millis());

  renderingMutex = xSemaphoreCreateMutex();
  updateRequired = true;
  state = LocalNetworkState::WIFI_SELECTION;

  xTaskCreate(&LocalNetworkActivity::taskTrampoline, "LocalNetTask", 4096, this, 1, &displayTaskHandle);

  WiFi.mode(WIFI_STA);
  enterNewActivity(
      new WifiSelectionActivity(renderer, mappedInput, [this](bool connected) { onWifiSelectionComplete(connected); }));
}

/**
 * @brief Cleans up resources when exiting the activity
 */
void LocalNetworkActivity::onExit() {
  ActivityWithSubactivity::onExit();

  stopWebServer();
  MDNS.end();

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
void LocalNetworkActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    Serial.printf("[%lu] [LOCALNET] WiFi selection cancelled\n", millis());
    if (onGoBack) onGoBack();
    return;
  }

  if (subActivity) {
    connectedIP = static_cast<WifiSelectionActivity*>(subActivity.get())->getConnectedIP();
  }
  connectedSSID = WiFi.SSID().c_str();

  Serial.printf("[%lu] [LOCALNET] Connected to %s, IP: %s\n", millis(), connectedSSID.c_str(), connectedIP.c_str());

  exitActivity();
  state = LocalNetworkState::SERVER_STARTING;
  updateRequired = true;

  if (MDNS.begin(AP_HOSTNAME)) {
    Serial.printf("[%lu] [LOCALNET] mDNS started: http://%s.local/\n", millis(), AP_HOSTNAME);
  }

  startWebServer();
}

/**
 * @brief Initializes and starts the web server for file transfers
 */
void LocalNetworkActivity::startWebServer() {
  Serial.printf("[%lu] [LOCALNET] Starting web server...\n", millis());

  webServer.reset(new LocalServer());
  webServer->begin();

  if (webServer->isRunning()) {
    state = LocalNetworkState::SERVER_RUNNING;
    Serial.printf("[%lu] [LOCALNET] Web server started successfully at http://%s/\n", millis(), connectedIP.c_str());

    updateRequired = true;
  } else {
    Serial.printf("[%lu] [LOCALNET] ERROR: Failed to start web server!\n", millis());
    webServer.reset();
    state = LocalNetworkState::ERROR;
    updateRequired = true;
  }
}

/**
 * @brief Stops the web server and cleans up resources
 */
void LocalNetworkActivity::stopWebServer() {
  if (webServer && webServer->isRunning()) {
    Serial.printf("[%lu] [LOCALNET] Stopping web server...\n", millis());
    webServer->stop();
  }
  webServer.reset();
}

/**
 * @brief Main loop processing WiFi monitoring and web server requests
 */
void LocalNetworkActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (state == LocalNetworkState::SERVER_RUNNING && webServer && webServer->isRunning()) {
    static unsigned long lastWifiCheck = 0;
    if (millis() - lastWifiCheck > 2000) {
      lastWifiCheck = millis();
      if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[%lu] [LOCALNET] WiFi disconnected!\n", millis());
        stopWebServer();
        state = LocalNetworkState::ERROR;
        updateRequired = true;
        return;
      }
    }

    esp_task_wdt_reset();

    constexpr int MAX_ITERATIONS = 500;
    for (int i = 0; i < MAX_ITERATIONS && webServer->isRunning(); i++) {
      webServer->handleClient();
      if ((i & 0x1F) == 0x1F) {
        esp_task_wdt_reset();
      }
      if ((i & 0x3F) == 0x3F) {
        yield();
        if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
          Serial.printf("[%lu] [LOCALNET] Back button pressed\n", millis());
          if (onGoBack) onGoBack();
          return;
        }
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    Serial.printf("[%lu] [LOCALNET] Back button pressed\n", millis());
    if (onGoBack) onGoBack();
  }
}

/**
 * @brief Background task loop that handles display updates
 */
void LocalNetworkActivity::displayTaskLoop() {
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

/**
 * @brief Main rendering function that dispatches to appropriate state renderers
 */
void LocalNetworkActivity::render() const {
  renderer.clearScreen();

  int screenHeight = renderer.getScreenHeight();
  int startY = 0;

  if (state == LocalNetworkState::SERVER_RUNNING) {
    renderServerRunning();
  } else if (state == LocalNetworkState::SERVER_STARTING) {
    const int contentStart = renderActivityHeader(renderer, startY, "Local Network");

    int centerY = contentStart + (screenHeight - contentStart - BOTTOM_AREA_HEIGHT) / 2;

    renderer.text.centered(MONTSERRAT_10_FONT_ID, centerY, "Please wait...");
  } else if (state == LocalNetworkState::ERROR) {
    const int contentStart = renderActivityHeader(renderer, startY, "Local Network");

    int centerY = contentStart + (screenHeight - contentStart - BOTTOM_AREA_HEIGHT) / 2;

    renderer.text.centered(MONTSERRAT_10_FONT_ID, centerY - 20, "Could not start server");
    renderer.text.centered(MONTSERRAT_10_FONT_ID, centerY + 10, "Press Back to try again");
  }

  auto labels = mappedInput.mapLabels("« Back", "", "", "");
  renderer.ui.buttonHints(MONTSERRAT_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

/**
 * @brief Renders the server running state UI with connection information
 */
void LocalNetworkActivity::renderServerRunning() const {
  const int contentStart = renderActivityHeader(renderer, 0, "Local Network");
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  constexpr int sideMargin = 20;
  const int labelFont = MONTSERRAT_8_FONT_ID;
  const int bodyFont = systemFontId();
  const int iconGroupWidth = ICON_SIZE * 3 + ICON_GAP * 2;
  const int iconX = (screenWidth - iconGroupWidth) / 2;
  const int iconY = contentStart + 58;

  renderer.bitmap.icon(Phone, iconX, iconY, ICON_SIZE, ICON_SIZE);
  renderer.bitmap.icon(Transfer, iconX + ICON_SIZE + ICON_GAP, iconY, ICON_SIZE, ICON_SIZE);
  renderer.bitmap.icon(Computer, iconX + (ICON_SIZE + ICON_GAP) * 2, iconY, ICON_SIZE, ICON_SIZE);

  const std::string ipUrl = "http://" + connectedIP + "/";
  const std::string hostnameUrl = std::string("http://") + AP_HOSTNAME + ".local/";
  const int availableTop = iconY + ICON_SIZE;
  const int availableBottom = screenHeight - BOTTOM_AREA_HEIGHT;
  const int bodyLineHeight = renderer.text.getLineHeight(bodyFont);
  const int labelLineHeight = renderer.text.getLineHeight(labelFont);
  const int detailsHeight = labelLineHeight + 12 + bodyLineHeight * 2 + 8;
  const int transferBlockHeight = QR_SIZE + 20 + detailsHeight;
  const int qrY = availableTop + std::max(0, (availableBottom - availableTop - transferBlockHeight) / 2);
  const int qrX = (screenWidth - QR_SIZE) / 2;
  const int detailsTop = qrY + QR_SIZE + 20;

  drawQRCode(qrX, qrY, hostnameUrl);
  renderer.text.centered(labelFont, detailsTop, "OPEN TRANSFER", true, EpdFontFamily::BOLD);
  renderer.text.centered(bodyFont, detailsTop + labelLineHeight + 12, hostnameUrl.c_str(), true,
                         EpdFontFamily::BOLD);
  renderer.text.centered(bodyFont, detailsTop + labelLineHeight + 12 + bodyLineHeight + 8, ipUrl.c_str());

  const int hintY = renderer.getScreenHeight() - 92;
  renderer.text.centered(MONTSERRAT_8_FONT_ID, hintY, "Keep this screen open while transferring");
}

void LocalNetworkActivity::drawQRCode(const int x, const int y, const std::string& data) const {
  QRCode qrcode;
  uint8_t qrcodeBytes[qrcode_getBufferSize(QR_VERSION)];
  qrcode_initText(&qrcode, qrcodeBytes, QR_VERSION, ECC_LOW, data.c_str());

  for (uint8_t cy = 0; cy < qrcode.size; ++cy) {
    for (uint8_t cx = 0; cx < qrcode.size; ++cx) {
      if (qrcode_getModule(&qrcode, cx, cy)) {
        renderer.rectangle.fill(x + QR_PIXEL_SIZE * cx, y + QR_PIXEL_SIZE * cy,
                                QR_PIXEL_SIZE, QR_PIXEL_SIZE, true);
      }
    }
  }
}
