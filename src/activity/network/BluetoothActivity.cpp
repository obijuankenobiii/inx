#include "BluetoothActivity.h"

#include <GfxRenderer.h>

#include <cstdio>

#include "system/BluetoothManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr int LIST_ITEM_HEIGHT = 60;
}  // namespace

void BluetoothActivity::taskTrampoline(void* param) {
  auto* self = static_cast<BluetoothActivity*>(param);
  self->displayTaskLoop();
}

void BluetoothActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  if (!renderingMutex) return;

  btManager = &BluetoothManager::getInstance();

  selectedIndex = 0;
  devices.clear();
  state = BluetoothState::SCANNING;
  connectionError.clear();

  if (!btManager->isEnabled()) {
    btManager->enable();
  }

  updateRequired = true;

  xTaskCreate(&BluetoothActivity::taskTrampoline, "BluetoothTask", 4096, this, 1, &displayTaskHandle);

  startScan();
}

void BluetoothActivity::onExit() {
  ActivityWithSubactivity::onExit();

  if (btManager) btManager->stopScan();

  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }

  exitActivity();
}

void BluetoothActivity::startScan() {
  state = BluetoothState::SCANNING;
  devices.clear();
  updateRequired = true;

  btManager->startScan(3000);
  scanStartTime = millis();
}

void BluetoothActivity::processScanResults() {
  if (btManager->isScanning()) {
    return;
  }

  auto discovered = btManager->getDiscoveredDevices();
  devices.clear();

  for (const auto& dev : discovered) {
    DeviceInfo info;
    info.name = dev.name;
    info.address = dev.address;
    info.rssi = dev.rssi;
    devices.push_back(info);
  }

  state = BluetoothState::DEVICE_LIST;
  selectedIndex = 0;
  updateRequired = true;
}

void BluetoothActivity::connectToDevice(int index) {
  if (index < 0 || index >= (int)devices.size()) {
    return;
  }

  const auto& device = devices[index];

  state = BluetoothState::CONNECTING;
  updateRequired = true;

  if (btManager->connectToDevice(device.address)) {
    state = BluetoothState::CONNECTED;
  } else {
    connectionError = "Connection failed";
    state = BluetoothState::CONNECTION_FAILED;
  }
  updateRequired = true;
}

void BluetoothActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (state == BluetoothState::SCANNING) {
    if (millis() - scanStartTime >= 3000) {
      processScanResults();
    }
    return;
  }

  if (state == BluetoothState::CONNECTION_FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      state = BluetoothState::DEVICE_LIST;
      updateRequired = true;
    }
    return;
  }

  if (state == BluetoothState::DEVICE_LIST) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      if (onGoBack) onGoBack();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (!devices.empty()) {
        connectToDevice(selectedIndex);
      } else {
        startScan();
      }
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (selectedIndex > 0) {
        selectedIndex--;
        updateRequired = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (!devices.empty() && selectedIndex < (int)devices.size() - 1) {
        selectedIndex++;
        updateRequired = true;
      }
    }
  }

  if (updateRequired) {
    updateRequired = false;
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    render();
    xSemaphoreGive(renderingMutex);
  }
}

void BluetoothActivity::displayTaskLoop() {
  while (true) {
    if (subActivity) {
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

void BluetoothActivity::render() const {
  renderer.clearScreen();
  renderTabBar(renderer);
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int startY = TAB_BAR_HEIGHT;

  switch (state) {
    case BluetoothState::SCANNING:
      renderScanning();
      break;
    case BluetoothState::DEVICE_LIST:
      renderDeviceList();
      break;
    case BluetoothState::CONNECTING:
      renderConnecting();
      break;
    case BluetoothState::CONNECTED:
      renderConnected();
      break;
    case BluetoothState::CONNECTION_FAILED:
      renderConnectionFailed();
      break;
  }

  renderer.displayBuffer();
}

void BluetoothActivity::renderScanning() const {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int startY = TAB_BAR_HEIGHT;

  const char* headerText = "Bluetooth Devices";
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, startY + 10, headerText, true, EpdFontFamily::BOLD);

  const char* subtitleText = "Scanning for devices...";
  int subtitleY = startY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, screenWidth, dividerY);

  const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Scanning...");

  const auto labels = mappedInput.mapLabels("« Back", "", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BluetoothActivity::renderDeviceList() const {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int startY = TAB_BAR_HEIGHT;

  const char* headerText = "Bluetooth Devices";
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, startY + 10, headerText, true, EpdFontFamily::BOLD);

  const char* subtitleText = "Select a device to connect";
  int subtitleY = startY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, screenWidth, dividerY);

  const int listStartY = dividerY;
  const int visibleAreaHeight = screenHeight - listStartY - 80;

  if (devices.empty()) {
    const int centerY = listStartY + (visibleAreaHeight / 2);
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY - 20, "No devices found");
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY + 10, "Press Connect to scan again");
  } else {
    const int maxVisible = visibleAreaHeight / LIST_ITEM_HEIGHT;

    int scrollOffset = 0;
    if (selectedIndex >= maxVisible) {
      scrollOffset = selectedIndex - maxVisible + 1;
    }

    int displayIndex = 0;
    for (size_t i = scrollOffset; i < devices.size() && displayIndex < maxVisible; i++, displayIndex++) {
      const int itemY = listStartY + displayIndex * LIST_ITEM_HEIGHT;
      const auto& device = devices[i];
      const bool isSelected = (static_cast<int>(i) == selectedIndex);

      if (isSelected) {
        renderer.fillRect(0, itemY, screenWidth, LIST_ITEM_HEIGHT, GfxRenderer::FillTone::Ink);
      }

      std::string displayName = device.name;
      if (displayName.length() > 25) {
        displayName.replace(22, displayName.length() - 22, "...");
      }

      const int textX = 20;
      const int titleY = itemY + 20;

      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, titleY, displayName.c_str(), !isSelected);

      drawBluetoothIcon(screenWidth - 60, itemY + 15, device.rssi, isSelected);

      if (i < devices.size() - 1) {
        renderer.drawLine(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1);
      }
    }

    char countStr[32];
    snprintf(countStr, sizeof(countStr), "%zu devices found", devices.size());
    renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 20, screenHeight - 60, countStr);
  }

  const auto labels = mappedInput.mapLabels("« Back", "Connect", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BluetoothActivity::renderConnecting() const {
  const int screenHeight = renderer.getScreenHeight();
  const int startY = TAB_BAR_HEIGHT;

  const char* headerText = "Bluetooth Devices";
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, startY + 10, headerText, true, EpdFontFamily::BOLD);

  const char* subtitleText = "Connecting...";
  int subtitleY = startY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, renderer.getScreenWidth(), dividerY);

  const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;

  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, centerY - 20, "Connecting to device...");
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, centerY + 20, "Please wait...");

  const auto labels = mappedInput.mapLabels("", "", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BluetoothActivity::renderConnected() const {
  const int screenHeight = renderer.getScreenHeight();
  const int startY = TAB_BAR_HEIGHT;

  const char* headerText = "Bluetooth Devices";
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, startY + 10, headerText, true, EpdFontFamily::BOLD);

  const char* subtitleText = "Connected!";
  int subtitleY = startY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, renderer.getScreenWidth(), dividerY);

  const int centerY = dividerY + (screenHeight - dividerY - 80) / 2;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, centerY, "Connected to keyboard!");
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY + 30, "Press Back to exit");

  const auto labels = mappedInput.mapLabels("« Back", "", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BluetoothActivity::renderConnectionFailed() const {
  const int screenHeight = renderer.getScreenHeight();
  const int startY = TAB_BAR_HEIGHT;

  const char* headerText = "Bluetooth Devices";
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, startY + 10, headerText, true, EpdFontFamily::BOLD);

  const char* subtitleText = "Connection Failed";
  int subtitleY = startY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, renderer.getScreenWidth(), dividerY);

  const int errorY = dividerY + 40;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, errorY, connectionError.c_str());

  const auto labels = mappedInput.mapLabels("« Back", "Continue", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BluetoothActivity::drawBluetoothIcon(int x, int y, int rssi, bool isSelected) const {
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