#pragma once

/**
 * @file FontManager.h
 * @brief Public interface and types for FontManager.
 */

#include <GfxRenderer.h>
#include <unordered_map>

class FontManager {
public:
    /**
     * Initialize the class.
     */
    static void initialize(GfxRenderer& renderer);
    
    /**
     * Gets the next larger font size in the same family.
     * Size progression: ExtraSmall → Small → Medium → Large → ExtraLarge
     * 
     * @param currentFontId The current font ID
     * @return Font ID for the next larger size, or current if already largest
     */
    static int getNextFont(int currentFontId);

    /**
     * Gets the max font size in the same family.
     * Size progression: ExtraSmall → Small → Medium → Large → ExtraLarge
     * 
     * @param currentFontId The current font ID
     * @return Font ID for the next larger size, or current if already largest
     */
    static int getMaxFontId(int currentFontId);
};