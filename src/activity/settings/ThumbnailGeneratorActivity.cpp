/**
 * @file ThumbnailGeneratorActivity.cpp
 * @brief Definitions for ThumbnailGeneratorActivity.
 */

#include "ThumbnailGeneratorActivity.h"

#include <Epub.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <cstring>
#include <string>

#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {
constexpr uint32_t kDisplayTaskStack = 4096;
constexpr uint32_t kWorkerTaskStack = 12288;
}

void ThumbnailGeneratorActivity::displayTaskTrampoline(void* param) {
  static_cast<ThumbnailGeneratorActivity*>(param)->displayTaskLoop();
}

void ThumbnailGeneratorActivity::workerTaskTrampoline(void* param) {
  static_cast<ThumbnailGeneratorActivity*>(param)->workerTaskLoop();
}

void ThumbnailGeneratorActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  updateRequired = true;
  cancelRequested = false;
  state = READY;
  processedCount = 0;
  generatedCount = 0;
  skippedCount = 0;
  failedCount = 0;
  currentPath[0] = '\0';

  xTaskCreate(&ThumbnailGeneratorActivity::displayTaskTrampoline, "ThumbGenDisplayTask", kDisplayTaskStack, this, 1,
              &displayTaskHandle);
}

void ThumbnailGeneratorActivity::onExit() {
  cancelRequested = true;

  const unsigned long start = millis();
  while (workerTaskHandle && millis() - start < 1500) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  ActivityWithSubactivity::onExit();

  if (workerTaskHandle) {
    vTaskDelete(workerTaskHandle);
    workerTaskHandle = nullptr;
  }
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
}

void ThumbnailGeneratorActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      if (renderingMutex && xSemaphoreTake(renderingMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        render();
        xSemaphoreGive(renderingMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void ThumbnailGeneratorActivity::startGeneration() {
  if (state == RUNNING || workerTaskHandle != nullptr) {
    return;
  }

  cancelRequested = false;
  state = RUNNING;
  processedCount = 0;
  generatedCount = 0;
  skippedCount = 0;
  failedCount = 0;
  currentPath[0] = '\0';
  updateRequired = true;

  xTaskCreate(&ThumbnailGeneratorActivity::workerTaskTrampoline, "ThumbGenWorkerTask", kWorkerTaskStack, this, 1,
              &workerTaskHandle);
}

bool ThumbnailGeneratorActivity::shouldSkipPath(const char* name) const {
  return name[0] == '.' || strcmp(name, "System Volume Information") == 0 || strcmp(name, ".metadata") == 0 ||
         strcmp(name, "sleep") == 0;
}

bool ThumbnailGeneratorActivity::isSupportedBookFile(const std::string& filename) const {
  return StringUtils::checkFileExtension(filename, ".epub") || StringUtils::checkFileExtension(filename, ".xtc");
}

bool ThumbnailGeneratorActivity::processBook(const std::string& path) {
  strlcpy(currentPath, path.c_str(), sizeof(currentPath));
  updateRequired = true;

  if (StringUtils::checkFileExtension(path, ".epub")) {
    Epub epub(path, "/.metadata/epub");
    const std::string thumbJpegPath = epub.getThumbJpegPath();
    const std::string thumbBmpPath = epub.getThumbBmpPath();
    if (SdMan.exists(thumbJpegPath.c_str()) || SdMan.exists(thumbBmpPath.c_str())) {
      skippedCount++;
      processedCount++;
      return true;
    }
    const bool ok = epub.load() && epub.generateThumbBmp();
    processedCount++;
    if (ok) {
      generatedCount++;
    } else {
      failedCount++;
    }
    return ok;
  }

  if (StringUtils::checkFileExtension(path, ".xtc")) {
    Xtc xtc(path, "/.metadata/xtc");
    const std::string thumbBmpPath = xtc.getThumbBmpPath();
    if (SdMan.exists(thumbBmpPath.c_str())) {
      skippedCount++;
      processedCount++;
      return true;
    }
    const bool ok = xtc.load() && xtc.generateThumbBmp();
    processedCount++;
    if (ok) {
      generatedCount++;
    } else {
      failedCount++;
    }
    return ok;
  }

  return false;
}

bool ThumbnailGeneratorActivity::scanPath(const std::string& path) {
  if (cancelRequested) {
    return false;
  }

  FsFile dir = SdMan.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return true;
  }

  dir.rewindDirectory();
  char name[256];

  while (!cancelRequested) {
    FsFile file = dir.openNextFile();
    if (!file) {
      break;
    }

    file.getName(name, sizeof(name));
    if (shouldSkipPath(name)) {
      file.close();
      continue;
    }

    std::string fullPath = path;
    if (fullPath.empty()) {
      fullPath = "/";
    }
    if (fullPath.back() != '/') {
      fullPath += "/";
    }
    fullPath += name;

    if (file.isDirectory()) {
      file.close();
      if (!scanPath(fullPath)) {
        dir.close();
        return false;
      }
      continue;
    }

    file.close();
    if (!isSupportedBookFile(name)) {
      continue;
    }
    processBook(fullPath);
    if ((processedCount % 4) == 0) {
      updateRequired = true;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  dir.close();
  return !cancelRequested;
}

void ThumbnailGeneratorActivity::workerTaskLoop() {
  scanPath("/");

  if (cancelRequested) {
    state = CANCELLED;
  } else if (failedCount > 0 && generatedCount == 0 && skippedCount == 0) {
    state = FAILED;
  } else {
    state = SUCCESS;
  }
  currentPath[0] = '\0';
  updateRequired = true;
  workerTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void ThumbnailGeneratorActivity::render() {
  renderer.clearScreen();
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 16, "Generate thumbnails", true, EpdFontFamily::BOLD);

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int bodyY = 70;
  char line[64];

  if (state == READY) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY, "Create missing EPUB/XTC thumbnails");
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY + 34, "Existing thumbnails are skipped");
    const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Start", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == RUNNING) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY, "Generating...");
    snprintf(line, sizeof(line), "Processed: %d", processedCount);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY + 34, line);
    snprintf(line, sizeof(line), "Generated: %d   Skipped: %d", generatedCount, skippedCount);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY + 68, line);
    snprintf(line, sizeof(line), "Failed: %d", failedCount);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY + 102, line);
    if (currentPath[0] != '\0') {
      const std::string path = renderer.text.truncate(ATKINSON_HYPERLEGIBLE_8_FONT_ID, currentPath, pageWidth - 60);
      renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, bodyY + 148, path.c_str());
    }
    const auto labels = mappedInput.mapLabels("Stop", "", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY, "Thumbnail generation complete", true,
                           EpdFontFamily::BOLD);
  } else if (state == CANCELLED) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY, "Thumbnail generation stopped", true,
                           EpdFontFamily::BOLD);
  } else {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY, "Thumbnail generation failed", true,
                           EpdFontFamily::BOLD);
  }

  snprintf(line, sizeof(line), "Processed: %d", processedCount);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY + 42, line);
  snprintf(line, sizeof(line), "Generated: %d   Skipped: %d", generatedCount, skippedCount);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY + 76, line);
  snprintf(line, sizeof(line), "Failed: %d", failedCount);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY + 110, line);

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void ThumbnailGeneratorActivity::loop() {
  if (state == READY) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      startGeneration();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
      return;
    }
    return;
  }

  if (state == RUNNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      cancelRequested = true;
      updateRequired = true;
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    goBack();
  }
}
