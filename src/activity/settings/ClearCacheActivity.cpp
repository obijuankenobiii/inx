/**
 * @file ClearCacheActivity.cpp
 * @brief Definitions for ClearCacheActivity.
 */

#include "ClearCacheActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "ReaderFontSettingsDraw.h"
#include "state/BookState.h"
#include "state/NetworkCredential.h"
#include "state/RecentBooks.h"
#include "state/Session.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/UiTheme.h"

namespace {
constexpr int kBodyFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
constexpr int kMetaFont = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
constexpr int kListItemHeight = UiTheme::DRAWER_LIST_ITEM_HEIGHT;
}  // namespace

void ClearCacheActivity::taskTrampoline(void* param) {
  auto* self = static_cast<ClearCacheActivity*>(param);
  self->displayTaskLoop();
}

void ClearCacheActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  state = WARNING;
  selectedGroup = 0;
  updateRequired = true;

  xTaskCreate(&ClearCacheActivity::taskTrampoline, "ClearCacheActivityTask", 4096, this, 1, &displayTaskHandle);
}

void ClearCacheActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void ClearCacheActivity::displayTaskLoop() {
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

void ClearCacheActivity::render() {
  const auto pageHeight = renderer.getScreenHeight();
  const auto pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  const int bodyTop = INX_THEME.drawPageHeader(renderer, "Clear cache");

  if (state == WARNING) {
    constexpr const char* names[GROUP_COUNT] = {"Display", "Book", "Recent", "Network"};
    constexpr int rowH = kListItemHeight;
    const int listTop = bodyTop + 1;
    const int left = 20;
    for (int i = 0; i < GROUP_COUNT; ++i) {
      const int y = listTop + i * rowH;
      const bool focused = i == selectedGroup;
      if (focused) {
        renderer.rectangle.fill(0, y, pageWidth, rowH, static_cast<int>(GfxRenderer::FillTone::Ink));
      }
      const int textY = y + (rowH - renderer.text.getLineHeight(kBodyFont)) / 2;
      renderer.text.render(kBodyFont, left, textY, names[i], !focused, EpdFontFamily::REGULAR);
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, pageWidth - 24, y, rowH, focused, selectedGroups[i]);
      renderer.line.render(0, y + rowH - 1, pageWidth, y + rowH - 1, true, LineRender::Style::Dotted);
    }

    const int actionY = listTop + GROUP_COUNT * rowH;
    const bool actionFocused = selectedGroup == GROUP_COUNT;
    if (actionFocused) {
      renderer.rectangle.fill(0, actionY, pageWidth, rowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    const int actionTextY = actionY + (rowH - renderer.text.getLineHeight(kBodyFont)) / 2;
    renderer.text.render(kBodyFont, left, actionTextY, "Clear selected", actionFocused ? false : anyGroupSelected(),
                         actionFocused ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.line.render(0, actionY + rowH - 1, pageWidth, actionY + rowH - 1, true, LineRender::Style::Dotted);

    if (!anyGroupSelected()) {
      renderer.text.centered(kMetaFont, pageHeight - 74, "Select a cache group");
    }

    const auto labels = mappedInput.mapLabels("\xC2\xAB Cancel", actionFocused ? "Clear" : "Toggle", "Up", "Down");
    renderer.ui.buttonHints(kBodyFont, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CLEARING) {
    renderer.text.centered(kBodyFont, pageHeight / 2, "Clearing...", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.text.centered(kBodyFont, pageHeight / 2 - 20, "Cache cleared", true, EpdFontFamily::BOLD);
    String resultText = String(clearedCount) + " items removed";
    if (failedCount > 0) {
      resultText += ", " + String(failedCount) + " failed";
    }
    renderer.text.centered(kBodyFont, pageHeight / 2 + 10, resultText.c_str());

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.ui.buttonHints(kBodyFont, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.text.centered(kBodyFont, pageHeight / 2 - 20, "Clear failed", true, EpdFontFamily::BOLD);
    renderer.text.centered(kBodyFont, pageHeight / 2 + 10, "Check serial output for details");

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.ui.buttonHints(kBodyFont, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

bool ClearCacheActivity::anyGroupSelected() const {
  for (int i = 0; i < GROUP_COUNT; ++i) {
    if (selectedGroups[i]) {
      return true;
    }
  }
  return false;
}

void ClearCacheActivity::clearCache() {
  Serial.printf("[%lu] [CLEAR_CACHE] Clearing selected cache groups...\n", millis());

  clearedCount = 0;
  failedCount = 0;

  const auto tryRemoveTree = [&](const char* path) {
    if (!SdMan.exists(path)) {
      Serial.printf("[%lu] [CLEAR_CACHE] %s not present\n", millis(), path);
      return;
    }
    Serial.printf("[%lu] [CLEAR_CACHE] Removing %s\n", millis(), path);
    if (SdMan.removeDir(path)) {
      clearedCount++;
      Serial.printf("[%lu] [CLEAR_CACHE] Removed %s\n", millis(), path);
    } else {
      failedCount++;
      Serial.printf("[%lu] [CLEAR_CACHE] Failed to remove %s\n", millis(), path);
    }
  };

  const auto tryRemoveFile = [&](const char* path) {
    if (!SdMan.exists(path)) {
      Serial.printf("[%lu] [CLEAR_CACHE] %s not present\n", millis(), path);
      return;
    }
    Serial.printf("[%lu] [CLEAR_CACHE] Removing %s\n", millis(), path);
    if (SdMan.remove(path)) {
      clearedCount++;
      Serial.printf("[%lu] [CLEAR_CACHE] Removed %s\n", millis(), path);
    } else {
      failedCount++;
      Serial.printf("[%lu] [CLEAR_CACHE] Failed to remove %s\n", millis(), path);
    }
  };

  const auto removeSystemTxtCaches = [&]() {
    FsFile root = SdMan.open("/.system");
    if (!root || !root.isDirectory()) {
      if (root) {
        root.close();
      }
      return;
    }
    char name[128];
    root.rewindDirectory();
    while (true) {
      FsFile entry = root.openNextFile();
      if (!entry) {
        break;
      }
      if (entry.isDirectory()) {
        entry.getName(name, sizeof(name));
        if (strncmp(name, "txt_", 4) == 0) {
          const std::string path = std::string("/.system/") + name;
          entry.close();
          tryRemoveTree(path.c_str());
          continue;
        }
      }
      entry.close();
    }
    root.close();
  };

  if (selectedGroups[GROUP_DISPLAY]) {
    tryRemoveTree("/.system/cache");
    tryRemoveTree("/.display-cache");
  }
  if (selectedGroups[GROUP_BOOK]) {
    tryRemoveTree("/.metadata/epub");
    tryRemoveTree("/.metadata/xtc");
    removeSystemTxtCaches();
  }
  if (selectedGroups[GROUP_RECENT]) {
    tryRemoveFile("/.metadata/recent.bin");
    tryRemoveFile("/.metadata/books.bin");
    tryRemoveTree("/.metadata/library");
  }
  if (selectedGroups[GROUP_NETWORK]) {
    tryRemoveFile("/.system/wifi.bin");
  }

  Serial.printf("[%lu] [CLEAR_CACHE] Done: %d removed, %d failed\n", millis(), clearedCount, failedCount);

  if (failedCount > 0 && clearedCount == 0) {
    state = FAILED;
    updateRequired = true;
    return;
  }

  if (selectedGroups[GROUP_RECENT]) {
    RECENT_BOOKS.clear(false);
    BOOK_STATE.clear(false);
  }
  if (selectedGroups[GROUP_NETWORK]) {
    (void)WIFI_STORE.loadFromFile();
  }

  state = SUCCESS;
  updateRequired = true;
}

void ClearCacheActivity::loop() {
  if (state == WARNING) {
    if (mappedInput.wasPressed(MenuNav::itemPrev())) {
      if (selectedGroup > 0) {
        selectedGroup--;
        updateRequired = true;
      }
      return;
    }

    if (mappedInput.wasPressed(MenuNav::itemNext())) {
      if (selectedGroup < GROUP_COUNT) {
        selectedGroup++;
        updateRequired = true;
      }
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (selectedGroup < GROUP_COUNT) {
        selectedGroups[selectedGroup] = !selectedGroups[selectedGroup];
        updateRequired = true;
        return;
      }
      if (!anyGroupSelected()) {
        updateRequired = true;
        return;
      }
      Serial.printf("[%lu] [CLEAR_CACHE] User confirmed selected cache clear\n", millis());
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = CLEARING;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      vTaskDelay(10 / portTICK_PERIOD_MS);

      clearCache();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      Serial.printf("[%lu] [CLEAR_CACHE] User cancelled\n", millis());
      goBack();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }
}
