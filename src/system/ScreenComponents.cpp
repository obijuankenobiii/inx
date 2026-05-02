/**
 * @file ScreenComponents.cpp
 * @brief Definitions for ScreenComponents.
 */

#include "system/ScreenComponents.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>

#include "system/Battery.h"
#include "system/Fonts.h"

void ScreenComponents::drawBattery(const GfxRenderer& renderer, const int left, const int top,
                                   const bool showPercentage) {
  
  const uint16_t percentage = battery.readPercentage();
  const auto percentageText = showPercentage ? std::to_string(percentage) + "%" : "";
  renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, left + 20, top, percentageText.c_str());

  
  constexpr int batteryWidth = 15;
  constexpr int batteryHeight = 12;
  const int x = left;
  const int y = top + 6;

  
  renderer.drawLine(x + 1, y, x + batteryWidth - 3, y);
  
  renderer.drawLine(x + 1, y + batteryHeight - 1, x + batteryWidth - 3, y + batteryHeight - 1);
  
  renderer.drawLine(x, y + 1, x, y + batteryHeight - 2);
  
  renderer.drawLine(x + batteryWidth - 2, y + 1, x + batteryWidth - 2, y + batteryHeight - 2);
  renderer.drawPixel(x + batteryWidth - 1, y + 3);
  renderer.drawPixel(x + batteryWidth - 1, y + batteryHeight - 4);
  renderer.drawLine(x + batteryWidth - 0, y + 4, x + batteryWidth - 0, y + batteryHeight - 5);

  
  int filledWidth = percentage * (batteryWidth - 5) / 100 + 1;
  if (filledWidth > batteryWidth - 5) {
    filledWidth = batteryWidth - 5;  
  }

  renderer.fillRect(x + 2, y + 2, filledWidth, batteryHeight - 4);
}

ScreenComponents::PopupLayout ScreenComponents::drawPopup(const GfxRenderer& renderer, const char* message) {
  constexpr int margin = 15;

  const int textWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, message);
  const int textHeight = renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID);
  const int w = textWidth + margin * 2;
  const int h = textHeight + margin * 2;
  constexpr int y = 330;
  const int x = (renderer.getScreenWidth() - w) / 2;

  renderer.fillRect(x - 2, y - 2, w + 4, h + 4, true, true);
  renderer.fillRect(x, y, w, h, true, true);

  const int textX = x + (w - textWidth) / 2;
  const int textY = y + margin - 2;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, textX, textY, message, false);
  renderer.displayBuffer();
  return {x, y, w, h};
}

void ScreenComponents::fillPopupProgress(const GfxRenderer& renderer, const PopupLayout& layout, const int progress) {
  constexpr int barHeight = 4;
  const int barWidth = layout.width - 30;  
  const int barX = layout.x + (layout.width - barWidth) / 2;
  const int barY = layout.y + layout.height - 10;

  const int clamped = std::max(0, std::min(100, progress));
  int fillWidth = barWidth * clamped / 100;

  renderer.fillRect(barX, barY, fillWidth, barHeight, true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

namespace {

constexpr int kLoadProgBottomY = 312;
constexpr int kLoadProgSideMargin = 20;
constexpr int kLoadProgInnerPad = 12;
constexpr int kLoadProgBarH = 10;
constexpr int kLoadProgGapLabelToBar = 10;
constexpr int kLoadProgGapBarToPct = 8;

void paintLoadingProgressBarRow(const GfxRenderer& renderer, const ScreenComponents::LoadingProgressLayout& L,
                                  const int progressPercent0to100) {
  const int clamped = std::max(0, std::min(100, progressPercent0to100));
  const int innerW = std::max(1, L.barW - 2);
  const int fillW = innerW * clamped / 100;

  renderer.fillRect(L.barX + 1, L.barY + 1, innerW, L.barH - 2, false);
  if (fillW > 0) {
    renderer.fillRect(L.barX + 1, L.barY + 1, fillW, L.barH - 2, true);
  }
  renderer.drawRect(L.barX, L.barY, L.barW, L.barH, true);

  char pctBuf[16];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", clamped);
  renderer.drawText(L.pctFontId, L.pctX, L.pctY, pctBuf, false);
}

}  

ScreenComponents::LoadingProgressLayout ScreenComponents::LoadingProgress::show(const GfxRenderer& renderer,
                                                                                const char* message,
                                                                                const int progressPercent0to100) {
  const int clamped = std::max(0, std::min(100, progressPercent0to100));
  const int screenW = renderer.getScreenWidth();
  constexpr int labelFontId = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
  constexpr int pctFontId = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
  const int lhLabel = renderer.getLineHeight(labelFontId);
  const int lhPct = renderer.getLineHeight(pctFontId);

  char pctBuf[16];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", clamped);
  const int pctW = renderer.getTextWidth(pctFontId, pctBuf);

  constexpr int kMinBarW = 40;
  const int labelMaxForMeasure = std::max(8, screenW - 2 * kLoadProgSideMargin - 2 * kLoadProgInnerPad);
  const std::string msgShown = renderer.truncatedText(labelFontId, message ? message : "", labelMaxForMeasure);
  const int labelW = renderer.getTextWidth(labelFontId, msgShown.c_str());

  const int rowInnerW = kMinBarW + kLoadProgGapBarToPct + pctW;
  const int innerContentW = std::max(labelW, rowInnerW);
  const int panelW = std::min(screenW - 4, innerContentW + 2 * kLoadProgInnerPad);
  const int panelX = (screenW - panelW) / 2;
  const int innerW = panelW - 2 * kLoadProgInnerPad;
  const int barW = std::max(kMinBarW, innerW - kLoadProgGapBarToPct - pctW);
  const int pctX = panelX + kLoadProgInnerPad + barW + kLoadProgGapBarToPct;
  const int panelH = kLoadProgInnerPad + lhLabel + kLoadProgGapLabelToBar + kLoadProgBarH + kLoadProgInnerPad;
  const int panelY = kLoadProgBottomY - panelH;
  const int labelX = panelX + (panelW - labelW) / 2;
  const int labelY = panelY + kLoadProgInnerPad;
  const int barX = panelX + kLoadProgInnerPad;
  const int barY = labelY + lhLabel + kLoadProgGapLabelToBar;
  const int pctY = barY + (kLoadProgBarH - lhPct) / 2;

  renderer.fillRect(panelX - 2, panelY - 2, panelW + 4, panelH + 4, true, true);
  renderer.fillRect(panelX, panelY, panelW, panelH, true, true);
  renderer.drawText(labelFontId, labelX, labelY, msgShown.c_str(), false);

  LoadingProgressLayout L;
  L.panelX = panelX;
  L.panelY = panelY;
  L.panelW = panelW;
  L.panelH = panelH;
  L.barX = barX;
  L.barY = barY;
  L.barW = barW;
  L.barH = kLoadProgBarH;
  L.pctX = pctX;
  L.pctY = pctY;
  L.pctFontId = pctFontId;

  paintLoadingProgressBarRow(renderer, L, clamped);
  renderer.displayBuffer();

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
  renderer.fillRect(vieweableMarginLeft, progressBarY, barWidth, BOOK_PROGRESS_BAR_HEIGHT, true);
}

int ScreenComponents::drawTabBar(const GfxRenderer& renderer, const int y, const std::vector<TabInfo>& tabs) {
  constexpr int tabPadding = 20;      
  constexpr int leftMargin = 20;      
  constexpr int underlineHeight = 2;  
  constexpr int underlineGap = 4;     

  const int lineHeight = renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID);
  const int tabBarHeight = lineHeight + underlineGap + underlineHeight;

  int currentX = leftMargin;

  for (const auto& tab : tabs) {
    const int textWidth =
        renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, tab.label, tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    
    renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, currentX, y, tab.label, true,
                      tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    
    if (tab.selected) {
      renderer.fillRect(currentX, y + lineHeight + underlineGap, textWidth, underlineHeight);
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
    renderer.drawLine(startX, indicatorTop + i, startX + lineWidth - 1, indicatorTop + i);
  }

  
  for (int i = 0; i < arrowSize; ++i) {
    const int lineWidth = 1 + (arrowSize - 1 - i) * 2;
    const int startX = centerX - (arrowSize - 1 - i);
    renderer.drawLine(startX, indicatorBottom - arrowSize + 1 + i, startX + lineWidth - 1,
                      indicatorBottom - arrowSize + 1 + i);
  }

  
  const std::string pageText = std::to_string(currentPage) + "/" + std::to_string(totalPages);
  const int textWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, pageText.c_str());
  const int textX = centerX - textWidth / 2;
  const int textY = (indicatorTop + indicatorBottom) / 2 - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID) / 2;

  renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, textX, textY, pageText.c_str());
}

void ScreenComponents::drawProgressBar(const GfxRenderer& renderer, const int x, const int y, const int width,
                                       const int height, const size_t current, const size_t total) {
  if (total == 0) {
    return;
  }

  
  const int percent = static_cast<int>((static_cast<uint64_t>(current) * 100) / total);

  
  renderer.drawRect(x, y, width, height);

  
  const int fillWidth = (width - 4) * percent / 100;
  if (fillWidth > 0) {
    renderer.fillRect(x + 2, y + 2, fillWidth, height - 4);
  }

  
  const std::string percentText = std::to_string(percent) + "%";
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, y + height + 15, percentText.c_str());
}
