#pragma once

#include <GfxRenderer.h>
#include <cstdint>

namespace ReaderFontSettingsDraw {

/** Right edge of the value column (e.g. screenWidth - 24). */
void drawFontFamilyRowValue(const GfxRenderer& renderer, uint8_t fontFamily, int valueColumnRight, int itemY,
                            int itemHeight, bool rowSelected, const char* familyLabel);

/**
 * Font size preview: small "a" — track + thumb — large "a", with "Lorem" at the selected size above.
 * @param valueAreaLeft / valueAreaRight horizontal bounds for the preview (e.g. mid-screen to right margin).
 */
void drawFontSizeSliderRowValue(const GfxRenderer& renderer, uint8_t fontFamily, uint8_t fontSizeIndex,
                                int valueAreaLeft, int valueAreaRight, int itemY, int itemHeight, bool rowSelected);

/** 16px square + polygon check (same as system Reader settings toggles). */
void drawToggleCheckbox(const GfxRenderer& renderer, int valueColumnRight, int itemY, int itemHeight, bool rowSelected,
                        bool checked);

}  
