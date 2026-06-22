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

#include "state/BookState.h"
#include "state/NetworkCredential.h"
#include "state/RecentBooks.h"
#include "state/Session.h"
#include "state/SystemSetting.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/Fonts.h"

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

  xTaskCreate(&ClearCacheActivity::taskTrampoline, "ClearCacheActivityTask",
              4096,               
              this,               
              1,                  
              &displayTaskHandle  
  );
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
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 15, "Clear cache", true, EpdFontFamily::BOLD);

  if (state == WARNING) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 54, "Select cache groups to remove");

    constexpr const char* names[GROUP_COUNT] = {"Display", "Book", "Recent", "Network"};
    constexpr const char* descriptions[GROUP_COUNT] = {
        "Rendered image/display planes",
        "EPUB, XTC, TXT book caches",
        "Recent list and library index",
        "Saved Wi-Fi credentials",
    };
    constexpr int rowH = 62;
    const int listTop = 100;
    const int left = 38;
    const int right = pageWidth - 38;
    const int boxSize = 20;
    for (int i = 0; i < GROUP_COUNT; ++i) {
      const int y = listTop + i * rowH;
      const bool focused = i == selectedGroup;
      if (focused) {
        renderer.rectangle.fill(left - 8, y - 8, right - left + 16, rowH - 4,
                                static_cast<int>(GfxRenderer::FillTone::Ink), false);
      }
      renderer.rectangle.render(left, y, boxSize, boxSize, !focused);
      if (selectedGroups[i]) {
        renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, left + 5, y - 3, "x", !focused);
      }
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, left + boxSize + 14, y - 2, names[i], !focused,
                           focused ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, left + boxSize + 14, y + 24, descriptions[i], !focused);
    }

    const int actionY = listTop + GROUP_COUNT * rowH + 8;
    const bool actionFocused = selectedGroup == GROUP_COUNT;
    if (actionFocused) {
      renderer.rectangle.fill(left - 8, actionY - 8, right - left + 16, 44,
                              static_cast<int>(GfxRenderer::FillTone::Ink), false);
    }
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, left, actionY, "Clear selected",
                         actionFocused ? false : anyGroupSelected(),
                         actionFocused ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    if (!anyGroupSelected()) {
      renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, pageHeight - 74, "Select at least one group");
    }

    const auto labels = mappedInput.mapLabels("\xC2\xAB Cancel", actionFocused ? "Clear" : "Toggle", "Up", "Down");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CLEARING) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2, "Clearing...", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 - 20, "Cache cleared", true, EpdFontFamily::BOLD);
    String resultText = String(clearedCount) + " items removed";
    if (failedCount > 0) {
      resultText += ", " + String(failedCount) + " failed";
    }
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 + 10, resultText.c_str());

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 - 20, "Clear failed", true, EpdFontFamily::BOLD);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageHeight / 2 + 10, "Check serial output for details");

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
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
