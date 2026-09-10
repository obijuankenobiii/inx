/**
 * @file DeviceInfoActivity.cpp
 * @brief Definitions for DeviceInfoActivity.
 */

#include "DeviceInfoActivity.h"

#include <Arduino.h>
#include <EInkDisplay.h>
#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdio>

#include "activity/page/SubPage.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

std::string formatBytes(const size_t bytes) {
  char buffer[24];
  if (bytes >= 1024 * 1024) {
    snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(buffer, sizeof(buffer), "%u B", static_cast<unsigned>(bytes));
  }
  return std::string(buffer);
}

}  // namespace

void DeviceInfoActivity::buildRows() {
  rows.clear();

  const bool x3 = renderer.deviceIsX3();
  rows.push_back({"FIRMWARE", INX_VERSION});
  rows.push_back({"BOARD", x3 ? "Xteink X3" : "Xteink X4"});

  char displayLine[48];
  const uint16_t displayWidth = x3 ? EInkDisplay::X3_DISPLAY_WIDTH : EInkDisplay::DISPLAY_WIDTH;
  const uint16_t displayHeight = x3 ? EInkDisplay::X3_DISPLAY_HEIGHT : EInkDisplay::DISPLAY_HEIGHT;
  snprintf(displayLine, sizeof(displayLine), "E-Ink %ux%u", displayWidth, displayHeight);
  rows.push_back({"DISPLAY", displayLine});

  char chipLine[64];
  snprintf(chipLine, sizeof(chipLine), "%s rev %d, %d cores @ %d MHz", ESP.getChipModel(), ESP.getChipRevision(),
           ESP.getChipCores(), ESP.getCpuFreqMHz());
  rows.push_back({"CHIP", chipLine});

  rows.push_back({"FLASH", formatBytes(ESP.getFlashChipSize())});
  rows.push_back({"PSRAM", formatBytes(ESP.getPsramSize())});
  rows.push_back({"FREE HEAP", formatBytes(ESP.getFreeHeap())});
  rows.push_back({"ESP-IDF", esp_get_idf_version()});
  rows.push_back({"MAC ADDRESS", std::string(WiFi.macAddress().c_str())});
}

void DeviceInfoActivity::onEnter() {
  buildRows();
  updateRequired = true;
}

void DeviceInfoActivity::loop() {
  if (SubPage::closeInput(renderer, mappedInput, onClose)) return;
  if (updateRequired) {
    updateRequired = false;
    render();
  }
}

void DeviceInfoActivity::render() {
  renderer.clearScreen();
  const int startY = SubPage::header(renderer, "Device Information") + 20;
  const int screenWidth = renderer.getScreenWidth();
  const int rowHeight = rows.size() > 8
                            ? std::max(60, (renderer.getScreenHeight() - startY - 10) / static_cast<int>(rows.size()))
                            : 80;

  constexpr int labelFont = MONTSERRAT_8_FONT_ID;
  const int valueFont = systemFontId();

  for (size_t i = 0; i < rows.size(); ++i) {
    const int itemY = startY + static_cast<int>(i) * rowHeight;
    const int labelY = itemY + 7;
    const int valueY = itemY + 30;
    const int valueMaxW = screenWidth - 40;
    const std::string clippedValue = renderer.text.truncate(valueFont, rows[i].value.c_str(), valueMaxW);

    renderer.text.render(labelFont, 20, labelY, rows[i].label.c_str(), true, EpdFontFamily::BOLD);
    renderer.text.render(valueFont, 20, valueY, clippedValue.c_str(), true, EpdFontFamily::REGULAR);
  }

  renderer.displayBuffer();
}
