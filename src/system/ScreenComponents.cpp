/**
 * @file ScreenComponents.cpp
 * @brief Definitions for ScreenComponents.
 */

#include "system/ScreenComponents.h"

#include <GfxRenderer.h>

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

  int fillWidth = barWidth * progress / 100;

  renderer.fillRect(barX, barY, fillWidth, barHeight, true);

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
