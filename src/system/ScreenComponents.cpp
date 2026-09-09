/**
 * @file ScreenComponents.cpp
 * @brief Definitions for ScreenComponents.
 */

#include "system/ScreenComponents.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "images/Battery.h"
#include "images/Charging.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"

extern HalGPIO gpio;

namespace {

bool formatMenuClock(char* out, const size_t outSize) {
  if (!out || outSize == 0 || !gpio.deviceIsX3() || !SETTINGS.showMenuClock) {
    return false;
  }

  HalGPIO::DateTime dt;
  if (!gpio.readDateTime(dt)) {
    return false;
  }

  uint8_t hour = dt.hour;
  if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_12_HOUR) {
    const char* meridiem = hour >= 12 ? "PM" : "AM";
    hour %= 12;
    if (hour == 0) {
      hour = 12;
    }
    std::snprintf(out, outSize, "%02u:%02u %s", hour, dt.minute, meridiem);
    return true;
  }

  std::snprintf(out, outSize, "%02u:%02u", hour, dt.minute);
  return true;
}

}  // namespace

void ScreenComponents::drawBattery(const GfxRenderer& renderer, const int left, const int top,
                                   const bool showPercentage) {
#ifdef SIMULATOR
  const uint16_t percentage = 100;
  const bool charging = false;
#else
  const uint16_t percentage = gpio.getBatteryPercentage();
  const bool charging = gpio.isUsbConnected();
#endif
  const auto percentageText = showPercentage ? std::to_string(percentage) + "%" : "";
  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, left + BATTERY_ICON_WIDTH + BATTERY_TEXT_GAP, top,
                       percentageText.c_str());
  renderer.bitmap.icon(BatteryIcon, left, top + BATTERY_ICON_TOP_OFFSET, BATTERY_ICON_WIDTH, BATTERY_ICON_HEIGHT);

  const int bars = percentage >= 75 ? 4 : percentage >= 50 ? 3 : percentage >= 25 ? 2 : 1;
  constexpr int barHeight = 6;
  constexpr int firstBarX = 4;
  constexpr int fillWidth = 17;
  const int barY = top + BATTERY_ICON_TOP_OFFSET + 4;
  const int filledWidth = (fillWidth * bars + 3) / 4;
  renderer.rectangle.fill(left + firstBarX, barY, filledWidth, barHeight, true);

  if (charging) {
    constexpr int chargingIconSize = 40;
    constexpr int chargingIconGap = 30;
    renderer.bitmap.icon(Charging, left - chargingIconGap,
                         top + BATTERY_ICON_TOP_OFFSET + (BATTERY_ICON_HEIGHT - chargingIconSize) / 2,
                         chargingIconSize, chargingIconSize);
  }
}

bool ScreenComponents::drawMenuClock(const GfxRenderer& renderer, const int left, const int top) {
  char clockText[10] = {};
  if (!formatMenuClock(clockText, sizeof(clockText))) {
    return false;
  }

  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, left, top, clockText, true);
  return true;
}

void ScreenComponents::drawMenuClockAndBattery(const GfxRenderer& renderer, const int batteryLeft, const int top,
                                               const bool showBatteryPercentage) {
  constexpr int kClockBatteryGap = 12;

  char clockText[10] = {};
  if (formatMenuClock(clockText, sizeof(clockText))) {
    const int clockFont = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
    const int clockW = renderer.text.getWidth(clockFont, clockText);
    renderer.text.render(clockFont, batteryLeft - kClockBatteryGap - clockW, top, clockText, true);
  }

  drawBattery(renderer, batteryLeft, top, showBatteryPercentage);
}

ScreenComponents::PopupLayout ScreenComponents::drawPopup(const GfxRenderer& renderer, const char* message) {
  // Compact bottom status chip (not a center modal): leaves most of the page readable and matches
  // how e-reader firmware usually reports short blocking work.
  constexpr int marginX = 14;
  constexpr int marginY = 10;
  constexpr int bottomGap = 18;
  constexpr int fontId = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int maxLabelW = std::max(8, screenW - 2 * marginX - 24);
  const std::string msgShown = renderer.text.truncate(fontId, message ? message : "", maxLabelW);
  const int textWidth = renderer.text.getWidth(fontId, msgShown.c_str());
  const int textHeight = renderer.text.getLineHeight(fontId);
  const int w = std::min(screenW - 8, textWidth + marginX * 2);
  const int h = textHeight + marginY * 2;
  const int x = (screenW - w) / 2;
  const int y = std::max(0, screenH - h - bottomGap);

  renderer.rectangle.fill(x - 1, y - 1, w + 2, h + 2, true, true);
  renderer.rectangle.fill(x, y, w, h, false, true);
  renderer.rectangle.render(x, y, w, h, true, true);

  const int textX = x + (w - textWidth) / 2;
  const int textY = y + marginY - 1;
  renderer.text.render(fontId, textX, textY, msgShown.c_str(), true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  return {x, y, w, h};
}

void ScreenComponents::fillPopupProgress(const GfxRenderer& renderer, const PopupLayout& layout, const int progress) {
  // Thin progress underline under the bottom status chip.
  constexpr int barHeight = 3;
  const int barWidth = std::max(8, layout.width - 16);
  const int barX = layout.x + (layout.width - barWidth) / 2;
  const int barY = layout.y + layout.height - 6;

  const int clamped = std::max(0, std::min(100, progress));
  const int fillWidth = barWidth * clamped / 100;

  renderer.rectangle.fill(barX, barY, barWidth, barHeight, false);
  if (fillWidth > 0) {
    renderer.rectangle.fill(barX, barY, fillWidth, barHeight, true);
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

namespace {

constexpr int kLoadProgSideMargin = 16;
constexpr int kLoadProgInnerPad = 10;
constexpr int kLoadProgBarH = 6;
constexpr int kLoadProgGapLabelToBar = 6;

void paintLoadingProgressBarRow(const GfxRenderer& renderer, const ScreenComponents::LoadingProgressLayout& L,
                                const int progressPercent0to100) {
  const int clamped = std::max(0, std::min(100, progressPercent0to100));
  const int innerW = std::max(1, L.barW - 2);
  const int fillW = innerW * clamped / 100;

  // Light panel: empty track is white, fill + outline are ink.
  renderer.rectangle.fill(L.barX + 1, L.barY + 1, innerW, L.barH - 2, false);
  if (fillW > 0) {
    renderer.rectangle.fill(L.barX + 1, L.barY + 1, fillW, L.barH - 2, true);
  }
  renderer.rectangle.render(L.barX, L.barY, L.barW, L.barH, true);
}

}  // namespace

ScreenComponents::LoadingProgressLayout ScreenComponents::LoadingProgress::show(const GfxRenderer& renderer,
                                                                                const char* message,
                                                                                const int progressPercent0to100) {
  // Bottom status strip with a slim progress bar — same visual language as drawPopup, so layout
  // updates ("Updating layout") don't plant a heavy center modal over the page mid-reflow.
  const int clamped = std::max(0, std::min(100, progressPercent0to100));
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  constexpr int labelFontId = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
  const int lhLabel = renderer.text.getLineHeight(labelFontId);

  constexpr int kMinBarW = 80;
  constexpr int kBottomGap = 16;
  const int labelMaxForMeasure = std::max(8, screenW - 2 * kLoadProgSideMargin - 2 * kLoadProgInnerPad);
  const std::string msgShown = renderer.text.truncate(labelFontId, message ? message : "", labelMaxForMeasure);
  const int labelW = renderer.text.getWidth(labelFontId, msgShown.c_str());

  const int innerContentW = std::max(labelW, kMinBarW);
  const int panelW = std::min(screenW - 8, std::max(innerContentW + 2 * kLoadProgInnerPad, screenW / 2));
  const int panelX = (screenW - panelW) / 2;
  const int innerW = panelW - 2 * kLoadProgInnerPad;
  const int barW = std::max(kMinBarW, innerW);
  const int panelH = kLoadProgInnerPad + lhLabel + kLoadProgGapLabelToBar + kLoadProgBarH + kLoadProgInnerPad;
  const int panelY = std::max(0, screenH - panelH - kBottomGap);
  const int labelX = panelX + (panelW - labelW) / 2;
  const int labelY = panelY + kLoadProgInnerPad;
  const int barX = panelX + kLoadProgInnerPad;
  const int barY = labelY + lhLabel + kLoadProgGapLabelToBar;

  renderer.rectangle.fill(panelX - 1, panelY - 1, panelW + 2, panelH + 2, true, true);
  renderer.rectangle.fill(panelX, panelY, panelW, panelH, false, true);
  renderer.rectangle.render(panelX, panelY, panelW, panelH, true, true);
  renderer.text.render(labelFontId, labelX, labelY, msgShown.c_str(), true);

  LoadingProgressLayout L;
  L.panelX = panelX;
  L.panelY = panelY;
  L.panelW = panelW;
  L.panelH = panelH;
  L.barX = barX;
  L.barY = barY;
  L.barW = barW;
  L.barH = kLoadProgBarH;

  paintLoadingProgressBarRow(renderer, L, clamped);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  return L;
}

void ScreenComponents::LoadingProgress::setProgress(const GfxRenderer& renderer, const LoadingProgressLayout& layout,
                                                    const int progressPercent0to100) {
  paintLoadingProgressBarRow(renderer, layout, progressPercent0to100);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void ScreenComponents::drawBookProgressBar(const GfxRenderer& renderer, const size_t bookProgress) {
  int vieweableMarginTop, vieweableMarginRight, vieweableMarginBottom, vieweableMarginLeft;
  renderer.getOrientedViewableTRBL(&vieweableMarginTop, &vieweableMarginRight, &vieweableMarginBottom,
                                   &vieweableMarginLeft);

  const int progressBarMaxWidth = renderer.getScreenWidth() - vieweableMarginLeft - vieweableMarginRight;
  const int progressBarY = renderer.getScreenHeight() - vieweableMarginBottom - BOOK_PROGRESS_BAR_HEIGHT;
  const int barWidth = progressBarMaxWidth * bookProgress / 100;
  renderer.rectangle.fill(vieweableMarginLeft, progressBarY, barWidth, BOOK_PROGRESS_BAR_HEIGHT, true);
}

int ScreenComponents::drawTabBar(const GfxRenderer& renderer, const int y, const std::vector<TabInfo>& tabs) {
  constexpr int tabPadding = 20;
  constexpr int leftMargin = 20;
  constexpr int underlineHeight = 2;
  constexpr int underlineGap = 4;

  const int lineHeight = renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID);
  const int tabBarHeight = lineHeight + underlineGap + underlineHeight;

  int currentX = leftMargin;

  for (const auto& tab : tabs) {
    const int textWidth = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, tab.label,
                                                 tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, currentX, y, tab.label, true,
                         tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    if (tab.selected) {
      renderer.rectangle.fill(currentX, y + lineHeight + underlineGap, textWidth, underlineHeight);
    }

    currentX += textWidth + tabPadding;
  }

  return tabBarHeight;
}

void ScreenComponents::drawScrollIndicator(const GfxRenderer& renderer, const int currentPage, const int totalPages,
                                           const int contentTop, const int contentHeight) {
  if (totalPages <= 1) {
    return;
  }

  const int screenWidth = renderer.getScreenWidth();
  constexpr int indicatorWidth = 20;
  constexpr int arrowSize = 6;
  constexpr int margin = 15;

  const int centerX = screenWidth - indicatorWidth / 2 - margin;
  const int indicatorTop = contentTop + 60;
  const int indicatorBottom = contentTop + contentHeight - 30;

  for (int i = 0; i < arrowSize; ++i) {
    const int lineWidth = 1 + i * 2;
    const int startX = centerX - i;
    renderer.line.render(startX, indicatorTop + i, startX + lineWidth - 1, indicatorTop + i);
  }

  for (int i = 0; i < arrowSize; ++i) {
    const int lineWidth = 1 + (arrowSize - 1 - i) * 2;
    const int startX = centerX - (arrowSize - 1 - i);
    renderer.line.render(startX, indicatorBottom - arrowSize + 1 + i, startX + lineWidth - 1,
                         indicatorBottom - arrowSize + 1 + i);
  }

  const std::string pageText = std::to_string(currentPage) + "/" + std::to_string(totalPages);
  const int textWidth = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, pageText.c_str());
  const int textX = centerX - textWidth / 2;
  const int textY =
      (indicatorTop + indicatorBottom) / 2 - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID) / 2;

  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, textX, textY, pageText.c_str());
}

void ScreenComponents::drawProgressBar(const GfxRenderer& renderer, const int x, const int y, const int width,
                                       const int height, const size_t current, const size_t total) {
  if (total == 0) {
    return;
  }

  const int percent = static_cast<int>((static_cast<uint64_t>(current) * 100) / total);

  renderer.rectangle.render(x, y, width, height);

  const int fillWidth = (width - 4) * percent / 100;
  if (fillWidth > 0) {
    renderer.rectangle.fill(x + 2, y + 2, fillWidth, height - 4);
  }

  const std::string percentText = std::to_string(percent) + "%";
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, y + height + 15, percentText.c_str());
}
