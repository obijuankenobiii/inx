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

#include <algorithm>
#include <cstring>
#include <string>

#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {
constexpr uint32_t kDisplayTaskStack = 4096;
constexpr uint32_t kWorkerTaskStack = 12288;

void drawThinProgressBar(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                         const int fillX, const int fillW) {
  renderer.rectangle.render(x, y, w, h, true);
  const int innerW = std::max(1, w - 2);
  renderer.rectangle.fill(x + 1, y + 1, innerW, h - 2, false);
  if (fillW > 0) {
    const int clampedX = std::max(0, std::min(innerW, fillX));
    const int clampedW = std::max(0, std::min(innerW - clampedX, fillW));
    if (clampedW > 0) {
      renderer.rectangle.fill(x + 1 + clampedX, y + 1, clampedW, h - 2, true);
    }
  }
}

void drawThumbnailProgressView(const GfxRenderer& renderer, const int pageWidth, const int screenHeight,
                               const bool running, const bool success, const bool cancelled,
                               const int processedCount, const int generatedCount, const int skippedCount,
                               const int failedCount, const char* currentPath) {
  const int centerY = screenHeight / 2;

  const char* eyebrow = running ? "GENERATING THUMBNAILS"
                        : success ? "THUMBNAILS READY"
                        : cancelled ? "GENERATION STOPPED"
                                    : "GENERATION FAILED";
  const char* title = running ? "Scanning library"
                      : success ? "Thumbnails complete"
                      : cancelled ? "Stopped"
                                  : "Thumbnail generation failed";
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, centerY - 92, eyebrow, true, EpdFontFamily::BOLD);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_14_FONT_ID, centerY - 54, title, true, EpdFontFamily::BOLD);

  char line[80];
  snprintf(line, sizeof(line), "Processed %d books", processedCount);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY - 10, line, true, EpdFontFamily::REGULAR);

  const int barW = std::min(300, pageWidth - 72);
  constexpr int barH = 6;
  const int barX = (pageWidth - barW) / 2;
  const int barY = centerY + 28;
  const int innerW = std::max(1, barW - 2);
  int fillX = 0;
  int fillW = 0;
  if (running) {
    fillW = std::max(36, innerW / 4);
    const int travel = std::max(1, innerW - fillW);
    fillX = (processedCount * 17) % travel;
  } else if (success) {
    fillW = innerW;
  }
  drawThinProgressBar(renderer, barX, barY, barW, barH, fillX, fillW);

  snprintf(line, sizeof(line), "Generated %d   Skipped %d   Failed %d", generatedCount, skippedCount, failedCount);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, barY + 26, line, true, EpdFontFamily::REGULAR);

  if (running && currentPath && currentPath[0] != '\0') {
    const std::string path = renderer.text.truncate(ATKINSON_HYPERLEGIBLE_8_FONT_ID, currentPath, pageWidth - 60);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, barY + 54, path.c_str(), true, EpdFontFamily::REGULAR);
  }
}
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

  const int pageWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int centerY = screenHeight / 2;

  if (state == READY) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, centerY - 92, "GENERATE THUMBNAILS", true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_14_FONT_ID, centerY - 54, "Build missing covers", true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY - 10, "Existing thumbnails are skipped.", true,
                           EpdFontFamily::REGULAR);

    const int barW = std::min(300, pageWidth - 72);
    constexpr int barH = 6;
    const int barX = (pageWidth - barW) / 2;
    const int barY = centerY + 28;
    drawThinProgressBar(renderer, barX, barY, barW, barH, 0, 0);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, barY + 26, "Ready to scan EPUB and XTC books", true,
                           EpdFontFamily::REGULAR);

    const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Start", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == RUNNING) {
    drawThumbnailProgressView(renderer, pageWidth, screenHeight, true, false, false, processedCount, generatedCount,
                              skippedCount, failedCount, currentPath);
    const auto labels = mappedInput.mapLabels("Stop", "", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  drawThumbnailProgressView(renderer, pageWidth, screenHeight, false, state == SUCCESS, state == CANCELLED,
                            processedCount, generatedCount, skippedCount, failedCount, nullptr);

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
