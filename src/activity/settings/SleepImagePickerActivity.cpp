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
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "util/StringUtils.h"

namespace {
constexpr int GRID_COLS = 2;
constexpr int GRID_ROWS = 3;
constexpr int GRID_ITEMS = GRID_COLS * GRID_ROWS;
constexpr int GRID_MARGIN_X = 18;
constexpr int GRID_GAP_X = 12;
constexpr int GRID_GAP_Y = 12;
constexpr int GRID_TOP = 32;
constexpr int THUMB_INSET_X = 18;
constexpr int THUMB_INSET_Y = 12;
constexpr int RANDOM_BUTTON_W = 178;
constexpr int RANDOM_BUTTON_H = 28;
constexpr int FOOTER_SIDE_PAD = 20;

}  // namespace

void SleepImagePickerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  freeGridBuffer();
  rebuildRows();

  randomEnabled = SETTINGS.sleepCustomBmp[0] == '\0';
  selectedIndex = 0;
  for (size_t i = 0; i < rows.size(); i++) {
    if (rows[i].value == SETTINGS.sleepCustomBmp) {
      selectedIndex = static_cast<int>(i);
      break;
    }
  }

  renderedPageStart = -1;
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
  renderedPageStart = -1;
  freeGridBuffer();
  std::vector<Row>().swap(rows);
  ActivityWithSubactivity::onExit();
}

int SleepImagePickerActivity::pageStartForIndex(const int index) const {
  if (index <= 0) {
    return 0;
  }
  return (index / GRID_ITEMS) * GRID_ITEMS;
}

int SleepImagePickerActivity::indexForSlot(const int pageStart, const int slot) const { return pageStart + slot; }

int SleepImagePickerActivity::slotForIndex(const int pageStart, const int index) const {
  const int offset = index - pageStart;
  if (offset < 0 || offset >= GRID_ITEMS) {
    return -1;
  }
  return offset;
}

void SleepImagePickerActivity::drawPickerChrome(const int pageStart, const int rowCount, const bool hasImages,
                                                const bool localRandomEnabled, const bool drawCells) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int buttonX = pageWidth - RANDOM_BUTTON_W - FOOTER_SIDE_PAD;
  const int buttonY = pageHeight - 76;
  const int gridBottom = buttonY - 14;
  const int gridHeight = std::max(1, gridBottom - GRID_TOP);
  const int cellW = (pageWidth - GRID_MARGIN_X * 2 - GRID_GAP_X) / GRID_COLS;
  const int cellH = (gridHeight - GRID_GAP_Y * (GRID_ROWS - 1)) / GRID_ROWS;

  renderer.rectangle.fill(0, 0, pageWidth, 26, false);

  if (hasImages && drawCells) {
    for (int slot = 0; slot < GRID_ITEMS; ++slot) {
      const int rowIndex = indexForSlot(pageStart, slot);
      const int col = slot % GRID_COLS;
      const int gridRow = slot / GRID_COLS;
      const int cellX = GRID_MARGIN_X + col * (cellW + GRID_GAP_X);
      const int cellY = GRID_TOP + gridRow * (cellH + GRID_GAP_Y);

      if (rowIndex >= rowCount) {
        continue;
      }

      renderer.rectangle.fill(cellX, cellY, cellW, cellH, false);
      renderer.rectangle.render(cellX, cellY, cellW, cellH, true);
    }
  } else if (!hasImages) {
    const int emptyX = GRID_MARGIN_X;
    const int emptyY = GRID_TOP;
    const int emptyW = pageWidth - GRID_MARGIN_X * 2;
    const int emptyH = gridBottom - GRID_TOP;
    renderer.rectangle.render(emptyX, emptyY, emptyW, emptyH, true);
    const char* msg = "No sleep images";
    const int msgFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
    const int msgW = renderer.text.getWidth(msgFont, msg);
    renderer.text.render(msgFont, emptyX + (emptyW - msgW) / 2,
                         emptyY + (emptyH - renderer.text.getLineHeight(msgFont)) / 2, msg, true);
  }

  if (hasImages) {
    const int totalPages = std::max(1, (rowCount + GRID_ITEMS - 1) / GRID_ITEMS);
    const int currentPage = std::min(totalPages, pageStart / GRID_ITEMS + 1);
    char pageText[16];
    std::snprintf(pageText, sizeof(pageText), "%d - %d", currentPage, totalPages);

    const int pageFont = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
    const int pagePadX = 8;
    const int pageTextW = renderer.text.getWidth(pageFont, pageText);
    const int pageLineH = renderer.text.getLineHeight(pageFont);
    const int pageTagH = pageLineH + 6;
    const int pageTagW = pageTextW + pagePadX * 2;
    const int pageTagX = FOOTER_SIDE_PAD;
    const int pageTagY = buttonY + (RANDOM_BUTTON_H - pageTagH) / 2;
    renderer.rectangle.fill(pageTagX, pageTagY, pageTagW, pageTagH, true, true);
    renderer.text.render(pageFont, pageTagX + pagePadX, pageTagY + (pageTagH - pageLineH) / 2, pageText, false,
                         EpdFontFamily::REGULAR);
  }

  renderer.rectangle.fill(buttonX, buttonY, RANDOM_BUTTON_W, RANDOM_BUTTON_H, false);
  renderer.rectangle.render(buttonX, buttonY, RANDOM_BUTTON_W, RANDOM_BUTTON_H, true);
  const char* buttonText = localRandomEnabled ? "Random: On" : "Random: Off";
  const int buttonTextW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, buttonText);
  const int buttonTextX = buttonX + (RANDOM_BUTTON_W - buttonTextW) / 2;
  const int buttonTextY =
      buttonY + (RANDOM_BUTTON_H - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, buttonTextX, buttonTextY, buttonText, true,
                       EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Random", "Next");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SleepImagePickerActivity::drawPickerThumbnails(const int pageStart, const int rowCount) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int buttonY = pageHeight - 76;
  const int gridBottom = buttonY - 14;
  const int gridHeight = std::max(1, gridBottom - GRID_TOP);
  const int cellW = (pageWidth - GRID_MARGIN_X * 2 - GRID_GAP_X) / GRID_COLS;
  const int cellH = (gridHeight - GRID_GAP_Y * (GRID_ROWS - 1)) / GRID_ROWS;

  for (int slot = 0; slot < GRID_ITEMS; ++slot) {
    const int rowIndex = indexForSlot(pageStart, slot);
    if (rowIndex >= rowCount) {
      continue;
    }

    const int col = slot % GRID_COLS;
    const int gridRow = slot / GRID_COLS;
    const int cellX = GRID_MARGIN_X + col * (cellW + GRID_GAP_X);
    const int cellY = GRID_TOP + gridRow * (cellH + GRID_GAP_Y);

    bool rendered = false;
    const Row& row = rows[static_cast<size_t>(rowIndex)];
    if (!row.previewPath.empty()) {
      ImageRender::Options options;
      options.mode = ImageRenderMode::OneBit;
      options.cropToFill = false;
      options.useDisplayCache = true;
      const int thumbX = cellX + THUMB_INSET_X;
      const int thumbY = cellY + THUMB_INSET_Y;
      const int thumbW = std::max(8, cellW - THUMB_INSET_X * 2);
      const int thumbH = std::max(8, cellH - THUMB_INSET_Y * 2);
      rendered = ImageRender::create(renderer, row.previewPath).render(thumbX, thumbY, thumbW, thumbH, options);
    }

    if (!rendered) {
      const char* msg = "No preview";
      const int msgFont = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
      const int msgW = renderer.text.getWidth(msgFont, msg);
      renderer.text.render(msgFont, cellX + (cellW - msgW) / 2,
                           cellY + (cellH - renderer.text.getLineHeight(msgFont)) / 2, msg, true);
    }

    renderer.rectangle.render(cellX, cellY, cellW, cellH, true);
  }
}

void SleepImagePickerActivity::drawSelectionFrame(const int pageStart, const int rowCount, const int index) {
  if (index < 0 || index >= rowCount) {
    return;
  }
  const int slot = slotForIndex(pageStart, index);
  if (slot < 0) {
    return;
  }
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int buttonY = pageHeight - 76;
  const int gridBottom = buttonY - 14;
  const int gridHeight = std::max(1, gridBottom - GRID_TOP);
  const int cellW = (pageWidth - GRID_MARGIN_X * 2 - GRID_GAP_X) / GRID_COLS;
  const int cellH = (gridHeight - GRID_GAP_Y * (GRID_ROWS - 1)) / GRID_ROWS;
  const int col = slot % GRID_COLS;
  const int gridRow = slot / GRID_COLS;
  const int cellX = GRID_MARGIN_X + col * (cellW + GRID_GAP_X);
  const int cellY = GRID_TOP + gridRow * (cellH + GRID_GAP_Y);
  renderer.rectangle.render(cellX + 1, cellY + 1, cellW - 2, cellH - 2, true);
  renderer.rectangle.render(cellX + 2, cellY + 2, cellW - 4, cellH - 4, true);
}

bool SleepImagePickerActivity::storeGridBuffer(const int pageStart) {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  freeGridBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  gridBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!gridBuffer) {
    return false;
  }

  memcpy(gridBuffer, frameBuffer, bufferSize);
  gridBufferStored = true;
  gridBufferPageStart = pageStart;
  return true;
}

bool SleepImagePickerActivity::restoreGridBuffer(const int pageStart) {
  if (!gridBufferStored || !gridBuffer || gridBufferPageStart != pageStart) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = renderer.getBufferSize();
  memcpy(frameBuffer, gridBuffer, bufferSize);
  return true;
}

void SleepImagePickerActivity::freeGridBuffer() {
  if (gridBuffer) {
    free(gridBuffer);
    gridBuffer = nullptr;
  }
  gridBufferStored = false;
  gridBufferPageStart = -1;
}

void SleepImagePickerActivity::render() {
  const int rowCount = static_cast<int>(rows.size());
  if (selectedIndex < 0) {
    selectedIndex = 0;
  }
  if (rowCount > 0 && selectedIndex >= rowCount) {
    selectedIndex = rowCount - 1;
  }
  const bool hasImages = rowCount > 0;
  const int pageStart = hasImages ? pageStartForIndex(selectedIndex) : 0;
  const bool lazyFirstPass = hasImages && renderedPageStart != pageStart;
  const bool pageNeedsHalfRefresh = hasImages && renderedPageStart != pageStart;

  if (hasImages && restoreGridBuffer(pageStart)) {
    drawPickerChrome(pageStart, rowCount, hasImages, randomEnabled, false);
    drawSelectionFrame(pageStart, rowCount, selectedIndex);
    renderer.displayBuffer(pageNeedsHalfRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    renderedPageStart = pageStart;
    return;
  }

  renderer.clearScreen();
  drawPickerChrome(pageStart, rowCount, hasImages, randomEnabled);
  if (lazyFirstPass || !hasImages) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }

  if (hasImages) {
    renderer.clearScreen();
    drawPickerChrome(pageStart, rowCount, hasImages, randomEnabled);
    drawPickerThumbnails(pageStart, rowCount);
    storeGridBuffer(pageStart);
    drawSelectionFrame(pageStart, rowCount, selectedIndex);
    renderer.displayBuffer(pageNeedsHalfRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    renderedPageStart = pageStart;
  }
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
    return;
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

  const bool randomPressed = mappedInput.wasPressed(MenuNav::tabPrev());
  const bool upPressed = mappedInput.wasPressed(MenuNav::itemPrev());
  const bool downPressed = mappedInput.wasPressed(MenuNav::itemNext());
  const bool nextPressed = mappedInput.wasPressed(MenuNav::tabNext());

  if (randomPressed) {
    randomEnabled = !randomEnabled;
    SETTINGS.setSleepCustomBmpFromInput(
        randomEnabled ? "" : (rows.empty() ? "" : rows[static_cast<size_t>(selectedIndex)].value.c_str()));
    SETTINGS.saveToFile();
    renderedPageStart = -1;
    freeGridBuffer();
    needRedraw = true;
  } else if (!rows.empty() && (upPressed || downPressed || nextPressed)) {
    const int count = static_cast<int>(rows.size());
    if (upPressed) {
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
