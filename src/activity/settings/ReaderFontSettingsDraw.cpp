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
#include "system/FontManager.h"

#define SETTINGS SystemSetting::getInstance()

namespace {

void drawCheckboxCheckWithPolygons(const GfxRenderer& renderer, int cbX, int cbY, int kCb, bool ink) {
  const int oX = cbX;
  const int oY = cbY;
  const int shortLegX[] = {oX + 2, oX + 8, oX + 5};
  const int shortLegY[] = {oY + kCb / 2, oY + kCb - 2, oY + kCb - 2};
  renderer.polygon.render(shortLegX, shortLegY, 3, true, ink);

  const int longLegX[] = {oX + 5, oX + 9, oX + kCb - 2, oX + kCb - 5};
  const int longLegY[] = {oY + kCb - 2, oY + kCb - 5, oY + 3, oY + 5};
  renderer.polygon.render(longLegX, longLegY, 4, true, ink);
}

/** Filled circle (octagon) for the slider thumb. */
void drawSliderThumb(const GfxRenderer& renderer, int cx, int cy, bool ink) {
  constexpr int kR = 5;
  constexpr int n = 8;
  int xs[n];
  int ys[n];
  for (int i = 0; i < n; i++) {
    const float a = static_cast<float>(i) * (6.2831853f / static_cast<float>(n));
    xs[i] = cx + static_cast<int>(std::lround(static_cast<float>(kR) * std::cos(a)));
    ys[i] = cy + static_cast<int>(std::lround(static_cast<float>(kR) * std::sin(a)));
  }
  renderer.polygon.render(xs, ys, n, true, ink);
}

}  

namespace ReaderFontSettingsDraw {

void drawFontFamilyRowValue(const GfxRenderer& renderer, uint8_t fontFamily, int valueColumnRight, int itemY,
                            int itemHeight, bool rowSelected, const char* familyLabel) {
  if (!familyLabel || familyLabel[0] == '\0') {
    return;
  }
  const int previewFont = SETTINGS.getReaderFontIdForSettingsUi(fontFamily, SystemSetting::EXTRA_SMALL);
  const bool black = !rowSelected;
  const int valW = renderer.text.getWidth(previewFont, familyLabel, EpdFontFamily::REGULAR);
  const int lh = renderer.text.getLineHeight(previewFont);
  const int valY = itemY + (itemHeight - lh) / 2;
  const int valX = valueColumnRight - valW;
  renderer.text.render(previewFont, valX, valY, familyLabel, black, EpdFontFamily::REGULAR);
}

void drawFontSizeSliderRowValue(const GfxRenderer& renderer, uint8_t fontFamily, uint8_t fontSizeIndex,
                                int valueAreaLeft, int valueAreaRight, int itemY, int itemHeight, bool rowSelected) {
  const bool ink = !rowSelected;
  const uint32_t famCount = FontManager::readerFontFamilyOptionCount();
  const uint8_t fam =
      famCount > 0 ? std::min<uint8_t>(fontFamily, static_cast<uint8_t>(famCount - 1u)) : fontFamily;
  const uint8_t sel = std::min<uint8_t>(fontSizeIndex, SystemSetting::FONT_SIZE_COUNT - 1);
  constexpr int kN = SystemSetting::FONT_SIZE_COUNT;

  const int fidSel = SETTINGS.getReaderFontIdForSettingsUi(fam, sel);
  const int fidMin = SETTINGS.getReaderFontIdForSettingsUi(fam, SystemSetting::EXTRA_SMALL);
  const int fidMax = SETTINGS.getReaderFontIdForSettingsUi(fam, SystemSetting::EXTRA_LARGE);

  const int trackY = itemY + itemHeight - 9;
  const int maxPreviewW = std::max(24, valueAreaRight - valueAreaLeft - 8);

  const std::string loremShown =
      renderer.text.truncate(fidSel, "Lorem", maxPreviewW, EpdFontFamily::REGULAR);
  const int loremW = renderer.text.getWidth(fidSel, loremShown.c_str(), EpdFontFamily::REGULAR);
  const int loremLh = renderer.text.getLineHeight(fidSel);
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
  renderer.text.render(fidSel, loremX, loremY, loremShown.c_str(), ink, EpdFontFamily::REGULAR);

  const int ascMin = renderer.text.getFontAscenderSize(fidMin);
  const int ascMax = renderer.text.getFontAscenderSize(fidMax);
  const int wSmall = renderer.text.getWidth(fidMin, "a", EpdFontFamily::REGULAR);
  const int wLarge = renderer.text.getWidth(fidMax, "a", EpdFontFamily::REGULAR);

  int xL = valueAreaLeft;
  int xR = valueAreaRight - 4;
  if (xR - xL < wSmall + wLarge + 40) {
    xL = std::max(0, xR - (wSmall + wLarge + 40));
  }

  const int ySmall = trackY - ascMin;
  const int yLarge = trackY - ascMax;
  renderer.text.render(fidMin, xL, ySmall, "a", ink, EpdFontFamily::REGULAR);
  renderer.text.render(fidMax, xR - wLarge, yLarge, "a", ink, EpdFontFamily::REGULAR);

  constexpr int kTrackEndPad = 18;
  const int trackX0 = xL + wSmall + kTrackEndPad;
  const int trackX1 = xR - wLarge - kTrackEndPad;
  if (trackX1 > trackX0) {
    constexpr int kLineThick = 2;
    const int lineW = trackX1 - trackX0 + 1;
    const int lineTop = trackY - kLineThick / 2;
    renderer.rectangle.fill(trackX0, lineTop, lineW, kLineThick, static_cast<int>(ink));
    const float t = (kN <= 1) ? 0.f : static_cast<float>(sel) / static_cast<float>(kN - 1);
    const int thumbCx = trackX0 + static_cast<int>(std::lround(static_cast<float>(trackX1 - trackX0) * t));
    drawSliderThumb(renderer, thumbCx, trackY, ink);
  }
}

void drawToggleCheckbox(const GfxRenderer& renderer, int valueColumnRight, int itemY, int itemHeight, bool rowSelected,
                        bool checked) {
  constexpr int kCb = 16;
  const int cbX = valueColumnRight - kCb;
  const int cbY = itemY + (itemHeight - kCb) / 2;
  const bool ink = !rowSelected;
  renderer.rectangle.render(cbX, cbY, kCb, kCb, ink, false);
  if (checked) {
    drawCheckboxCheckWithPolygons(renderer, cbX, cbY, kCb, ink);
  }
}

}  
