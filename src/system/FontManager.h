#pragma once

/**
 * @file FontManager.h
 * @brief Public interface and types for FontManager.
 */

#include <GfxRenderer.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

  /** SD streaming font IDs (must not overlap built-in Literata / Atkinson ranges). */
  static constexpr int SD_FONT_START_ID = 4000;

  static void initialize(GfxRenderer& renderer);

  static int getNextFont(int currentFontId);
  static int getMaxFontId(int currentFontId);

  static bool scanSDFonts(const char* sdPath = "/fonts", bool forceRescan = false);

  /**
   * Reader "Font Family" slot encoding stored in SystemSetting::fontFamily / BookSettings::fontFamily:
   * 0 = Literata, 1 = Atkinson Hyperlegible, 2+ = SD folder names (sorted), see readerFontFamilyOptionCount().
   */
  static uint32_t readerFontFamilyOptionCount();
  static std::vector<std::string> readerFontFamilyEnumLabels();
  static std::string readerFontFamilyLabel(uint8_t slot);
  static void clampReaderFontFamilySlot(uint8_t& slot);
  static int getFontIdNearestPointSize(const std::string& family, int preferredPt);

  static bool loadFontFromSD(int fontId, GfxRenderer& renderer);
  static bool ensureFontReady(int fontId, GfxRenderer& renderer);
  /** Preload body, next-larger, and max-in-family SD slots used together during EPUB layout. */
  static void ensureReaderLayoutFonts(int bodyFontId, GfxRenderer& renderer);
  static bool unloadFont(int fontId);
  static void unloadAllSDFonts();

  /** If any SD font is loaded, unload all, run fn, then reload readerBodyFontId when it is an SD slot. */
  static void withSdFontsReleasedForHeapIntensiveWork(int readerBodyFontId, const std::function<void()>& fn);

  static const FontInfo* getFontInfo(int fontId);
  static std::vector<FontInfo> getAllAvailableFonts();
  static std::vector<FontInfo> getFontsByFamily(const std::string& family);
  static std::vector<std::string> getAllFamilies();
  static bool isFontLoaded(int fontId);
  static int getFontId(const std::string& family, int size);

  using ProgressCallback = std::function<void(const std::string& family, int current, int total)>;
  static void setProgressCallback(ProgressCallback callback);

  static void printMemoryUsage();
  static void printFontStats();
  static size_t getFreeHeap();

  static void setMaxLoadedFonts(int maxFonts);
  static int getMaxLoadedFonts();
  static int getLoadedFontCount();
  static void unloadLRUFont();

 private:
  FontManager() = delete;

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
    uint32_t lastUsed;
  };

  static std::vector<SDFontEntry> g_sdFonts;
  static int g_nextSDFontId;
  static GfxRenderer* g_renderer;
  static ProgressCallback g_progressCallback;
  static std::vector<std::unique_ptr<EpdFontFamily>> g_fontFamilyStorage;
  static std::vector<std::unique_ptr<EpdFont>> g_fontStorage;
  static int g_maxLoadedFonts;
  static int g_loadedFontCount;
  static bool g_scannedForFonts;

  static void updateFontLRU(int fontId);
  static void cleanupFontData(SDFontEntry* entry);
  static void rebuildSdReaderFamilyList();
};
