/**
 * @file HotspotActivity.cpp
 * @brief Definitions for HotspotActivity.
 */

#include "HotspotActivity.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <qrcode.h>

#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"
#include "system/WifiPower.h"

namespace {
constexpr const char* AP_SSID = "Xteink-X4";
constexpr const char* AP_HOSTNAME = "xteink";
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CONNECTIONS = 4;

DNSServer* dnsServer = nullptr;
constexpr uint16_t DNS_PORT = 53;

constexpr int CONTENT_MARGIN = 25;
constexpr int LINE_SPACING = 28;
constexpr int SMALL_SPACING = 25;
constexpr int SECTION_SPACING = 40;
constexpr int TAB_BAR_HEIGHT = 40;
constexpr int HEADER_TITLE_Y_OFFSET = 10;
constexpr int SUBTITLE_Y_OFFSET = 40;
constexpr int DIVIDER_PADDING = 10;
constexpr int BOTTOM_AREA_HEIGHT = 80;

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
void renderActivityHeader(const GfxRenderer& renderer, int startY, const char* title) {
  const int headerHeight = TAB_BAR_HEIGHT;

  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, CONTENT_MARGIN,
                       startY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2 +
                           HEADER_TITLE_Y_OFFSET,
                       title, true, EpdFontFamily::BOLD);

  const int dividerY = startY + headerHeight + 18;
  renderer.line.render(0, dividerY, renderer.getScreenWidth(), dividerY);
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
 * @param param Pointer to HotspotActivity instance
 */
void HotspotActivity::taskTrampoline(void* param) {
  auto* self = static_cast<HotspotActivity*>(param);
  self->displayTaskLoop();
}

/**
 * @brief Initializes the activity when entering, sets up access point and web server
 */
void HotspotActivity::onEnter() {
  Activity::onEnter();

  Serial.printf("[%lu] [HOTSPOT] Starting hotspot mode\n", millis());
  Serial.printf("[%lu] [HOTSPOT] [MEM] Free heap at onEnter: %d bytes\n", millis(), ESP.getFreeHeap());

  renderingMutex = xSemaphoreCreateMutex();
  updateRequired = true;
  state = HotspotState::STARTING;

  xTaskCreate(&HotspotActivity::taskTrampoline, "HotspotTask", 3072, this, 1, &displayTaskHandle);

  startAccessPoint();
}

/**
 * @brief Cleans up resources when exiting the activity
 */
void HotspotActivity::onExit() {
  Activity::onExit();

  stopWebServer();
  MDNS.end();

  if (dnsServer) {
    dnsServer->stop();
    delete dnsServer;
    dnsServer = nullptr;
  }

  WiFi.softAPdisconnect(true);
  WifiPower::off();

  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
}

/**
 * @brief Starts the WiFi access point for client connections
 */
void HotspotActivity::startAccessPoint() {
  Serial.printf("[%lu] [HOTSPOT] Starting Access Point...\n", millis());
  Serial.printf("[%lu] [HOTSPOT] [MEM] Free heap before AP start: %d bytes\n", millis(), ESP.getFreeHeap());

  WiFi.mode(WIFI_AP);
  delay(100);

  bool apStarted = WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, false, AP_MAX_CONNECTIONS);

  if (!apStarted) {
    Serial.printf("[%lu] [HOTSPOT] ERROR: Failed to start Access Point!\n", millis());
    state = HotspotState::ERROR;
    updateRequired = true;
    return;
  }

  delay(100);

  IPAddress apIP = WiFi.softAPIP();
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
  connectedIP = ipStr;
  connectedSSID = AP_SSID;

  Serial.printf("[%lu] [HOTSPOT] Access Point started!\n", millis());
  Serial.printf("[%lu] [HOTSPOT] SSID: %s\n", millis(), AP_SSID);
  Serial.printf("[%lu] [HOTSPOT] IP: %s\n", millis(), connectedIP.c_str());

  if (MDNS.begin(AP_HOSTNAME)) {
    Serial.printf("[%lu] [HOTSPOT] mDNS started: http://%s.local/\n", millis(), AP_HOSTNAME);
  }

  dnsServer = new DNSServer();
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer->start(DNS_PORT, "*", apIP);
  Serial.printf("[%lu] [HOTSPOT] DNS server started for captive portal\n", millis());

  Serial.printf("[%lu] [HOTSPOT] [MEM] Free heap after AP start: %d bytes\n", millis(), ESP.getFreeHeap());

  startWebServer();
}

/**
 * @brief Initializes and starts the web server for file transfers
 */
void HotspotActivity::startWebServer() {
  Serial.printf("[%lu] [HOTSPOT] Starting web server...\n", millis());

  webServer.reset(new LocalServer());
  webServer->begin();

  if (webServer->isRunning()) {
    Serial.printf("[%lu] [HOTSPOT] Web server started successfully\n", millis());
    state = HotspotState::RUNNING;
    updateRequired = true;
  } else {
    Serial.printf("[%lu] [HOTSPOT] ERROR: Failed to start web server!\n", millis());
    webServer.reset();
    state = HotspotState::ERROR;
    updateRequired = true;
  }
}

/**
 * @brief Stops the web server and cleans up resources
 */
void HotspotActivity::stopWebServer() {
  if (webServer && webServer->isRunning()) {
    webServer->stop();
  }
  webServer.reset();
}

/**
 * @brief Main loop processing DNS and web server requests
 */
void HotspotActivity::loop() {
  if (state == HotspotState::RUNNING && webServer && webServer->isRunning()) {
    if (dnsServer) {
      dnsServer->processNextRequest();
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
          if (onGoBack) onGoBack();
          return;
        }
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onGoBack) onGoBack();
  }
}

/**
 * @brief Background task loop that handles display updates
 */
void HotspotActivity::displayTaskLoop() {
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
 * @brief Draws a QR code at the specified position
 * @param x Top-left X coordinate
 * @param y Top-left Y coordinate
 * @param data String data to encode in QR code
 */
void HotspotActivity::drawQRCode(int x, int y, const std::string& data) const {
  QRCode qrcode;
  uint8_t qrcodeBytes[qrcode_getBufferSize(QR_VERSION)];

  qrcode_initText(&qrcode, qrcodeBytes, QR_VERSION, ECC_LOW, data.c_str());

  for (uint8_t cy = 0; cy < qrcode.size; cy++) {
    for (uint8_t cx = 0; cx < qrcode.size; cx++) {
      if (qrcode_getModule(&qrcode, cx, cy)) {
        renderer.rectangle.fill(x + QR_PIXEL_SIZE * cx, y + QR_PIXEL_SIZE * cy, QR_PIXEL_SIZE, QR_PIXEL_SIZE, true);
      }
    }
  }
}

/**
 * @brief Main rendering function that dispatches to appropriate state renderers
 */
void HotspotActivity::render() const {
  renderer.clearScreen();
  renderTabBar(renderer);

  int screenHeight = renderer.getScreenHeight();
  int startY = TAB_BAR_HEIGHT;

  if (state == HotspotState::RUNNING) {
    renderServerRunning();
  } else if (state == HotspotState::STARTING) {
    renderActivityHeader(renderer, startY, "Hotspot");

    int contentStart = startY + SUBTITLE_Y_OFFSET;
    int centerY = contentStart + (screenHeight - contentStart - BOTTOM_AREA_HEIGHT) / 2;

    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Please wait...");

    auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == HotspotState::ERROR) {
    renderActivityHeader(renderer, startY, "Hotspot");

    int contentStart = startY + SUBTITLE_Y_OFFSET;
    int centerY = contentStart + (screenHeight - contentStart - BOTTOM_AREA_HEIGHT) / 2;

    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY - 20, "Could not start hotspot");
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY + 10, "Press Back to try again");

    auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}

/**
 * @brief Renders the server running state UI with connection info and QR codes
 */
/**
 * @brief Renders the server running state UI with connection info and QR codes
 */
void HotspotActivity::renderServerRunning() const {
  int screenWidth = renderer.getScreenWidth();
  int startY = TAB_BAR_HEIGHT;

  renderActivityHeader(renderer, startY, "Hotspot");

  std::string hostnameUrl = std::string("http://") + AP_HOSTNAME + ".local/";
  std::string ipUrl = "http://" + connectedIP + "/";

  const int bodyTop = startY + SUBTITLE_Y_OFFSET + 50;
  const int textX = CONTENT_MARGIN + 2;
  const int qrX = screenWidth - QR_SIZE - CONTENT_MARGIN;
  const int wifiY = bodyTop + 8;
  const int webY = wifiY + QR_SIZE + 92;
  const int labelFont = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
  const int titleFont = ATKINSON_HYPERLEGIBLE_14_FONT_ID;
  const int bodyFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  renderer.text.render(labelFont, textX, wifiY, "STEP 1", true, EpdFontFamily::BOLD);
  renderer.text.render(titleFont, textX, wifiY + 24, "Join WiFi", true, EpdFontFamily::BOLD);
  renderer.text.render(bodyFont, textX, wifiY + 61, truncateString(connectedSSID, 20).c_str(), true);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, textX, wifiY + 86, connectedIP.c_str());
  drawQRCode(qrX, wifiY, "WIFI:S:" + connectedSSID + ";;");

  renderer.text.render(labelFont, textX, webY, "STEP 2", true, EpdFontFamily::BOLD);
  renderer.text.render(titleFont, textX, webY + 24, "Open Transfer", true, EpdFontFamily::BOLD);
  renderer.text.render(bodyFont, textX, webY + 61, hostnameUrl.c_str(), true);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, textX, webY + 86, ipUrl.c_str());
  drawQRCode(qrX, webY, hostnameUrl);

  auto labels = mappedInput.mapLabels("« Back", "", "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
