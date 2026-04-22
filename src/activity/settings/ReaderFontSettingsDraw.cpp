/**
 * @file ReaderFontSettingsDraw.cpp
 * @brief Shared reader font preview drawing (system settings + book settings drawer).
 */

#include "ReaderFontSettingsDraw.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <EpdFontFamily.h>

#include "state/SystemSetting.h"

#define SETTINGS SystemSetting::getInstance()

namespace {

void drawCheckboxCheckWithPolygons(GfxRenderer& renderer, int cbX, int cbY, int kCb, bool ink) {
  const int oX = cbX;
  const int oY = cbY;
  const int shortLegX[] = {oX + 2, oX + 8, oX + 5};
  const int shortLegY[] = {oY + kCb / 2, oY + kCb - 2, oY + kCb - 2};
  renderer.drawPolygon(shortLegX, shortLegY, 3, ink);

  const int longLegX[] = {oX + 5, oX + 9, oX + kCb - 2, oX + kCb - 5};
  const int longLegY[] = {oY + kCb - 2, oY + kCb - 5, oY + 3, oY + 5};
  renderer.drawPolygon(longLegX, longLegY, 4, ink);
}

/** Filled circle (octagon) for the slider thumb. */
void drawSliderThumb(GfxRenderer& renderer, int cx, int cy, bool ink) {
  constexpr int kR = 5;
  constexpr int n = 8;
  int xs[n];
  int ys[n];
  for (int i = 0; i < n; i++) {
    const float a = static_cast<float>(i) * (6.2831853f / static_cast<float>(n));
    xs[i] = cx + static_cast<int>(std::lround(static_cast<float>(kR) * std::cos(a)));
    ys[i] = cy + static_cast<int>(std::lround(static_cast<float>(kR) * std::sin(a)));
  }
  renderer.fillPolygon(xs, ys, n, ink);
}

}  

namespace ReaderFontSettingsDraw {

void drawFontFamilyRowValue(GfxRenderer& renderer, uint8_t fontFamily, int valueColumnRight, int itemY, int itemHeight,
                            bool rowSelected, const char* familyLabel) {
  if (!familyLabel || familyLabel[0] == '\0') {
    return;
  }
  const int previewFont = SETTINGS.getReaderFontIdForFamilyAndSize(fontFamily, SystemSetting::EXTRA_SMALL);
  const bool black = !rowSelected;
  const int valW = renderer.getTextWidth(previewFont, familyLabel, EpdFontFamily::REGULAR);
  const int lh = renderer.getLineHeight(previewFont);
  const int valY = itemY + (itemHeight - lh) / 2;
  const int valX = valueColumnRight - valW;
  renderer.drawText(previewFont, valX, valY, familyLabel, black, EpdFontFamily::REGULAR);
}

void drawFontSizeSliderRowValue(GfxRenderer& renderer, uint8_t fontFamily, uint8_t fontSizeIndex, int valueAreaLeft,
                                int valueAreaRight, int itemY, int itemHeight, bool rowSelected) {
  const bool ink = !rowSelected;
  const uint8_t fam = std::min<uint8_t>(fontFamily, SystemSetting::FONT_FAMILY_COUNT - 1);
  const uint8_t sel = std::min<uint8_t>(fontSizeIndex, SystemSetting::FONT_SIZE_COUNT - 1);
  constexpr int kN = SystemSetting::FONT_SIZE_COUNT;

  const int fidSel = SETTINGS.getReaderFontIdForFamilyAndSize(fam, sel);
  const int fidMin = SETTINGS.getReaderFontIdForFamilyAndSize(fam, SystemSetting::EXTRA_SMALL);
  const int fidMax = SETTINGS.getReaderFontIdForFamilyAndSize(fam, SystemSetting::EXTRA_LARGE);

  const int trackY = itemY + itemHeight - 9;
  const int maxPreviewW = std::max(24, valueAreaRight - valueAreaLeft - 8);

  const std::string loremShown =
      renderer.truncatedText(fidSel, "Lorem", maxPreviewW, EpdFontFamily::REGULAR);
  const int loremW = renderer.getTextWidth(fidSel, loremShown.c_str(), EpdFontFamily::REGULAR);
  const int loremLh = renderer.getLineHeight(fidSel);
  int loremY = itemY + 4;
  if (loremY + loremLh > trackY - 5) {
    loremY = std::max(itemY + 2, trackY - 5 - loremLh);
  }
  const int innerRight = valueAreaRight - 4;
  int loremX = (valueAreaLeft + innerRight - loremW) / 2;
  if (loremX < valueAreaLeft) {
    loremX = valueAreaLeft;
  }
  if (loremX + loremW > innerRight) {
    loremX = std::max(valueAreaLeft, innerRight - loremW);
  }
  renderer.drawText(fidSel, loremX, loremY, loremShown.c_str(), ink, EpdFontFamily::REGULAR);

  const int ascMin = renderer.getFontAscenderSize(fidMin);
  const int ascMax = renderer.getFontAscenderSize(fidMax);
  const int wSmall = renderer.getTextWidth(fidMin, "a", EpdFontFamily::REGULAR);
  const int wLarge = renderer.getTextWidth(fidMax, "a", EpdFontFamily::REGULAR);

  int xL = valueAreaLeft;
  int xR = valueAreaRight - 4;
  if (xR - xL < wSmall + wLarge + 40) {
    xL = std::max(0, xR - (wSmall + wLarge + 40));
  }

  const int ySmall = trackY - ascMin;
  const int yLarge = trackY - ascMax;
  renderer.drawText(fidMin, xL, ySmall, "a", ink, EpdFontFamily::REGULAR);
  renderer.drawText(fidMax, xR - wLarge, yLarge, "a", ink, EpdFontFamily::REGULAR);

  constexpr int kTrackEndPad = 18;
  const int trackX0 = xL + wSmall + kTrackEndPad;
  const int trackX1 = xR - wLarge - kTrackEndPad;
  if (trackX1 > trackX0) {
    constexpr int kLineThick = 2;
    const int lineW = trackX1 - trackX0 + 1;
    const int lineTop = trackY - kLineThick / 2;
    renderer.fillRect(trackX0, lineTop, lineW, kLineThick, ink);
    const float t = (kN <= 1) ? 0.f : static_cast<float>(sel) / static_cast<float>(kN - 1);
    const int thumbCx = trackX0 + static_cast<int>(std::lround(static_cast<float>(trackX1 - trackX0) * t));
    drawSliderThumb(renderer, thumbCx, trackY, ink);
  }
}

void drawToggleCheckbox(GfxRenderer& renderer, int valueColumnRight, int itemY, int itemHeight, bool rowSelected,
                        bool checked) {
  constexpr int kCb = 16;
  const int cbX = valueColumnRight - kCb;
  const int cbY = itemY + (itemHeight - kCb) / 2;
  const bool ink = !rowSelected;
  renderer.drawRect(cbX, cbY, kCb, kCb, ink, false);
  if (checked) {
    drawCheckboxCheckWithPolygons(renderer, cbX, cbY, kCb, ink);
  }
}

}  
