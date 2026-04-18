#include "BluetoothActivity.h"

#include <GfxRenderer.h>

#include <cstdio>
#include <strings.h>

#include "state/BleDeviceStore.h"
#include "state/SystemSetting.h"
#include "system/BluetoothManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr int LIST_ITEM_HEIGHT = 60;

struct HidPagePreset {
  const char* label;
  uint8_t code;
};

constexpr HidPagePreset kHidPagePresets[] = {
    {"Up", 0x52},        {"Down", 0x51},      {"Left", 0x50},     {"Right", 0x4F},
    {"Page Up", 0x4B},   {"Page Down", 0x4E}, {"Space", 0x2C},    {"Comma", 0x36},
    {"Period", 0x37},    {"[", 0x2F},         {"]", 0x30},
};

constexpr int kHidPagePresetCount = sizeof(kHidPagePresets) / sizeof(kHidPagePresets[0]);

/** Hold Back at least this long on release to disconnect BLE before leaving. */
constexpr unsigned long kBackHoldDisconnectMs = 800;

int findPresetIndex(uint8_t code) {
  for (int i = 0; i < kHidPagePresetCount; ++i) {
    if (kHidPagePresets[i].code == code) {
      return i;
    }
  }
  return -1;
}
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

  BLE_DEVICES.loadFromFile();

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

  if (btManager) {
    btManager->stopScan();
  }

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

  rebuildMergedDeviceList();
  state = BluetoothState::DEVICE_LIST;
  selectedIndex = 0;
  updateRequired = true;
}

void BluetoothActivity::rebuildMergedDeviceList() {
  devices.clear();
  if (btManager == nullptr) {
    return;
  }

  const auto discovered = btManager->getDiscoveredDevices();
  const auto& stored = BLE_DEVICES.devices();
  for (size_t i = 0; i < stored.size(); ++i) {
    int rssi = -100;
    uint8_t addrType = stored[i].addrType;
    for (const auto& dev : discovered) {
      if (strcasecmp(dev.address.c_str(), stored[i].address.c_str()) == 0) {
        rssi = dev.rssi;
        addrType = dev.addrType;
        break;
      }
    }
    DeviceInfo di;
    di.name = stored[i].name;
    di.address = stored[i].address;
    di.rssi = rssi;
    di.storeIndex = static_cast<int>(i);
    di.addrType = addrType;
    devices.push_back(di);
  }

  for (const auto& dev : discovered) {
    if (!BleDeviceStore::isDisplayableName(dev.name)) {
      continue;
    }
    if (BLE_DEVICES.findIndexByAddress(dev.address) >= 0) {
      continue;
    }
    DeviceInfo info;
    info.name = dev.name;
    info.address = dev.address;
    info.rssi = dev.rssi;
    info.storeIndex = -1;
    info.addrType = dev.addrType;
    devices.push_back(info);
  }
}

void BluetoothActivity::connectToDevice(int index) {
  if (index < 0 || index >= (int)devices.size()) {
    return;
  }
  if (renderingMutex == nullptr || btManager == nullptr) {
    return;
  }

  const auto& device = devices[index];

  state = BluetoothState::CONNECTING;

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  updateRequired = false;
  render();

  const bool ok = btManager->connectToDevice(device.address, device.addrType);

  if (ok) {
    state = BluetoothState::CONNECTED;
  } else {
    connectionError = "Connection failed";
    state = BluetoothState::CONNECTION_FAILED;
  }
  updateRequired = false;
  render();
  xSemaphoreGive(renderingMutex);
}

void BluetoothActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (state == BluetoothState::SCANNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      if (onGoBack) {
        onGoBack();
      }
      return;
    }
    if (millis() - scanStartTime >= 3000) {
      processScanResults();
    }
    return;
  }

  if (state == BluetoothState::CONNECTION_FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      rebuildMergedDeviceList();
      state = BluetoothState::DEVICE_LIST;
      if (selectedIndex >= (int)devices.size()) {
        selectedIndex = devices.empty() ? 0 : static_cast<int>(devices.size()) - 1;
      }
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
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (!devices.empty() && devices[static_cast<size_t>(selectedIndex)].storeIndex >= 0) {
        BLE_DEVICES.removeAt(static_cast<size_t>(devices[static_cast<size_t>(selectedIndex)].storeIndex));
        rebuildMergedDeviceList();
        if (selectedIndex >= (int)devices.size()) {
          selectedIndex = devices.empty() ? 0 : static_cast<int>(devices.size()) - 1;
        }
        updateRequired = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (!devices.empty()) {
        const auto& sel = devices[static_cast<size_t>(selectedIndex)];
        BLE_DEVICES.applyPreferred(sel.address, sel.name, sel.addrType);
        updateRequired = true;
      }
    }
    // fall through to refresh display when only navigation keys updated
  }

  if (state == BluetoothState::KEY_MAP_PREV || state == BluetoothState::KEY_MAP_NEXT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = BluetoothState::CONNECTED;
      updateRequired = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (keyMapPresetIndex > 0) {
        keyMapPresetIndex--;
        updateRequired = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (keyMapPresetIndex < kHidPagePresetCount - 1) {
        keyMapPresetIndex++;
        updateRequired = true;
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (state == BluetoothState::KEY_MAP_PREV) {
        SETTINGS.bleHidPagePrevKey = kHidPagePresets[keyMapPresetIndex].code;
        SETTINGS.saveToFile();
        state = BluetoothState::KEY_MAP_NEXT;
        keyMapPresetIndex = findPresetIndex(SETTINGS.bleHidPageNextKey);
        if (keyMapPresetIndex < 0) {
          keyMapPresetIndex = 0;
        }
      } else {
        SETTINGS.bleHidPageNextKey = kHidPagePresets[keyMapPresetIndex].code;
        SETTINGS.saveToFile();
        state = BluetoothState::CONNECTED;
      }
      updateRequired = true;
    }
  } else if (state == BluetoothState::CONNECTED) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (btManager != nullptr && mappedInput.getHeldTime() >= kBackHoldDisconnectMs) {
        btManager->disconnectAll();
      }
      if (onGoBack) {
        onGoBack();
      }
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      SETTINGS.bleAutoReconnect = SETTINGS.bleAutoReconnect ? 0 : 1;
      SETTINGS.saveToFile();
      updateRequired = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      state = BluetoothState::KEY_MAP_PREV;
      keyMapPresetIndex = findPresetIndex(SETTINGS.bleHidPagePrevKey);
      if (keyMapPresetIndex < 0) {
        keyMapPresetIndex = 0;
      }
      updateRequired = true;
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
    case BluetoothState::KEY_MAP_PREV:
      renderKeyMapPrev();
      break;
    case BluetoothState::KEY_MAP_NEXT:
      renderKeyMapNext();
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

  const char* subtitleText = "Connect · Right = preferred · Left = forget (saved)";
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
      if (SETTINGS.bleSavedAddress[0] != '\0' &&
          strcasecmp(SETTINGS.bleSavedAddress, device.address.c_str()) == 0) {
        displayName += " *";
      }
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

  const bool canForget =
      !devices.empty() && devices[static_cast<size_t>(selectedIndex)].storeIndex >= 0;
  const auto labels =
      mappedInput.mapLabels("« Back", "Connect", canForget ? "Forget" : "", "Preferred");
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
  const int screenWidth = renderer.getScreenWidth();
  const int startY = TAB_BAR_HEIGHT;

  const char* headerText = "Bluetooth Devices";
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, startY + 10, headerText, true, EpdFontFamily::BOLD);

  const char* subtitleText = "Connected!";
  int subtitleY = startY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, screenWidth, dividerY);

  int lineY = dividerY + 12;
  if (SETTINGS.bleSavedName[0] != '\0') {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, lineY, SETTINGS.bleSavedName);
    lineY += 22;
  }
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, lineY,
                            SETTINGS.bleAutoReconnect ? "Auto-reconnect: On (Up toggles)" : "Auto-reconnect: Off (Up toggles)");
  lineY += 22;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, lineY, "Reader page keys: release (not hold) on keyboard");
  lineY += 20;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, lineY, "Esc / Enter / arrows = device buttons (press)");
  lineY += 22;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, lineY, "Right = map BLE keys to page back / forward");
  lineY += 20;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, lineY, "Back: leave (keyboard stays on)");
  lineY += 18;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, lineY, "Hold Back ~1s: disconnect then leave");

  const auto labels = mappedInput.mapLabels("« Back", "", "", "Map keys");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BluetoothActivity::renderKeyMapPrev() const {
  const int screenHeight = renderer.getScreenHeight();
  const int startY = TAB_BAR_HEIGHT;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, startY + 10, "BLE page back key", true, EpdFontFamily::BOLD);
  const int dividerY = startY + 42;
  renderer.drawLine(0, dividerY, renderer.getScreenWidth(), dividerY);
  const int mid = dividerY + (screenHeight - dividerY) / 2 - 20;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, mid, kHidPagePresets[keyMapPresetIndex].label);
  char hex[24];
  snprintf(hex, sizeof(hex), "HID code 0x%02X", kHidPagePresets[keyMapPresetIndex].code);
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, mid + 24, hex);
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, mid + 48, "Up/Down change  Confirm next  Back cancel");
  const auto labels = mappedInput.mapLabels("« Back", "Next", "", "Up/Down");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BluetoothActivity::renderKeyMapNext() const {
  const int screenHeight = renderer.getScreenHeight();
  const int startY = TAB_BAR_HEIGHT;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, startY + 10, "BLE page forward key", true, EpdFontFamily::BOLD);
  const int dividerY = startY + 42;
  renderer.drawLine(0, dividerY, renderer.getScreenWidth(), dividerY);
  const int mid = dividerY + (screenHeight - dividerY) / 2 - 20;
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, mid, kHidPagePresets[keyMapPresetIndex].label);
  char hex[24];
  snprintf(hex, sizeof(hex), "HID code 0x%02X", kHidPagePresets[keyMapPresetIndex].code);
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, mid + 24, hex);
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, mid + 48, "Up/Down change  Confirm done  Back cancel");
  const auto labels = mappedInput.mapLabels("« Back", "Done", "", "Up/Down");
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