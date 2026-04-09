#pragma once

#include <GfxRenderer.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>
#include <memory>

// Forward declarations
class EpdFont;
class EpdFontFamily;
class ExternalFont;

class FontManager {
public:
    struct FontInfo {
        std::string name;
        std::string family;
        int id;
        int size;
        bool isBuiltin;
    };
    
    static constexpr int SD_FONT_START_ID = 2000;
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
     * @param forceRescan Force rescan even if already scanned
     * @return true if scan completed successfully
     */
    static bool scanSDFonts(const char* sdPath = "/fonts", bool forceRescan = false);
    
    /**
     * Lazy load a font from SD card when needed
     * @param fontId The font ID to load
     * @param renderer Graphics renderer to register with
     * @return true if font loaded successfully
     */
    static bool loadFontFromSD(int fontId, GfxRenderer& renderer);

    /**
     * Ensure font is ready to use (loads from SD if needed)
     * @param fontId The font ID to check/load
     * @param renderer Graphics renderer to use for loading
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
    
    /**
     * Get font ID for a specific family and size
     * @param family Font family name
     * @param size Font size in points
     * @return Font ID, or default font ID if not found
     */
    static int getFontId(const std::string& family, int size);
    
    // Progress callback for batch operations
    using ProgressCallback = std::function<void(const std::string& family, int current, int total)>;
    static void setProgressCallback(ProgressCallback callback);
    
    // Memory management
    /**
     * Print memory usage statistics
     */
    static void printMemoryUsage();
    
    /**
     * Print font manager statistics
     */
    static void printFontStats();
    
    /**
     * Get free heap memory
     * @return Free heap size in bytes
     */
    static size_t getFreeHeap();
    
    /**
     * Set maximum number of fonts to keep loaded simultaneously
     * @param maxFonts Maximum fonts (default: 3)
     */
    static void setMaxLoadedFonts(int maxFonts);
    
    /**
     * Get maximum number of fonts that can be loaded
     * @return Maximum font count
     */
    static int getMaxLoadedFonts();
    
    /**
     * Get current number of loaded fonts
     * @return Loaded font count
     */
    static int getLoadedFontCount();
    
    /**
     * Unload least recently used font (for cache management)
     */
    static void unloadLRUFont();
    
private:
    FontManager() = delete;
    
    // Internal structure for SD font entries
    struct SDFontEntry {
        int id;
        std::string family;
        int size;
        std::string regularPath;
        std::string boldPath;
        std::string italicPath;
        std::string boldItalicPath;
        EpdFont* regularFont;
        EpdFont* boldFont;
        EpdFont* italicFont;
        EpdFont* boldItalic;
        EpdFontFamily* fontFamily;
        bool isLoaded;
        uint32_t lastUsed;  // For LRU tracking
    };
    
    // Static members
    static std::vector<SDFontEntry> g_sdFonts;
    static int g_nextSDFontId;
    static GfxRenderer* g_renderer;
    static ProgressCallback g_progressCallback;
    
    // Storage for loaded fonts
    static std::vector<std::unique_ptr<EpdFontFamily>> g_fontFamilyStorage;
    static std::vector<std::unique_ptr<EpdFont>> g_fontStorage;
    
    // LRU cache management
    static int g_maxLoadedFonts;
    static int g_loadedFontCount;
    static bool g_scannedForFonts;
    
    // Helper functions
    static void updateFontLRU(int fontId);
    static void cleanupFontData(SDFontEntry* entry);
};