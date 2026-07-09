/**
 * @file LocalNetworkActivity.cpp
 * @brief Definitions for LocalNetworkActivity.
 */

#include "LocalNetworkActivity.h"

#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "WifiSelectionActivity.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

namespace {
constexpr const char* AP_HOSTNAME = "xteink";

constexpr int CONTENT_MARGIN = 25;
constexpr int LINE_SPACING = 28;
constexpr int SMALL_SPACING = 25;
constexpr int SECTION_SPACING = 40;
constexpr int TAB_BAR_HEIGHT = 40;
constexpr int HEADER_TITLE_Y_OFFSET = 10;
constexpr int SUBTITLE_Y_OFFSET = 40;
constexpr int DIVIDER_PADDING = 10;
constexpr int BOTTOM_AREA_HEIGHT = 80;

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
  Serial.printf("[%lu] [LOCALNET] [MEM] Free heap: %d bytes\n", millis(), ESP.getFreeHeap());

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
    lastHandleClientTime = millis();
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
  renderTabBar(renderer);

  int screenHeight = renderer.getScreenHeight();
  int startY = TAB_BAR_HEIGHT;

  if (state == LocalNetworkState::SERVER_RUNNING) {
    renderServerRunning();
  } else if (state == LocalNetworkState::SERVER_STARTING) {
    renderActivityHeader(renderer, startY, "Local Network");

    int contentStart = startY + SUBTITLE_Y_OFFSET;
    int centerY = contentStart + (screenHeight - contentStart - BOTTOM_AREA_HEIGHT) / 2;

    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Please wait...");
  } else if (state == LocalNetworkState::ERROR) {
    renderActivityHeader(renderer, startY, "Local Network");

    int contentStart = startY + SUBTITLE_Y_OFFSET;
    int centerY = contentStart + (screenHeight - contentStart - BOTTOM_AREA_HEIGHT) / 2;

    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY - 20, "Could not start server");
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY + 10, "Press Back to try again");
  }

  auto labels = mappedInput.mapLabels("« Back", "", "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

/**
 * @brief Renders the server running state UI with connection information
 */
void LocalNetworkActivity::renderServerRunning() const {
  int startY = TAB_BAR_HEIGHT;

  renderActivityHeader(renderer, startY, "Local Network");

  std::string ipUrl = "http://" + connectedIP + "/";
  std::string hostnameUrl = std::string("http://") + AP_HOSTNAME + ".local/";

  const int bodyTop = startY + SUBTITLE_Y_OFFSET + 95;
  const int labelFont = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
  const int titleFont = ATKINSON_HYPERLEGIBLE_14_FONT_ID;
  const int bodyFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  renderer.text.centered(labelFont, bodyTop, "LOCAL TRANSFER", true, EpdFontFamily::BOLD);
  renderer.text.centered(titleFont, bodyTop + 34, "Ready on WiFi", true, EpdFontFamily::BOLD);
  renderer.text.centered(bodyFont, bodyTop + 74, truncateString(connectedSSID, 30).c_str());

  const int urlY = bodyTop + 136;
  renderer.text.centered(labelFont, urlY, "OPEN IN BROWSER", true, EpdFontFamily::BOLD);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, urlY + 32, ipUrl.c_str(), true, EpdFontFamily::BOLD);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, urlY + 64, hostnameUrl.c_str());

  const int hintY = renderer.getScreenHeight() - 92;
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, hintY, "Keep this screen open while transferring");
}
