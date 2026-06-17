/**
 * @file SleepImagePickerActivity.cpp
 * @brief Definitions for SleepImagePickerActivity.
 */

#include "SleepImagePickerActivity.h"

#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {
// Preview image occupies 70% of the screen, centered, like a single-item carousel.
constexpr int PREVIEW_PERCENT = 70;

void truncateLabelToWidth(const GfxRenderer& renderer, int fontId, int maxWidth, const char* text, char* out,
                          size_t outSize) {
  if (outSize == 0) {
    return;
  }
  strncpy(out, text, outSize - 1);
  out[outSize - 1] = '\0';
  if (renderer.text.getWidth(fontId, out) <= maxWidth) {
    return;
  }
  const char* ell = "...";
  const int ellW = renderer.text.getWidth(fontId, ell);
  size_t n = strlen(out);
  while (n > 0 && renderer.text.getWidth(fontId, out) + ellW > maxWidth) {
    out[--n] = '\0';
  }
  strncat(out, ell, outSize - strlen(out) - 1);
}
}  

void SleepImagePickerActivity::taskTrampoline(void* param) {
  auto* self = static_cast<SleepImagePickerActivity*>(param);
  self->displayTaskLoop();
}

void SleepImagePickerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  rebuildRows();

  selectedIndex = 0;
  for (size_t i = 0; i < rows.size(); i++) {
    if (rows[i].value == SETTINGS.sleepCustomBmp) {
      selectedIndex = static_cast<int>(i);
      break;
    }
  }

  updateRequired = true;

  xTaskCreate(&SleepImagePickerActivity::taskTrampoline, "SleepImagePickerTask", 4096, this, 1, &displayTaskHandle);
}

void SleepImagePickerActivity::rebuildRows() {
  rows.clear();
  rows.push_back({"Random (each sleep)", "", ""});

  std::vector<Row> folderImages;
  auto dir = SdMan.open("/sleep");
  if (dir && dir.isDirectory()) {
    char name[256];
    while (auto file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      std::string filename = name;
      const bool supported = StringUtils::checkFileExtension(filename, ".bmp") ||
                             StringUtils::checkFileExtension(filename, ".jpg") ||
                             StringUtils::checkFileExtension(filename, ".jpeg");
      if (filename[0] != '.' && supported) {
        folderImages.push_back({filename, filename, std::string("/sleep/") + filename});
      }
      file.close();
    }
    dir.close();
  }

  std::sort(folderImages.begin(), folderImages.end(), [](const Row& a, const Row& b) { return a.label < b.label; });
  rows.insert(rows.end(), folderImages.begin(), folderImages.end());

  if (SdMan.exists("/sleep.bmp")) {
    rows.push_back({"sleep.bmp (SD root)", "/sleep.bmp", "/sleep.bmp"});
  }
  if (SdMan.exists("/sleep.jpg")) {
    rows.push_back({"sleep.jpg (SD root)", "/sleep.jpg", "/sleep.jpg"});
  }
  if (SdMan.exists("/sleep.jpeg")) {
    rows.push_back({"sleep.jpeg (SD root)", "/sleep.jpeg", "/sleep.jpeg"});
  }
}

void SleepImagePickerActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void SleepImagePickerActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void SleepImagePickerActivity::render() {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  if (rows.empty()) {
    renderer.displayBuffer();
    return;
  }

  const auto& row = rows[static_cast<size_t>(selectedIndex)];

  // Centered preview occupying PREVIEW_PERCENT of the screen.
  const int previewW = pageWidth * PREVIEW_PERCENT / 100;
  const int previewH = pageHeight * PREVIEW_PERCENT / 100;
  const int previewX = (pageWidth - previewW) / 2;
  const int previewY = (pageHeight - previewH) / 2;

  bool rendered = false;
  if (!row.previewPath.empty()) {
    ImageRender::Options options;
    options.mode = ImageRenderMode::OneBit;
    options.useDisplayCache = false;
    rendered =
        ImageRender::create(renderer, row.previewPath).render(previewX, previewY, previewW, previewH, options);
  }

  if (rendered) {
    renderer.rectangle.render(previewX - 1, previewY - 1, previewW + 2, previewH + 2, true);
  } else {
    // "Random" entry or an image that failed to load: show a framed placeholder message.
    renderer.rectangle.render(previewX, previewY, previewW, previewH, true);
    const char* msg = row.value.empty() ? "Random each sleep" : "No preview";
    const int msgFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
    const int msgW = renderer.text.getWidth(msgFont, msg);
    renderer.text.render(msgFont, previewX + (previewW - msgW) / 2,
                         previewY + (previewH - renderer.text.getLineHeight(msgFont)) / 2, msg, true);
  }

  // Top bar: image name (left) and position counter (right), matching the clock picker.
  renderer.rectangle.fill(0, 0, pageWidth, 24, false);
  char label[128];
  truncateLabelToWidth(renderer, ATKINSON_HYPERLEGIBLE_8_FONT_ID, pageWidth - 70, row.label.c_str(), label,
                       sizeof(label));
  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 8, 6, label, true, EpdFontFamily::BOLD);
  char countText[8];
  std::snprintf(countText, sizeof(countText), "%d/%d", selectedIndex + 1, static_cast<int>(rows.size()));
  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID,
                       pageWidth - renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, countText) - 8, 6,
                       countText, true);

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Prev", "Next");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void SleepImagePickerActivity::applySelection() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(rows.size())) {
    return;
  }
  const std::string& v = rows[static_cast<size_t>(selectedIndex)].value;
  SETTINGS.setSleepCustomBmpFromInput(v.c_str());
  SETTINGS.saveToFile();
  onBack();
}

void SleepImagePickerActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    applySelection();
    return;
  }

  if (rows.empty()) {
    return;
  }

  const int count = static_cast<int>(rows.size());
  bool needRedraw = false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex + count - 1) % count;
    needRedraw = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % count;
    needRedraw = true;
  }

  if (needRedraw) {
    updateRequired = true;
  }
}
