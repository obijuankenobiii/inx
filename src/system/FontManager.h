#pragma once
#include <GfxRenderer.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>

class FontManager {
public:
    struct FontInfo {
        std::string name;
        std::string family;
        int id;
        int size;
        bool isBuiltin;
    };
    
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
     * Gets the previous smaller font size in the same family.
     * 
     * @param currentFontId The current font ID
     * @return Font ID for the previous smaller size, or current if already smallest
     */
    static int getPreviousFont(int currentFontId);
    
    // SD Card font management
    /**
     * Scan SD card for .bin font files in /fonts/{family}/ directory
     * @param sdPath Path to fonts directory (default: "/fonts")
     * @return true if scan completed successfully
     */
    static bool scanSDFonts(const char* sdPath = "/fonts");
    
    /**
     * Lazy load a font from SD card when needed
     * @param fontId The font ID to load
     * @return true if font loaded successfully
     */
    static bool loadFontFromSD(int fontId, GfxRenderer& renderer);

    /**
     * Ensure font is ready to use (loads from SD if needed)
     * @param fontId The font ID to check/load
     * @return true if font is ready
     */
    static bool ensureFontReady(int fontId, GfxRenderer& renderer);
    
    /**
     * Unload a font from memory to free resources
     * @param fontId The font ID to unload
     * @return true if unloaded successfully
     */
    static bool unloadFont(int fontId);
    
    /**
     * Unload all SD card fonts from memory
     */
    static void unloadAllSDFonts();
    
    // Font lookup
    /**
     * Get information about a font by ID
     * @param fontId The font ID
     * @return Pointer to FontInfo or nullptr if not found
     */
    static const FontInfo* getFontInfo(int fontId);
    
    /**
     * Get all available fonts (built-in + discovered SD fonts)
     * @return Vector of all font info
     */
    static std::vector<FontInfo> getAllAvailableFonts();
    
    /**
     * Get fonts by family name
     * @param family Family name (e.g., "Roboto", "Atkinson Hyperlegible")
     * @return Vector of fonts in that family
     */
    static std::vector<FontInfo> getFontsByFamily(const std::string& family);
    
    /**
     * Get all available font families
     * @return Vector of family names
     */
    static std::vector<std::string> getAllFamilies();
    
    /**
     * Check if a font is loaded in memory
     * @param fontId The font ID
     * @return true if loaded
     */
    static bool isFontLoaded(int fontId);
    
    // Progress callback for batch operations
    using ProgressCallback = std::function<void(const std::string& family, int current, int total)>;
    static void setProgressCallback(ProgressCallback callback);
    
    // Debug
    static void printFontStats();
    
    // Public constants
    static constexpr int SD_FONT_START_ID = 1000;

    static int getFontId(const std::string& family, int size);
    
private:
    FontManager() = delete;
};