/**
 * @file AboutPage.cpp
 * @brief Definitions for AboutPage.
 */

#include "AboutPage.h"

#include <Arduino.h>
#include <cstdio>
#include <esp_heap_caps.h>

#include "system/Fonts.h"
#include "system/SystemMetrics.h"

namespace {
void formatHeapSize(const size_t bytes, char* out, const size_t outSize) {
  if (bytes >= 1024 * 1024) {
    const size_t wholeMb = bytes / (1024 * 1024);
    const size_t tenthMb = ((bytes % (1024 * 1024)) * 10) / (1024 * 1024);
    std::snprintf(out, outSize, "%u.%u MB", static_cast<unsigned>(wholeMb), static_cast<unsigned>(tenthMb));
    return;
  }

  std::snprintf(out, outSize, "%u KB", static_cast<unsigned>((bytes + 1023) / 1024));
}
}  // namespace

AboutPage::AboutPage(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : renderer(renderer),
      mappedInput(mappedInput),
      visible(false),
      dismissed(false),
      lastInputTime(0) {}

AboutPage::~AboutPage() = default;

void AboutPage::show() {
  if (visible) return;
  visible = true;
  dismissed = false;
  renderWithRefresh();
}

void AboutPage::hide() {
  visible = false;
  dismissed = true;
}

void AboutPage::handleInput() {
  if (!visible) return;

  const uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    hide();
    lastInputTime = currentTime;
    return;
  }

}

void AboutPage::render() {
  if (!visible) return;
  renderWithRefresh();
}

void AboutPage::renderWithRefresh() {
  if (!visible) return;

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  const int popupWidth = 400;
  const int popupHeight = 320;
  const int popupX = (screenWidth - popupWidth) / 2;
  const int popupY = (screenHeight - popupHeight) / 2;

  renderer.rectangle.fill(popupX, popupY, popupWidth, popupHeight, false);
  renderer.rectangle.render(popupX, popupY, popupWidth, popupHeight, true);

  int yPos = popupY + 28;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_18_FONT_ID, popupX + 24, yPos, "Inx", true, EpdFontFamily::BOLD);
  yPos += 36;

  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, popupX + 24, yPos, "Current version", true, EpdFontFamily::BOLD);
  yPos += 22;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, popupX + 24, yPos, INX_VERSION, true, EpdFontFamily::REGULAR);
  yPos += 36;

  const size_t totalHeap = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  const size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t usedHeap = totalHeap > freeHeap ? totalHeap - freeHeap : 0;

  char usedBuffer[20];
  char totalBuffer[20];
  char heapLine[52];
  formatHeapSize(usedHeap, usedBuffer, sizeof(usedBuffer));
  formatHeapSize(totalHeap, totalBuffer, sizeof(totalBuffer));
  const unsigned heapPercent = totalHeap > 0 ? static_cast<unsigned>((usedHeap * 100) / totalHeap) : 0;
  std::snprintf(heapLine, sizeof(heapLine), "%s / %s (%u%%)", usedBuffer, totalBuffer, heapPercent);

  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, popupX + 24, yPos, "RAM used / total", true,
                       EpdFontFamily::BOLD);
  yPos += 22;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, popupX + 24, yPos, heapLine, true,
                       EpdFontFamily::REGULAR);
  yPos += 36;

  const unsigned cpuUsage = SystemMetrics::getCpuUsagePercent();
  char cpuLine[32];
  std::snprintf(cpuLine, sizeof(cpuLine), "%u%% / 100%%", cpuUsage);

  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, popupX + 24, yPos, "CPU use / total", true,
                       EpdFontFamily::BOLD);
  yPos += 22;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, popupX + 24, yPos, cpuLine, true, EpdFontFamily::REGULAR);

  const auto labels = mappedInput.mapLabels("Close", "", "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
