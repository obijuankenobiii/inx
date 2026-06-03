/**
 * @file SleepImagePickerActivity.cpp
 * @brief Definitions for SleepImagePickerActivity.
 */

#include "SleepImagePickerActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstring>
#include <iterator>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {
constexpr int SELECTOR_ROW_HEIGHT = 34;
constexpr int SELECTOR_HEADER_HEIGHT = 34;
constexpr int SELECTOR_VISIBLE_ROWS = 5;

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
  scrollOffset = 0;
  for (size_t i = 0; i < rows.size(); i++) {
    if (rows[i].value == SETTINGS.sleepCustomBmp) {
      selectedIndex = static_cast<int>(i);
      break;
    }
  }
  const int maxScroll = std::max(0, static_cast<int>(rows.size()) - itemsPerPage);
  if (selectedIndex > maxScroll) {
    scrollOffset = selectedIndex - itemsPerPage + 1;
  }
  if (scrollOffset < 0) {
    scrollOffset = 0;
  }

  updateRequired = true;

  xTaskCreate(&SleepImagePickerActivity::taskTrampoline, "SleepImagePickerTask", 4096, this, 1, &displayTaskHandle);
}

void SleepImagePickerActivity::rebuildRows() {
  rows.clear();
  rows.push_back({"Random (each sleep)", ""});

  std::vector<std::pair<std::string, std::string>> folderImages;
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
        folderImages.emplace_back(filename, filename);
      }
      file.close();
    }
    dir.close();
  }

  std::sort(folderImages.begin(), folderImages.end(),
             [](const std::pair<std::string, std::string>& a, const std::pair<std::string, std::string>& b) {
               return a.first < b.first;
             });

  std::transform(folderImages.begin(), folderImages.end(), std::back_inserter(rows),
                 [](const std::pair<std::string, std::string>& p) {
                   return SleepImagePickerActivity::Row{p.first, p.second};
                 });

  if (SdMan.exists("/sleep.bmp")) {
    rows.push_back({"sleep.bmp (SD root)", "/sleep.bmp"});
  }
  if (SdMan.exists("/sleep.jpg")) {
    rows.push_back({"sleep.jpg (SD root)", "/sleep.jpg"});
  }
  if (SdMan.exists("/sleep.jpeg")) {
    rows.push_back({"sleep.jpeg (SD root)", "/sleep.jpeg"});
  }
  itemsPerPage = SELECTOR_VISIBLE_ROWS;
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
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  constexpr int titleFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
  const int fontId = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  const int visibleRows = std::min(SELECTOR_VISIBLE_ROWS, static_cast<int>(rows.size()));
  const int panelW = std::min(pageWidth - 24, 380);
  const int panelH = SELECTOR_HEADER_HEIGHT + visibleRows * SELECTOR_ROW_HEIGHT + 18;
  const int panelX = (pageWidth - panelW) / 2;
  const int panelY = std::max(12, (pageHeight - panelH) / 2);

  renderer.rectangle.fill(panelX - 2, panelY - 2, panelW + 4, panelH + 4, true);
  renderer.rectangle.fill(panelX, panelY, panelW, panelH, false);
  renderer.rectangle.render(panelX, panelY, panelW, panelH, true);
  renderer.rectangle.fill(panelX, panelY, panelW, SELECTOR_HEADER_HEIGHT, true);

  const int titleY = panelY + (SELECTOR_HEADER_HEIGHT - renderer.text.getLineHeight(titleFont)) / 2;
  renderer.text.render(titleFont, panelX + 14, titleY, "Sleep image", false, EpdFontFamily::BOLD);

  const int maxScroll = std::max(0, static_cast<int>(rows.size()) - visibleRows);
  scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
  for (int i = 0; i < visibleRows; ++i) {
    const int index = scrollOffset + i;
    if (index >= static_cast<int>(rows.size())) {
      break;
    }
    const auto& row = rows[static_cast<size_t>(index)];
    const int rowY = panelY + SELECTOR_HEADER_HEIGHT + i * SELECTOR_ROW_HEIGHT;
    const bool isSelected = index == selectedIndex;

    if (isSelected) {
      renderer.rectangle.fill(panelX + 4, rowY + 2, panelW - 8, SELECTOR_ROW_HEIGHT - 4, true);
    }

    char line[128];
    truncateLabelToWidth(renderer, fontId, panelW - 44, row.label.c_str(), line, sizeof(line));
    const int textY = rowY + (SELECTOR_ROW_HEIGHT - renderer.text.getLineHeight(fontId)) / 2;
    renderer.text.render(fontId, panelX + 18, textY, line, !isSelected,
                         isSelected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  if (static_cast<int>(rows.size()) > visibleRows) {
    const int trackX = panelX + panelW - 10;
    const int trackY = panelY + SELECTOR_HEADER_HEIGHT + 4;
    const int trackH = visibleRows * SELECTOR_ROW_HEIGHT - 8;
    const int thumbH = std::max(8, trackH * visibleRows / static_cast<int>(rows.size()));
    const int thumbY = trackY + scrollOffset * std::max(1, trackH - thumbH) / maxScroll;
    renderer.rectangle.fill(trackX, trackY, 2, trackH, true);
    renderer.rectangle.fill(trackX - 2, thumbY, 6, thumbH, true);
  }

  const auto labels = mappedInput.mapLabels("Cancel", "Select", "Page -", "Page +");
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

  bool needRedraw = false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (selectedIndex > 0) {
      selectedIndex--;
      if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
      }
      needRedraw = true;
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (selectedIndex < static_cast<int>(rows.size()) - 1) {
      selectedIndex++;
      const int maxScroll = std::max(0, static_cast<int>(rows.size()) - itemsPerPage);
      if (selectedIndex > scrollOffset + itemsPerPage - 1) {
        scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      }
      needRedraw = true;
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    const int direction = mappedInput.wasPressed(MappedInputManager::Button::Left) ? -1 : 1;
    selectedIndex += direction * itemsPerPage;
    selectedIndex = std::max(0, std::min(selectedIndex, static_cast<int>(rows.size()) - 1));
    const int maxScroll = std::max(0, static_cast<int>(rows.size()) - itemsPerPage);
    if (selectedIndex < scrollOffset) {
      scrollOffset = selectedIndex;
    } else if (selectedIndex > scrollOffset + itemsPerPage - 1) {
      scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
    }
    needRedraw = true;
  }

  if (needRedraw) {
    updateRequired = true;
  }
}
