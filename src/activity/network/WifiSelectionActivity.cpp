#include "WifiSelectionActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>

#include <algorithm>
#include <map>

#include "activity/util/KeyboardEntryActivity.h"
#include "state/NetworkCredential.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr int LIST_ITEM_HEIGHT = 60;
}  // namespace

/**
 * @brief Static trampoline function for the display task
 * @param param Pointer to the WifiSelectionActivity instance
 */
void WifiSelectionActivity::taskTrampoline(void* param) {
  auto* self = static_cast<WifiSelectionActivity*>(param);
  self->displayTaskLoop();
}

/**
 * @brief Called when entering the activity
 */
void WifiSelectionActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  WIFI_STORE.loadFromFile();
  xSemaphoreGive(renderingMutex);

  selectedNetworkIndex = 0;
  networks.clear();
  state = WifiSelectionState::SCANNING;
  selectedSSID.clear();
  connectedIP.clear();
  connectionError.clear();
  enteredPassword.clear();
  usedSavedPassword = false;
  savePromptSelection = 0;
  forgetPromptSelection = 0;

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[32];
  snprintf(macStr, sizeof(macStr), "MAC address: %02x-%02x-%02x-%02x-%02x-%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
  cachedMacAddress = std::string(macStr);

  updateRequired = true;

  xTaskCreate(&WifiSelectionActivity::taskTrampoline, "WifiSelectionTask", 4096, this, 1, &displayTaskHandle);

  startWifiScan();
}

/**
 * @brief Called when exiting the activity
 */
void WifiSelectionActivity::onExit() {
  Activity::onExit();

  WiFi.scanDelete();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);

  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  exitActivity();
}

/**
 * @brief Starts an asynchronous WiFi network scan
 */
void WifiSelectionActivity::startWifiScan() {
  state = WifiSelectionState::SCANNING;
  networks.clear();
  updateRequired = true;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  WiFi.scanNetworks(true);
}

/**
 * @brief Processes the results of a WiFi scan
 */
void WifiSelectionActivity::processWifiScanResults() {
  const int16_t scanResult = WiFi.scanComplete();

  if (scanResult == WIFI_SCAN_RUNNING) {
    return;
  }

  if (scanResult == WIFI_SCAN_FAILED) {
    state = WifiSelectionState::NETWORK_LIST;
    updateRequired = true;
    return;
  }

  std::map<std::string, WifiNetworkInfo> uniqueNetworks;

  for (int i = 0; i < scanResult; i++) {
    std::string ssid = WiFi.SSID(i).c_str();
    const int32_t rssi = WiFi.RSSI(i);

    if (ssid.empty()) {
      continue;
    }

    auto it = uniqueNetworks.find(ssid);
    if (it == uniqueNetworks.end() || rssi > it->second.rssi) {
      WifiNetworkInfo network;
      network.ssid = ssid;
      network.rssi = rssi;
      network.isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      network.hasSavedPassword = WIFI_STORE.hasSavedCredential(network.ssid);
      uniqueNetworks[ssid] = network;
    }
  }

  networks.clear();
  networks.reserve(uniqueNetworks.size());
  std::transform(uniqueNetworks.begin(), uniqueNetworks.end(), std::back_inserter(networks),
                 [](const std::pair<const std::string, WifiNetworkInfo>& pair) { return pair.second; });

  std::sort(networks.begin(), networks.end(),
            [](const WifiNetworkInfo& a, const WifiNetworkInfo& b) { return a.rssi > b.rssi; });

  std::sort(networks.begin(), networks.end(), [](const WifiNetworkInfo& a, const WifiNetworkInfo& b) {
    return a.hasSavedPassword && !b.hasSavedPassword;
  });

  WiFi.scanDelete();
  state = WifiSelectionState::NETWORK_LIST;
  selectedNetworkIndex = 0;
  updateRequired = true;
}

/**
 * @brief Selects a network from the list to connect to
 * @param index Index of the network to select
 */
void WifiSelectionActivity::selectNetwork(const int index) {
  if (index < 0 || index >= static_cast<int>(networks.size())) {
    return;
  }

  const auto& network = networks[index];
  selectedSSID = network.ssid;
  selectedRequiresPassword = network.isEncrypted;
  usedSavedPassword = false;
  enteredPassword.clear();

  const auto* savedCred = WIFI_STORE.findCredential(selectedSSID);
  if (savedCred && !savedCred->password.empty()) {
    enteredPassword = savedCred->password;
    usedSavedPassword = true;
    attemptConnection();
    return;
  }

  if (selectedRequiresPassword) {
    state = WifiSelectionState::PASSWORD_ENTRY;
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    enterNewActivity(new KeyboardEntryActivity(
        renderer, mappedInput, "Enter WiFi Password", "", 50, 64, false,
        [this](const std::string& text) {
          enteredPassword = text;
          exitActivity();
        },
        [this] {
          state = WifiSelectionState::NETWORK_LIST;
          updateRequired = true;
          exitActivity();
        }));
    updateRequired = true;
    xSemaphoreGive(renderingMutex);
  } else {
    attemptConnection();
  }
}

/**
 * @brief Attempts to connect to the selected network
 */
void WifiSelectionActivity::attemptConnection() {
  state = WifiSelectionState::CONNECTING;
  connectionStartTime = millis();
  connectedIP.clear();
  connectionError.clear();
  updateRequired = true;

  WiFi.mode(WIFI_STA);

  if (selectedRequiresPassword && !enteredPassword.empty()) {
    WiFi.begin(selectedSSID.c_str(), enteredPassword.c_str());
  } else {
    WiFi.begin(selectedSSID.c_str());
  }
}

/**
 * @brief Checks the status of an ongoing connection attempt
 */
void WifiSelectionActivity::checkConnectionStatus() {
  if (state != WifiSelectionState::CONNECTING) {
    return;
  }

  const wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    connectedIP = ipStr;

    if (!usedSavedPassword && !enteredPassword.empty()) {
      state = WifiSelectionState::SAVE_PROMPT;
      savePromptSelection = 0;
      updateRequired = true;
    } else {
      onComplete(true);
    }
    return;
  }

  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
    connectionError = "Error: General failure";
    if (status == WL_NO_SSID_AVAIL) {
      connectionError = "Error: Network not found";
    }
    state = WifiSelectionState::CONNECTION_FAILED;
    updateRequired = true;
    return;
  }

  if (millis() - connectionStartTime > 15000) {
    WiFi.disconnect();
    connectionError = "Error: Connection timeout";
    state = WifiSelectionState::CONNECTION_FAILED;
    updateRequired = true;
    return;
  }
}

/**
 * @brief Main loop function called repeatedly while activity is active
 */
void WifiSelectionActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (state == WifiSelectionState::SCANNING) {
    processWifiScanResults();
    return;
  }

  if (state == WifiSelectionState::CONNECTING) {
    checkConnectionStatus();
    return;
  }

  if (state == WifiSelectionState::PASSWORD_ENTRY) {
    attemptConnection();
    return;
  }

  if (state == WifiSelectionState::SAVE_PROMPT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (savePromptSelection > 0) {
        savePromptSelection--;
        updateRequired = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (savePromptSelection < 1) {
        savePromptSelection++;
        updateRequired = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (savePromptSelection == 0) {
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        WIFI_STORE.addCredential(selectedSSID, enteredPassword);
        xSemaphoreGive(renderingMutex);
      }
      onComplete(true);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onComplete(true);
    }
    return;
  }

  if (state == WifiSelectionState::FORGET_PROMPT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (forgetPromptSelection > 0) {
        forgetPromptSelection--;
        updateRequired = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (forgetPromptSelection < 1) {
        forgetPromptSelection++;
        updateRequired = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (forgetPromptSelection == 1) {
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        WIFI_STORE.removeCredential(selectedSSID);
        xSemaphoreGive(renderingMutex);
        const auto network = find_if(networks.begin(), networks.end(),
                                     [this](const WifiNetworkInfo& net) { return net.ssid == selectedSSID; });
        if (network != networks.end()) {
          network->hasSavedPassword = false;
        }
      }
      state = WifiSelectionState::NETWORK_LIST;
      updateRequired = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = WifiSelectionState::NETWORK_LIST;
      updateRequired = true;
    }
    return;
  }

  if (state == WifiSelectionState::CONNECTED) {
    onComplete(true);
    return;
  }

  if (state == WifiSelectionState::CONNECTION_FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (usedSavedPassword) {
        state = WifiSelectionState::FORGET_PROMPT;
        forgetPromptSelection = 0;
      } else {
        state = WifiSelectionState::NETWORK_LIST;
      }
      updateRequired = true;
      return;
    }
  }

  if (state == WifiSelectionState::NETWORK_LIST) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onComplete(false);
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (!networks.empty()) {
        selectNetwork(selectedNetworkIndex);
      } else {
        startWifiScan();
      }
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (selectedNetworkIndex > 0) {
        selectedNetworkIndex--;
        updateRequired = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (!networks.empty() && selectedNetworkIndex < static_cast<int>(networks.size()) - 1) {
        selectedNetworkIndex++;
        updateRequired = true;
      }
    }
  }
}

/**
 * @brief Main display task loop running on separate thread
 */
void WifiSelectionActivity::displayTaskLoop() {
  while (true) {
    if (subActivity) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    if (state == WifiSelectionState::PASSWORD_ENTRY) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

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
 * @brief Renders the current screen content
 */
void WifiSelectionActivity::render() const {
  renderer.clearScreen();
  renderTabBar(renderer);
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int startY = TAB_BAR_HEIGHT;

  switch (state) {
    case WifiSelectionState::SCANNING:
      renderScanning(screenWidth, screenHeight, startY);
      break;
    case WifiSelectionState::NETWORK_LIST:
      renderNetworkList(screenWidth, screenHeight, startY);
      break;
    case WifiSelectionState::CONNECTING:
      renderConnecting(screenWidth, screenHeight, startY);
      break;
    case WifiSelectionState::SAVE_PROMPT:
      renderSavePrompt(screenWidth, screenHeight, startY);
      break;
    case WifiSelectionState::CONNECTION_FAILED:
      renderConnectionFailed(screenWidth, screenHeight, startY);
      break;
    case WifiSelectionState::FORGET_PROMPT:
      renderForgetPrompt(screenWidth, screenHeight, startY);
      break;
  }

  renderer.displayBuffer();
}

/**
 * @brief Renders the scanning state screen
 * @param screenWidth Width of the screen
 * @param screenHeight Height of the screen
 * @param startY Starting Y coordinate for content
 */
void WifiSelectionActivity::renderScanning(const int screenWidth, const int screenHeight, const int startY) const {
  const int headerHeight = TAB_BAR_HEIGHT;
  const int headerY = startY;

  const char* headerText = "WiFi Networks";
  int headerTextX = 20;
  int headerTextY = headerY + (headerHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, headerTextX, headerTextY - 10, headerText, true,
                    EpdFontFamily::BOLD);

  const char* subtitleText = "Scanning for networks...";
  int subtitleY = headerY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, screenWidth, dividerY);

  const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Scanning...");

  const auto labels = mappedInput.mapLabels("« Back", "", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

/**
 * @brief Renders the network list screen
 * @param screenWidth Width of the screen
 * @param screenHeight Height of the screen
 * @param startY Starting Y coordinate for content
 */
void WifiSelectionActivity::renderNetworkList(int screenWidth, int screenHeight, int startY) const {
  const int headerHeight = TAB_BAR_HEIGHT;
  const int headerY = startY;

  const char* headerText = "WiFi Networks";
  int headerTextX = 20;
  int headerTextY = headerY + (headerHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, headerTextX, headerTextY - 10, headerText, true,
                    EpdFontFamily::BOLD);

  const char* subtitleText = "Select a network to connect";
  int subtitleY = headerY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, screenWidth, dividerY);

  const int listStartY = dividerY;
  const int visibleAreaHeight = screenHeight - listStartY - 80;

  if (networks.empty()) {
    const int centerY = listStartY + (visibleAreaHeight / 2);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY - 20, "No networks found");
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY + 10, "Press Connect to scan again");
  } else {
    const int maxVisibleNetworks = visibleAreaHeight / LIST_ITEM_HEIGHT;

    int scrollOffset = 0;
    if (selectedNetworkIndex >= maxVisibleNetworks) {
      scrollOffset = selectedNetworkIndex - maxVisibleNetworks + 1;
    }

    int displayIndex = 0;
    for (size_t i = scrollOffset; i < networks.size() && displayIndex < maxVisibleNetworks; i++, displayIndex++) {
      const int itemY = listStartY + displayIndex * LIST_ITEM_HEIGHT;
      const auto& network = networks[i];
      const bool isSelected = (static_cast<int>(i) == selectedNetworkIndex);

      if (isSelected) {
        renderer.fillRect(0, itemY, screenWidth, LIST_ITEM_HEIGHT, GfxRenderer::FillTone::Ink);
      }

      std::string displayName = network.ssid;
      if (displayName.length() > 25) {
        displayName.replace(22, displayName.length() - 22, "...");
      }

      const int textX = 20;
      const int titleY = itemY + 20;

      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, titleY, displayName.c_str(), !isSelected);

      if (network.isEncrypted) {
        int lockTextX = textX + renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, displayName.c_str()) + 10;
        if (lockTextX < screenWidth - 150) {
          renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, lockTextX, titleY + 2, "(Locked)", !isSelected);
        }
      }

      drawWifiIcon(screenWidth - 60, itemY + 15, network.rssi, isSelected);

      if (network.hasSavedPassword) {
        renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenWidth - 80, itemY + 15, "+", !isSelected);
      }

      if (i < networks.size() - 1) {
        renderer.drawLine(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1);
      }
    }

    if (scrollOffset > 0) {
      renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, screenWidth - 15, listStartY, "^");
    }
    if (scrollOffset + maxVisibleNetworks < static_cast<int>(networks.size())) {
      renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, screenWidth - 15,
                        listStartY + maxVisibleNetworks * LIST_ITEM_HEIGHT, "v");
    }

    char countStr[32];
    snprintf(countStr, sizeof(countStr), "%zu networks found", networks.size());
    renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 20, screenHeight - 90, countStr);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 20, screenHeight - 105, cachedMacAddress.c_str());
  }

  const auto labels = mappedInput.mapLabels("« Back", "Connect", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

/**
 * @brief Renders the connecting state screen
 * @param screenWidth Width of the screen
 * @param screenHeight Height of the screen
 * @param startY Starting Y coordinate for content
 */
void WifiSelectionActivity::renderConnecting(const int screenWidth, const int screenHeight, const int startY) const {
  const int headerHeight = TAB_BAR_HEIGHT;
  const int headerY = startY;

  const char* headerText = "WiFi Networks";
  int headerTextX = 20;
  int headerTextY = headerY + (headerHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, headerTextX, headerTextY - 10, headerText, true,
                    EpdFontFamily::BOLD);

  const char* subtitleText = "Connecting...";
  int subtitleY = headerY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, screenWidth, dividerY);

  const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;

  std::string ssidInfo = selectedSSID;
  std::string connect = "Connecting to";
  if (ssidInfo.length() > 25) {
    ssidInfo.replace(22, ssidInfo.length() - 22, "...");
  }
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, centerY - 50, connect.c_str());
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, centerY - 20, ssidInfo.c_str());
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, centerY + 20, "Please wait...");

  const auto labels = mappedInput.mapLabels("", "", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

/**
 * @brief Renders the save password prompt screen
 * @param screenWidth Width of the screen
 * @param screenHeight Height of the screen
 * @param startY Starting Y coordinate for content
 */
void WifiSelectionActivity::renderSavePrompt(const int screenWidth, const int screenHeight, const int startY) const {
  const int headerHeight = TAB_BAR_HEIGHT;
  const int headerY = startY;

  const char* headerText = "WiFi Networks";
  int headerTextX = 20;
  int headerTextY = headerY + (headerHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, headerTextX, headerTextY - 10, headerText, true,
                    EpdFontFamily::BOLD);

  const char* subtitleText = "Connected successfully!";
  int subtitleY = headerY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, screenWidth, dividerY);

  const int promptY = dividerY + 30;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, promptY, "Save password for next time?");

  const int buttonY = promptY + 50;
  constexpr int buttonWidth = 60;
  constexpr int buttonSpacing = 30;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = (screenWidth - totalWidth) / 2;

  if (savePromptSelection == 0) {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, startX, buttonY, "[Yes]");
  } else {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, startX + 4, buttonY, "Yes");
  }

  if (savePromptSelection == 1) {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, startX + buttonWidth + buttonSpacing, buttonY, "[No]");
  } else {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, startX + buttonWidth + buttonSpacing + 4, buttonY, "No");
  }

  const auto labels = mappedInput.mapLabels("« Skip", "Select", "Left", "Right");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

/**
 * @brief Renders the connection failed screen
 * @param screenWidth Width of the screen
 * @param screenHeight Height of the screen
 * @param startY Starting Y coordinate for content
 */
void WifiSelectionActivity::renderConnectionFailed(const int screenWidth, const int screenHeight,
                                                   const int startY) const {
  const int headerHeight = TAB_BAR_HEIGHT;
  const int headerY = startY;

  const char* headerText = "WiFi Networks";
  int headerTextX = 20;
  int headerTextY = headerY + (headerHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, headerTextX, headerTextY - 10, headerText, true,
                    EpdFontFamily::BOLD);

  const char* subtitleText = "Connection Failed";
  int subtitleY = headerY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, screenWidth, dividerY);

  const int errorY = dividerY + 40;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, errorY - 20, connectionError.c_str());

  std::string ssidInfo = "Network: " + selectedSSID;
  if (ssidInfo.length() > 25) {
    ssidInfo.replace(22, ssidInfo.length() - 22, "...");
  }
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, errorY + 10, ssidInfo.c_str());

  const auto labels = mappedInput.mapLabels("« Back", "Continue", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

/**
 * @brief Renders the forget network prompt screen
 * @param screenWidth Width of the screen
 * @param screenHeight Height of the screen
 * @param startY Starting Y coordinate for content
 */
void WifiSelectionActivity::renderForgetPrompt(const int screenWidth, const int screenHeight, const int startY) const {
  const int headerHeight = TAB_BAR_HEIGHT;
  const int headerY = startY;

  const char* headerText = "WiFi Networks";
  int headerTextX = 20;
  int headerTextY = headerY + (headerHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, headerTextX, headerTextY - 10, headerText, true,
                    EpdFontFamily::BOLD);

  const char* subtitleText = "Connection Failed";
  int subtitleY = headerY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, screenWidth, dividerY);

  const int promptY = dividerY + 30;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, promptY, "Forget network and remove saved password?");

  const int buttonY = promptY + 50;
  constexpr int buttonWidth = 120;
  constexpr int buttonSpacing = 30;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = (screenWidth - totalWidth) / 2;

  if (forgetPromptSelection == 0) {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, startX, buttonY, "[Cancel]");
  } else {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, startX + 4, buttonY, "Cancel");
  }

  if (forgetPromptSelection == 1) {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, startX + buttonWidth + buttonSpacing, buttonY,
                      "[Forget network]");
  } else {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, startX + buttonWidth + buttonSpacing + 4, buttonY,
                      "Forget network");
  }

  const auto labels = mappedInput.mapLabels("« Back", "Select", "Left", "Right");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

/**
 * @brief Draws a WiFi signal strength icon
 * @param x X coordinate for the icon
 * @param y Y coordinate for the icon
 * @param rssi Signal strength in dBm
 * @param isSelected Whether the current item is selected
 */
void WifiSelectionActivity::drawWifiIcon(int x, int y, int32_t rssi, bool isSelected) const {
  int bar1Height = 7;
  int bar2Height = 14;
  int bar3Height = 21;
  int bar4Height = 28;
  int barWidth = 5;

  int maxHeight = bar4Height;
  int startY = y + (28 - maxHeight) / 2;

  int visibleBars = 0;
  if (rssi >= -80) visibleBars = 1;
  if (rssi >= -70) visibleBars = 2;
  if (rssi >= -60) visibleBars = 3;
  if (rssi >= -50) visibleBars = 4;

  bool drawColor = !isSelected;

  int bar1Y = startY + (maxHeight - bar1Height);
  int bar2Y = startY + (maxHeight - bar2Height);
  int bar3Y = startY + (maxHeight - bar3Height);
  int bar4Y = startY;

  if (visibleBars >= 1) {
    renderer.fillRect(x, bar1Y, barWidth, bar1Height, drawColor);
  } else {
    renderer.drawRect(x, bar1Y, barWidth, bar1Height, drawColor);
  }

  if (visibleBars >= 2) {
    renderer.fillRect(x + 10, bar2Y, barWidth, bar2Height, drawColor);
  } else {
    renderer.drawRect(x + 10, bar2Y, barWidth, bar2Height, drawColor);
  }

  if (visibleBars >= 3) {
    renderer.fillRect(x + 20, bar3Y, barWidth, bar3Height, drawColor);
  } else {
    renderer.drawRect(x + 20, bar3Y, barWidth, bar3Height, drawColor);
  }

  if (visibleBars >= 4) {
    renderer.fillRect(x + 30, bar4Y, barWidth, bar4Height, drawColor);
  } else {
    renderer.drawRect(x + 30, bar4Y, barWidth, bar4Height, drawColor);
  }
}