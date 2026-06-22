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
#include <iterator>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "util/StringUtils.h"

namespace {
// Preview image occupies 70% of the screen, centered, like a single-item carousel.
constexpr int PREVIEW_PERCENT = 70;

}  

void SleepImagePickerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  rebuildRows();

  randomEnabled = SETTINGS.sleepCustomBmp[0] == '\0';
  selectedIndex = 0;
  for (size_t i = 0; i < rows.size(); i++) {
    if (rows[i].value == SETTINGS.sleepCustomBmp) {
      selectedIndex = static_cast<int>(i);
      break;
    }
  }

  requestRedraw();
}

void SleepImagePickerActivity::rebuildRows() {
  rows.clear();

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
}

void SleepImagePickerActivity::render() {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const int rowCount = static_cast<int>(rows.size());
  int localSelectedIndex = selectedIndex;
  if (localSelectedIndex < 0) {
    localSelectedIndex = 0;
  }
  if (rowCount > 0 && localSelectedIndex >= rowCount) {
    localSelectedIndex = rowCount - 1;
  }
  const bool localRandomEnabled = randomEnabled;

  renderer.clearScreen();

  const bool hasImages = rowCount > 0;
  const Row* row = hasImages ? &rows[static_cast<size_t>(localSelectedIndex)] : nullptr;

  // Centered preview occupying PREVIEW_PERCENT of the screen.
  const int previewW = pageWidth * PREVIEW_PERCENT / 100;
  const int previewH = pageHeight * PREVIEW_PERCENT / 100;
  const int previewX = (pageWidth - previewW) / 2;
  const int previewY = (pageHeight - previewH) / 2;

  bool rendered = false;
  if (row != nullptr && !row->previewPath.empty()) {
    ImageRender::Options options;
    options.mode = ImageRenderMode::OneBit;
    options.cropToFill = true;
    options.useDisplayCache = true;
    rendered =
        ImageRender::create(renderer, row->previewPath).render(previewX, previewY, previewW, previewH, options);
  }

  if (rendered) {
    renderer.rectangle.render(previewX - 1, previewY - 1, previewW + 2, previewH + 2, true);
  } else {
    renderer.rectangle.render(previewX, previewY, previewW, previewH, true);
    const char* msg = hasImages ? "No preview" : "No sleep images";
    const int msgFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
    const int msgW = renderer.text.getWidth(msgFont, msg);
    renderer.text.render(msgFont, previewX + (previewW - msgW) / 2,
                         previewY + (previewH - renderer.text.getLineHeight(msgFont)) / 2, msg, true);
  }

  renderer.rectangle.fill(0, 0, pageWidth, 24, false);
  if (hasImages) {
    char countText[8];
    std::snprintf(countText, sizeof(countText), "%d/%d", localSelectedIndex + 1, rowCount);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID,
                         pageWidth - renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, countText) - 8,
                         6, countText, true);
  }

  const int buttonW = 178;
  const int buttonH = 28;
  const int buttonX = (pageWidth - buttonW) / 2;
  const int buttonY = std::min(pageHeight - 52, previewY + previewH + 16);
  renderer.rectangle.fill(buttonX, buttonY, buttonW, buttonH, false);
  renderer.rectangle.render(buttonX, buttonY, buttonW, buttonH, true);
  const char* buttonText = localRandomEnabled ? "Random: On" : "Random: Off";
  const int buttonTextW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, buttonText);
  const int buttonTextX = buttonX + (buttonW - buttonTextW) / 2;
  const int buttonTextY =
      buttonY + (buttonH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, buttonTextX, buttonTextY, buttonText, true,
                       EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Random", "Next");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void SleepImagePickerActivity::applySelection() {
  if (randomEnabled) {
    SETTINGS.setSleepCustomBmpFromInput("");
    SETTINGS.saveToFile();
    onBack();
    return;
  }
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(rows.size())) {
    return;
  }
  const std::string& v = rows[static_cast<size_t>(selectedIndex)].value;
  SETTINGS.setSleepCustomBmpFromInput(v.c_str());
  SETTINGS.saveToFile();
  onBack();
}

void SleepImagePickerActivity::requestRedraw() {
  if (!rows.empty()) {
    if (selectedIndex < 0) {
      selectedIndex = 0;
    } else if (selectedIndex >= static_cast<int>(rows.size())) {
      selectedIndex = static_cast<int>(rows.size()) - 1;
    }
  } else {
    selectedIndex = 0;
  }
  updateRequired = true;
}

void SleepImagePickerActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (updateRequired) {
    updateRequired = false;
    render();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    randomEnabled = false;
    applySelection();
    return;
  }

  bool needRedraw = false;

  if (mappedInput.wasPressed(MenuNav::tabPrev())) {
    randomEnabled = !randomEnabled;
    SETTINGS.setSleepCustomBmpFromInput(randomEnabled ? "" : (rows.empty() ? "" : rows[static_cast<size_t>(selectedIndex)].value.c_str()));
    SETTINGS.saveToFile();
    needRedraw = true;
  } else if (!rows.empty() &&
             (mappedInput.wasPressed(MenuNav::itemPrev()) ||
              mappedInput.wasPressed(MenuNav::itemNext()) ||
              mappedInput.wasPressed(MenuNav::tabNext()))) {
    const int count = static_cast<int>(rows.size());
    if (mappedInput.wasPressed(MenuNav::itemPrev())) {
      selectedIndex = (selectedIndex + count - 1) % count;
    } else {
      selectedIndex = (selectedIndex + 1) % count;
    }
    needRedraw = true;
  }

  if (needRedraw) {
    requestRedraw();
  }
}
