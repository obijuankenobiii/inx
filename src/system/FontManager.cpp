/**
 * @file FontManager.cpp
 * @brief Definitions for FontManager.
 */

#include "FontManager.h"
#include "system/Fonts.h"

void FontManager::initialize(GfxRenderer& renderer) {
    
    static EpdFont bookerly10RegularFont(&bookerly_10_regular);
    static EpdFont bookerly10BoldFont(&bookerly_10_bold);
    static EpdFont bookerly10ItalicFont(&bookerly_10_italic);
    static EpdFont bookerly10BoldItalicFont(&bookerly_10_bolditalic);
    static EpdFontFamily bookerly10FontFamily(&bookerly10RegularFont, 
                                               &bookerly10BoldFont, 
                                               &bookerly10ItalicFont,
                                               &bookerly10BoldItalicFont);

    static EpdFont bookerly12RegularFont(&bookerly_12_regular);
    static EpdFont bookerly12BoldFont(&bookerly_12_bold);
    static EpdFont bookerly12ItalicFont(&bookerly_12_italic);
    static EpdFont bookerly12BoldItalicFont(&bookerly_12_bolditalic);
    static EpdFontFamily bookerly12FontFamily(&bookerly12RegularFont, 
                                               &bookerly12BoldFont, 
                                               &bookerly12ItalicFont,
                                               &bookerly12BoldItalicFont);

    static EpdFont bookerly14RegularFont(&bookerly_14_regular);
    static EpdFont bookerly14BoldFont(&bookerly_14_bold);
    static EpdFont bookerly14ItalicFont(&bookerly_14_italic);
    static EpdFont bookerly14BoldItalicFont(&bookerly_14_bolditalic);
    static EpdFontFamily bookerly14FontFamily(&bookerly14RegularFont, 
                                               &bookerly14BoldFont, 
                                               &bookerly14ItalicFont,
                                               &bookerly14BoldItalicFont);

    static EpdFont bookerly16RegularFont(&bookerly_16_regular);
    static EpdFont bookerly16BoldFont(&bookerly_16_bold);
    static EpdFont bookerly16ItalicFont(&bookerly_16_italic);
    static EpdFont bookerly16BoldItalicFont(&bookerly_16_bolditalic);
    static EpdFontFamily bookerly16FontFamily(&bookerly16RegularFont, 
                                               &bookerly16BoldFont, 
                                               &bookerly16ItalicFont,
                                               &bookerly16BoldItalicFont);

    static EpdFont bookerly18RegularFont(&bookerly_18_regular);
    static EpdFont bookerly18BoldFont(&bookerly_18_bold);
    static EpdFont bookerly18ItalicFont(&bookerly_18_italic);
    static EpdFont bookerly18BoldItalicFont(&bookerly_18_bolditalic);
    static EpdFontFamily bookerly18FontFamily(&bookerly18RegularFont, 
                                               &bookerly18BoldFont, 
                                               &bookerly18ItalicFont,
                                               &bookerly18BoldItalicFont);

    
    static EpdFont literata10RegularFont(&literata_10_regular);
    static EpdFont literata10BoldFont(&literata_10_bold);
    static EpdFont literata10ItalicFont(&literata_10_italic);
    static EpdFont literata10BoldItalicFont(&literata_10_bolditalic);
    static EpdFontFamily literata10RegularFontFamily(&literata10RegularFont, 
                                                      &literata10BoldFont, 
                                                      &literata10ItalicFont,
                                                      &literata10BoldItalicFont);

    static EpdFont literata12RegularFont(&literata_12_regular);
    static EpdFont literata12BoldFont(&literata_12_bold);
    static EpdFont literata12ItalicFont(&literata_12_italic);
    static EpdFont literata12BoldItalicFont(&literata_12_bolditalic);
    static EpdFontFamily literata12RegularFontFamily(&literata12RegularFont, 
                                                      &literata12BoldFont, 
                                                      &literata12ItalicFont,
                                                      &literata12BoldItalicFont);

    static EpdFont literata14RegularFont(&literata_14_regular);
    static EpdFont literata14BoldFont(&literata_14_bold);
    static EpdFont literata14ItalicFont(&literata_14_italic);
    static EpdFont literata14BoldItalicFont(&literata_14_bolditalic);
    static EpdFontFamily literata14RegularFontFamily(&literata14RegularFont, 
                                                      &literata14BoldFont, 
                                                      &literata14ItalicFont,
                                                      &literata14BoldItalicFont);

    static EpdFont literata16RegularFont(&literata_16_regular);
    static EpdFont literata16BoldFont(&literata_16_bold);
    static EpdFont literata16ItalicFont(&literata_16_italic);
    static EpdFont literata16BoldItalicFont(&literata_16_bolditalic);
    static EpdFontFamily literata16RegularFontFamily(&literata16RegularFont, 
                                                      &literata16BoldFont, 
                                                      &literata16ItalicFont,
                                                      &literata16BoldItalicFont);

    static EpdFont literata18RegularFont(&literata_18_regular);
    static EpdFont literata18BoldFont(&literata_18_bold);
    static EpdFont literata18ItalicFont(&literata_18_italic);
    static EpdFont literata18BoldItalicFont(&literata_18_bolditalic);
    static EpdFontFamily literata18RegularFontFamily(&literata18RegularFont, 
                                                      &literata18BoldFont, 
                                                      &literata18ItalicFont,
                                                      &literata18BoldItalicFont);

    
    static EpdFont atkinson_hyperlegible8RegularFont(&atkinson_hyperlegible_8_regular);
    static EpdFontFamily atkinson_hyperlegible8FontFamily(&atkinson_hyperlegible8RegularFont, 
                                                           nullptr, 
                                                           nullptr, 
                                                           nullptr);

    static EpdFont atkinson_hyperlegible10RegularFont(&atkinson_hyperlegible_10_regular);
    static EpdFont atkinson_hyperlegible10BoldFont(&atkinson_hyperlegible_10_bold);
    static EpdFont atkinson_hyperlegible10ItalicFont(&atkinson_hyperlegible_10_italic);
    static EpdFont atkinson_hyperlegible10BoldItalicFont(&atkinson_hyperlegible_10_bolditalic);
    static EpdFontFamily atkinson_hyperlegible10FontFamily(&atkinson_hyperlegible10RegularFont, 
                                                            &atkinson_hyperlegible10BoldFont, 
                                                            &atkinson_hyperlegible10ItalicFont, 
                                                            &atkinson_hyperlegible10BoldItalicFont);

    static EpdFont atkinson_hyperlegible12RegularFont(&atkinson_hyperlegible_12_regular);
    static EpdFont atkinson_hyperlegible12BoldFont(&atkinson_hyperlegible_12_bold);
    static EpdFont atkinson_hyperlegible12ItalicFont(&atkinson_hyperlegible_12_italic);
    static EpdFont atkinson_hyperlegible12BoldItalicFont(&atkinson_hyperlegible_12_bolditalic);
    static EpdFontFamily atkinson_hyperlegible12FontFamily(&atkinson_hyperlegible12RegularFont, 
                                                            &atkinson_hyperlegible12BoldFont, 
                                                            &atkinson_hyperlegible12ItalicFont, 
                                                            &atkinson_hyperlegible12BoldItalicFont);

    static EpdFont atkinson_hyperlegible14RegularFont(&atkinson_hyperlegible_14_regular);
    static EpdFont atkinson_hyperlegible14BoldFont(&atkinson_hyperlegible_14_bold);
    static EpdFont atkinson_hyperlegible14ItalicFont(&atkinson_hyperlegible_14_italic);
    static EpdFont atkinson_hyperlegible14BoldItalicFont(&atkinson_hyperlegible_14_bolditalic);
    static EpdFontFamily atkinson_hyperlegible14FontFamily(&atkinson_hyperlegible14RegularFont, 
                                                            &atkinson_hyperlegible14BoldFont, 
                                                            &atkinson_hyperlegible14ItalicFont, 
                                                            &atkinson_hyperlegible14BoldItalicFont);

    static EpdFont atkinson_hyperlegible16RegularFont(&atkinson_hyperlegible_16_regular);
    static EpdFont atkinson_hyperlegible16BoldFont(&atkinson_hyperlegible_16_bold);
    static EpdFont atkinson_hyperlegible16ItalicFont(&atkinson_hyperlegible_16_italic);
    static EpdFont atkinson_hyperlegible16BoldItalicFont(&atkinson_hyperlegible_16_bolditalic);
    static EpdFontFamily atkinson_hyperlegible16FontFamily(&atkinson_hyperlegible16RegularFont, 
                                                            &atkinson_hyperlegible16BoldFont, 
                                                            &atkinson_hyperlegible16ItalicFont, 
                                                            &atkinson_hyperlegible16BoldItalicFont);

    static EpdFont atkinson_hyperlegible18RegularFont(&atkinson_hyperlegible_18_regular);
    static EpdFont atkinson_hyperlegible18BoldFont(&atkinson_hyperlegible_18_bold);
    static EpdFont atkinson_hyperlegible18ItalicFont(&atkinson_hyperlegible_18_italic);
    static EpdFont atkinson_hyperlegible18BoldItalicFont(&atkinson_hyperlegible_18_bolditalic);
    static EpdFontFamily atkinson_hyperlegible18FontFamily(&atkinson_hyperlegible18RegularFont, 
                                                            &atkinson_hyperlegible18BoldFont, 
                                                            &atkinson_hyperlegible18ItalicFont, 
                                                            &atkinson_hyperlegible18BoldItalicFont);

    renderer.insertFont(BOOKERLY_10_FONT_ID, bookerly10FontFamily);
    renderer.insertFont(BOOKERLY_12_FONT_ID, bookerly12FontFamily);
    renderer.insertFont(BOOKERLY_14_FONT_ID, bookerly14FontFamily);
    renderer.insertFont(BOOKERLY_16_FONT_ID, bookerly16FontFamily);
    renderer.insertFont(BOOKERLY_18_FONT_ID, bookerly18FontFamily);
    
    renderer.insertFont(LITERATA_10_FONT_ID, literata10RegularFontFamily);
    renderer.insertFont(LITERATA_12_FONT_ID, literata12RegularFontFamily);
    renderer.insertFont(LITERATA_14_FONT_ID, literata14RegularFontFamily);
    renderer.insertFont(LITERATA_16_FONT_ID, literata16RegularFontFamily);
    renderer.insertFont(LITERATA_18_FONT_ID, literata18RegularFontFamily);
    
    renderer.insertFont(ATKINSON_HYPERLEGIBLE_8_FONT_ID,  atkinson_hyperlegible8FontFamily);
    renderer.insertFont(ATKINSON_HYPERLEGIBLE_10_FONT_ID, atkinson_hyperlegible10FontFamily);
    renderer.insertFont(ATKINSON_HYPERLEGIBLE_12_FONT_ID, atkinson_hyperlegible12FontFamily);
    renderer.insertFont(ATKINSON_HYPERLEGIBLE_14_FONT_ID, atkinson_hyperlegible14FontFamily);
    renderer.insertFont(ATKINSON_HYPERLEGIBLE_16_FONT_ID, atkinson_hyperlegible16FontFamily);
    renderer.insertFont(ATKINSON_HYPERLEGIBLE_18_FONT_ID, atkinson_hyperlegible18FontFamily);
}

int FontManager::getNextFont(int currentFontId) {
    static const std::unordered_map<int, int> NEXT_FONT = {
        {BOOKERLY_10_FONT_ID, BOOKERLY_12_FONT_ID},
        {BOOKERLY_12_FONT_ID, BOOKERLY_14_FONT_ID},
        {BOOKERLY_14_FONT_ID, BOOKERLY_16_FONT_ID},
        {BOOKERLY_16_FONT_ID, BOOKERLY_18_FONT_ID},
        {BOOKERLY_18_FONT_ID, BOOKERLY_18_FONT_ID},
        
        {LITERATA_10_FONT_ID, LITERATA_12_FONT_ID},
        {LITERATA_12_FONT_ID, LITERATA_14_FONT_ID},
        {LITERATA_14_FONT_ID, LITERATA_16_FONT_ID},
        {LITERATA_16_FONT_ID, LITERATA_18_FONT_ID},
        {LITERATA_18_FONT_ID, LITERATA_18_FONT_ID},
        
        {ATKINSON_HYPERLEGIBLE_8_FONT_ID, ATKINSON_HYPERLEGIBLE_10_FONT_ID},
        {ATKINSON_HYPERLEGIBLE_10_FONT_ID, ATKINSON_HYPERLEGIBLE_12_FONT_ID},
        {ATKINSON_HYPERLEGIBLE_12_FONT_ID, ATKINSON_HYPERLEGIBLE_14_FONT_ID},
        {ATKINSON_HYPERLEGIBLE_14_FONT_ID, ATKINSON_HYPERLEGIBLE_16_FONT_ID},
        {ATKINSON_HYPERLEGIBLE_16_FONT_ID, ATKINSON_HYPERLEGIBLE_18_FONT_ID},
        {ATKINSON_HYPERLEGIBLE_18_FONT_ID, ATKINSON_HYPERLEGIBLE_18_FONT_ID}
    };
    
    auto it = NEXT_FONT.find(currentFontId);
    return (it != NEXT_FONT.end()) ? it->second : currentFontId;
}

int FontManager::getMaxFontId(int currentFontId) {
    if (currentFontId >= 1000 && currentFontId <= 1004) {
        return BOOKERLY_18_FONT_ID;
    } 
    if (currentFontId >= 2001 && currentFontId <= 2006) {
        return ATKINSON_HYPERLEGIBLE_18_FONT_ID;
    }
    if (currentFontId >= 3001 && currentFontId <= 3004) {
        return LITERATA_18_FONT_ID;
    }
    
    return currentFontId;
}