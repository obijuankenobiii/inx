#include "SleepImagePickerActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstring>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {
constexpr int LIST_ITEM_HEIGHT = 60;
constexpr int HEADER_BOTTOM = 88;
constexpr int FOOTER = 52;
constexpr int MAX_LABEL_PX_MARGIN = 36;

void truncateLabelToWidth(GfxRenderer& renderer, int fontId, int maxWidth, const char* text, char* out,
                          size_t outSize) {
  if (outSize == 0) {
    return;
  }
  strncpy(out, text, outSize - 1);
  out[outSize - 1] = '\0';
  if (renderer.getTextWidth(fontId, out) <= maxWidth) {
    return;
  }
  const char* ell = "...";
  const int ellW = renderer.getTextWidth(fontId, ell);
  size_t n = strlen(out);
  while (n > 0 && renderer.getTextWidth(fontId, out) + ellW > maxWidth) {
    out[--n] = '\0';
  }
  strncat(out, ell, outSize - strlen(out) - 1);
}
}  // namespace

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

  std::vector<std::pair<std::string, std::string>> folderBmps;
  auto dir = SdMan.open("/sleep");
  if (dir && dir.isDirectory()) {
    char name[256];
    while (auto file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      std::string filename = name;
      if (filename[0] != '.' && StringUtils::checkFileExtension(filename, ".bmp")) {
        folderBmps.emplace_back(filename, filename);
      }
      file.close();
    }
    dir.close();
  }

  std::sort(folderBmps.begin(), folderBmps.end(),
             [](const std::pair<std::string, std::string>& a, const std::pair<std::string, std::string>& b) {
               return a.first < b.first;
             });

  for (const auto& p : folderBmps) {
    rows.push_back({p.first, p.second});
  }

  if (SdMan.exists("/sleep.bmp")) {
    rows.push_back({"sleep.bmp (SD root)", "/sleep.bmp"});
  }

  const int listPixels = renderer.getScreenHeight() - HEADER_BOTTOM - FOOTER;
  itemsPerPage = listPixels / LIST_ITEM_HEIGHT;
  if (itemsPerPage < 1) {
    itemsPerPage = 1;
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
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, 25, "Sleep screen image", true, EpdFontFamily::BOLD);
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, 52, "Custom / transparent modes", true);

  const int startY = HEADER_BOTTOM;
  const int itemHeight = LIST_ITEM_HEIGHT;
  const int fontId = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  int visibleCount = 0;
  for (int i = 0; i < itemsPerPage && (i + scrollOffset) < static_cast<int>(rows.size()); i++) {
    const int index = i + scrollOffset;
    const auto& row = rows[static_cast<size_t>(index)];
    const int itemY = startY + visibleCount * itemHeight;
    const bool isSelected = (index == selectedIndex);

    if (isSelected) {
      renderer.fillRect(0, itemY, pageWidth, itemHeight);
    }

    char line[128];
    const int maxW = pageWidth - MAX_LABEL_PX_MARGIN;
    truncateLabelToWidth(renderer, fontId, maxW, row.label.c_str(), line, sizeof(line));

    const int textX = 20;
    const int textY = itemY + (itemHeight - renderer.getLineHeight(fontId)) / 2;
    renderer.drawText(fontId, textX, textY, line, !isSelected);

    renderer.drawLine(0, itemY + itemHeight - 1, pageWidth, itemY + itemHeight - 1, true);
    visibleCount++;
  }

  if (static_cast<int>(rows.size()) > itemsPerPage) {
    const int listHeight = itemsPerPage * itemHeight;
    int thumbH = (itemsPerPage * listHeight) / static_cast<int>(rows.size());
    if (thumbH < 4) {
      thumbH = 4;
    }
    const int thumbY = startY + (scrollOffset * listHeight) / static_cast<int>(rows.size());
    renderer.fillRect(pageWidth - 4, thumbY, 2, thumbH, true);
  }

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
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

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) || mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (selectedIndex > 0) {
      selectedIndex--;
      if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
      }
      needRedraw = true;
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (selectedIndex < static_cast<int>(rows.size()) - 1) {
      selectedIndex++;
      const int maxScroll = std::max(0, static_cast<int>(rows.size()) - itemsPerPage);
      if (selectedIndex > scrollOffset + itemsPerPage - 1) {
        scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      }
      needRedraw = true;
    }
  }

  if (needRedraw) {
    updateRequired = true;
  }
}
