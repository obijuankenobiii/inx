#include "FontManager.h"

#include <system/font/all.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>

#include "ExternalFont.h"
#include "SDCardManager.h"
#include "system/Fonts.h"

// Static member initialization
std::vector<FontManager::SDFontEntry> FontManager::g_sdFonts;
int FontManager::g_nextSDFontId = FontManager::SD_FONT_START_ID;
GfxRenderer* FontManager::g_renderer = nullptr;
FontManager::ProgressCallback FontManager::g_progressCallback = nullptr;

std::vector<std::unique_ptr<EpdFontFamily>> FontManager::g_fontFamilyStorage;
std::vector<std::unique_ptr<EpdFont>> FontManager::g_fontStorage;

int FontManager::g_maxLoadedFonts = 1;  // Maximum 1 font loaded at once
int FontManager::g_loadedFontCount = 0;
bool FontManager::g_scannedForFonts = false;

/**
 * @brief Extracts font size from filename
 */
static int extractSizeFromFilename(const std::string& filename) {
  for (size_t i = 0; i < filename.length(); i++) {
    if (isdigit(filename[i])) {
      std::string numStr;
      while (i < filename.length() && isdigit(filename[i])) {
        numStr += filename[i];
        i++;
      }
      return std::stoi(numStr);
    }
  }
  return 0;
}

/**
 * @brief Extracts font style from filename
 */
static std::string extractStyleFromFilename(const std::string& filename) {
  std::string lowerFilename = filename;
  std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);

  if (lowerFilename.find("bolditalic") != std::string::npos) return "bolditalic";
  if (lowerFilename.find("bold") != std::string::npos) return "bold";
  if (lowerFilename.find("italic") != std::string::npos) return "italic";
  return "regular";
}

/**
 * @brief Initializes the font manager with built-in fonts
 */
void FontManager::initialize(GfxRenderer& renderer) {
  g_renderer = &renderer;

  // Clear any existing storage vectors to free memory
  g_fontFamilyStorage.clear();
  g_fontStorage.clear();
  g_fontFamilyStorage.shrink_to_fit();
  g_fontStorage.shrink_to_fit();

  // Reset counters
  g_loadedFontCount = 0;
  g_scannedForFonts = false;

  static EpdFont SystemFont8Regular(&SystemFont_8_regular);
  static EpdFontFamily SystemFont8FontFamily(&SystemFont8Regular, nullptr, nullptr, nullptr);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_8_FONT_ID, SystemFont8FontFamily);

  static EpdFont SystemFont10Regular(&SystemFont_10_regular);
  static EpdFont SystemFont10Bold(&SystemFont_10_bold);
  static EpdFont SystemFont10Italic(&SystemFont_10_italic);
  static EpdFont SystemFont10BoldItalic(&SystemFont_10_bolditalic);
  static EpdFontFamily SystemFont10FontFamily(&SystemFont10Regular, &SystemFont10Bold, &SystemFont10Italic,
                                              &SystemFont10BoldItalic);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_10_FONT_ID, SystemFont10FontFamily);

  static EpdFont SystemFont12Regular(&SystemFont_12_regular);
  static EpdFont SystemFont12Bold(&SystemFont_12_bold);
  static EpdFont SystemFont12Italic(&SystemFont_12_italic);
  static EpdFont SystemFont12BoldItalic(&SystemFont_12_bolditalic);
  static EpdFontFamily SystemFont12FontFamily(&SystemFont12Regular, &SystemFont12Bold, &SystemFont12Italic,
                                              &SystemFont12BoldItalic);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_12_FONT_ID, SystemFont12FontFamily);

  static EpdFont SystemFont14Regular(&SystemFont_14_regular);
  static EpdFont SystemFont14Bold(&SystemFont_14_bold);
  static EpdFont SystemFont14Italic(&SystemFont_14_italic);
  static EpdFont SystemFont14BoldItalic(&SystemFont_14_bolditalic);
  static EpdFontFamily SystemFont14FontFamily(&SystemFont14Regular, &SystemFont14Bold, &SystemFont14Italic,
                                              &SystemFont14BoldItalic);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_14_FONT_ID, SystemFont14FontFamily);

  static EpdFont SystemFont16Regular(&SystemFont_16_regular);
  static EpdFont SystemFont16Bold(&SystemFont_16_bold);
  static EpdFont SystemFont16Italic(&SystemFont_16_italic);
  static EpdFont SystemFont16BoldItalic(&SystemFont_16_bolditalic);
  static EpdFontFamily SystemFont16FontFamily(&SystemFont16Regular, &SystemFont16Bold, &SystemFont16Italic,
                                              &SystemFont16BoldItalic);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_16_FONT_ID, SystemFont16FontFamily);

  static EpdFont SystemFont18Regular(&SystemFont_18_regular);
  static EpdFont SystemFont18Bold(&SystemFont_18_bold);
  static EpdFont SystemFont18Italic(&SystemFont_18_italic);
  static EpdFont SystemFont18BoldItalic(&SystemFont_18_bolditalic);
  static EpdFontFamily SystemFont18FontFamily(&SystemFont18Regular, &SystemFont18Bold, &SystemFont18Italic,
                                              &SystemFont18BoldItalic);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_18_FONT_ID, SystemFont18FontFamily);

  Serial.println("[FontManager] Initialized with built-in fonts");
  printMemoryUsage();
}

/**
 * @brief Gets the next font ID in sequence
 */
int FontManager::getNextFont(int currentFontId) {
  static const std::unordered_map<int, int> NEXT_FONT = {
      {ATKINSON_HYPERLEGIBLE_8_FONT_ID, ATKINSON_HYPERLEGIBLE_10_FONT_ID},
      {ATKINSON_HYPERLEGIBLE_10_FONT_ID, ATKINSON_HYPERLEGIBLE_12_FONT_ID},
      {ATKINSON_HYPERLEGIBLE_12_FONT_ID, ATKINSON_HYPERLEGIBLE_14_FONT_ID},
      {ATKINSON_HYPERLEGIBLE_14_FONT_ID, ATKINSON_HYPERLEGIBLE_16_FONT_ID},
      {ATKINSON_HYPERLEGIBLE_16_FONT_ID, ATKINSON_HYPERLEGIBLE_18_FONT_ID},
      {ATKINSON_HYPERLEGIBLE_18_FONT_ID, ATKINSON_HYPERLEGIBLE_18_FONT_ID}};

  auto it = NEXT_FONT.find(currentFontId);
  if (it != NEXT_FONT.end()) {
    return it->second;
  }

  for (const auto& entry : g_sdFonts) {
    if (entry.id == currentFontId) {
      int nextId = currentFontId;
      for (const auto& e : g_sdFonts) {
        if (e.family == entry.family && e.size > entry.size) {
          if (nextId == currentFontId || e.size < nextId) {
            nextId = e.id;
          }
        }
      }
      return nextId;
    }
  }

  return currentFontId;
}

/**
 * @brief Gets the previous font ID in sequence
 */
int FontManager::getPreviousFont(int currentFontId) {
  static const std::unordered_map<int, int> PREV_FONT = {
      {ATKINSON_HYPERLEGIBLE_8_FONT_ID, ATKINSON_HYPERLEGIBLE_8_FONT_ID},
      {ATKINSON_HYPERLEGIBLE_10_FONT_ID, ATKINSON_HYPERLEGIBLE_8_FONT_ID},
      {ATKINSON_HYPERLEGIBLE_12_FONT_ID, ATKINSON_HYPERLEGIBLE_10_FONT_ID},
      {ATKINSON_HYPERLEGIBLE_14_FONT_ID, ATKINSON_HYPERLEGIBLE_12_FONT_ID},
      {ATKINSON_HYPERLEGIBLE_16_FONT_ID, ATKINSON_HYPERLEGIBLE_14_FONT_ID},
      {ATKINSON_HYPERLEGIBLE_18_FONT_ID, ATKINSON_HYPERLEGIBLE_16_FONT_ID}};

  auto it = PREV_FONT.find(currentFontId);
  if (it != PREV_FONT.end()) {
    return it->second;
  }

  for (const auto& entry : g_sdFonts) {
    if (entry.id == currentFontId) {
      int prevId = currentFontId;
      for (const auto& e : g_sdFonts) {
        if (e.family == entry.family && e.size < entry.size) {
          if (prevId == currentFontId || e.size > prevId) {
            prevId = e.id;
          }
        }
      }
      return prevId;
    }
  }

  return currentFontId;
}

/**
 * @brief Scans SD card for font files
 */
bool FontManager::scanSDFonts(const char* sdPath, bool forceRescan) {
  if (!forceRescan && g_scannedForFonts) {
    Serial.println("[FontManager] Fonts already scanned, use forceRescan to rescan");
    return true;
  }

  if (!SdMan.ready()) {
    Serial.println("[FontManager] SD Card not ready");
    return false;
  }

  if (!SdMan.exists(sdPath)) {
    SdMan.mkdir(sdPath);
    return false;
  }

  // Clear existing entries to free memory
  g_sdFonts.clear();
  g_sdFonts.shrink_to_fit();
  g_nextSDFontId = FontManager::SD_FONT_START_ID;

  auto root = SdMan.open(sdPath);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return false;
  }

  std::vector<std::string> families;
  char name[128];

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    std::string itemName = name;

    if (itemName.substr(0, 2) == "._") {
      file.close();
      continue;
    }

    if (!file.isDirectory()) {
      file.close();
      continue;
    }

    families.push_back(itemName);
    file.close();
  }
  root.close();

  struct FontGroup {
    std::string family;
    int size;
    std::string regularPath;
    std::string boldPath;
    std::string italicPath;
    std::string boldItalicPath;
  };
  std::map<std::pair<std::string, int>, FontGroup> groups;

  for (const auto& family : families) {
    std::string familyPath = std::string(sdPath) + "/" + family;
    auto familyDir = SdMan.open(familyPath.c_str());

    if (!familyDir || !familyDir.isDirectory()) {
      if (familyDir) familyDir.close();
      continue;
    }

    for (auto file = familyDir.openNextFile(); file; file = familyDir.openNextFile()) {
      file.getName(name, sizeof(name));
      std::string filename = name;

      if (filename.substr(0, 2) == "._") {
        file.close();
        continue;
      }

      if (!file.isDirectory() && filename.length() > 4 && filename.substr(filename.length() - 4) == ".bin") {
        int size = extractSizeFromFilename(filename);
        if (size > 0) {
          auto key = std::make_pair(family, size);
          std::string fullPath = familyPath + "/" + filename;
          std::string style = extractStyleFromFilename(filename);

          if (style == "regular") {
            groups[key].regularPath = fullPath;
          } else if (style == "bold") {
            groups[key].boldPath = fullPath;
          } else if (style == "italic") {
            groups[key].italicPath = fullPath;
          } else if (style == "bolditalic") {
            groups[key].boldItalicPath = fullPath;
          }
          groups[key].family = family;
          groups[key].size = size;
        }
      }
      file.close();
    }
    familyDir.close();
  }

  for (auto& group : groups) {
    SDFontEntry entry;
    entry.id = g_nextSDFontId++;
    entry.family = group.second.family;
    entry.size = group.second.size;
    entry.regularPath = group.second.regularPath;
    entry.boldPath = group.second.boldPath;
    entry.italicPath = group.second.italicPath;
    entry.boldItalicPath = group.second.boldItalicPath;
    entry.regularFont = nullptr;
    entry.boldFont = nullptr;
    entry.italicFont = nullptr;
    entry.boldItalic = nullptr;
    entry.fontFamily = nullptr;
    entry.isLoaded = false;
    entry.lastUsed = 0;
    g_sdFonts.push_back(entry);

    Serial.printf("[FontManager] Found font: %s %dpt (ID: %d)\n", entry.family.c_str(), entry.size, entry.id);
  }

  g_scannedForFonts = true;
  Serial.printf("[FontManager] Scanned %d font families, found %d font sizes\n", (int)families.size(),
                (int)g_sdFonts.size());
  printMemoryUsage();
  return true;
}

/**
 * @brief Cleans up font data for an entry
 */
void FontManager::cleanupFontData(SDFontEntry* entry) {
  if (!entry) return;

  entry->regularFont = nullptr;
  entry->boldFont = nullptr;
  entry->italicFont = nullptr;
  entry->boldItalic = nullptr;
  entry->fontFamily = nullptr;
  entry->isLoaded = false;
}

/**
 * @brief Unloads the least recently used font
 */
void FontManager::unloadLRUFont() {
  uint32_t oldestTime = UINT32_MAX;
  int oldestId = -1;

  for (auto& entry : g_sdFonts) {
    if (entry.isLoaded && entry.lastUsed < oldestTime) {
      oldestTime = entry.lastUsed;
      oldestId = entry.id;
    }
  }

  if (oldestId != -1) {
    Serial.printf("[FontManager] Unloading LRU font ID: %d\n", oldestId);
    unloadFont(oldestId);
  }
}

/**
 * @brief Updates LRU timestamp for a font
 */
void FontManager::updateFontLRU(int fontId) {
  for (auto& entry : g_sdFonts) {
    if (entry.id == fontId) {
      entry.lastUsed = millis();
      break;
    }
  }
}

/**
 * @brief Gets free heap memory
 */
size_t FontManager::getFreeHeap() { return ESP.getFreeHeap(); }

/**
 * @brief Sets maximum number of fonts to keep loaded
 */
void FontManager::setMaxLoadedFonts(int maxFonts) {
  g_maxLoadedFonts = maxFonts;
  Serial.printf("[FontManager] Max loaded fonts set to %d\n", maxFonts);
}

/**
 * @brief Gets maximum number of fonts that can be loaded
 */
int FontManager::getMaxLoadedFonts() { return g_maxLoadedFonts; }

/**
 * @brief Gets current number of loaded fonts
 */
int FontManager::getLoadedFontCount() { return g_loadedFontCount; }

/**
 * @brief Loads a specific font from SD card by ID
 * This version uses the streaming ExternalFont that doesn't load metadata to RAM
 */
bool FontManager::loadFontFromSD(int fontId, GfxRenderer& renderer) {
  Serial.printf("[FontManager] Loading font ID: %d\n", fontId);

  // Check memory before loading
  if (getFreeHeap() < 40000) {  // Less than 40KB free
    Serial.printf("[FontManager] Low memory (%u bytes), cannot load font\n", getFreeHeap());
    return false;
  }

  SDFontEntry* entry = nullptr;
  for (auto& e : g_sdFonts) {
    if (e.id == fontId) {
      entry = &e;
      break;
    }
  }

  if (!entry) {
    Serial.printf("[FontManager] Error: Font ID %d not found\n", fontId);
    return false;
  }

  if (entry->isLoaded) {
    updateFontLRU(fontId);
    return true;
  }

  // Check if we need to unload a font to make room
  if (g_loadedFontCount >= g_maxLoadedFonts) {
    unloadLRUFont();
  }

  // Load ONLY the regular font (bold/italic use same font with faux styling)
  if (entry->regularPath.empty() || !SdMan.exists(entry->regularPath.c_str())) {
    Serial.printf("[FontManager] Regular font not found for ID %d\n", fontId);
    return false;
  }

  // Create streaming font (metadata NOT loaded to RAM - just stores offsets)
  std::unique_ptr<ExternalFont> regStream(new ExternalFont());
  if (!regStream->load(entry->regularPath.c_str())) {
    Serial.printf("[FontManager] Failed to load regular font: %s\n", entry->regularPath.c_str());
    return false;
  }

  // Create EpdFont from the streaming data
  // Note: The EpdFont will have null glyph/intervals pointers
  // The renderer will call ExternalFont methods to get glyph data on demand
  EpdFont* regularFont = new EpdFont(regStream->getData());
  g_fontStorage.push_back(std::unique_ptr<EpdFont>(regularFont));

  // Use the SAME font for all styles (renderer applies faux bold/italic)
  entry->regularFont = regularFont;
  entry->boldFont = regularFont;    // Same font - renderer does faux bold
  entry->italicFont = regularFont;  // Same font - renderer does faux italic
  entry->boldItalic = regularFont;  // Same font - renderer does faux bold+italic

  // Create the Font Family
  EpdFontFamily* fontFamily =
      new EpdFontFamily(entry->regularFont, entry->boldFont, entry->italicFont, entry->boldItalic);
  g_fontFamilyStorage.push_back(std::unique_ptr<EpdFontFamily>(fontFamily));
  entry->fontFamily = fontFamily;
  entry->isLoaded = true;
  entry->lastUsed = millis();
  g_loadedFontCount++;

  // Register the streaming font with the renderer
  renderer.insertStreamingFont(entry->id, std::move(regStream), *fontFamily);

  Serial.printf("[FontManager] Registered streaming font: %s %dpt (ID: %d) - RAM: ~%d bytes\n", 
                entry->family.c_str(), entry->size, entry->id, sizeof(ExternalFont));
  printMemoryUsage();
  return true;
}

/**
 * @brief Ensures a font is ready for use, loading it if necessary
 */
bool FontManager::ensureFontReady(int fontId, GfxRenderer& renderer) {
  Serial.printf("[FontManager] ensureFontReady called for ID %d\n", fontId);

  if (fontId >= ATKINSON_HYPERLEGIBLE_8_FONT_ID && fontId <= ATKINSON_HYPERLEGIBLE_18_FONT_ID) {
    return true;
  }

  for (auto& entry : g_sdFonts) {
    if (entry.id == fontId) {
      if (!entry.isLoaded) {
        return loadFontFromSD(fontId, renderer);
      }
      updateFontLRU(fontId);
      return true;
    }
  }

  Serial.printf("[FontManager] Font ID %d not found!\n", fontId);
  return false;
}

/**
 * @brief Unloads a font from memory
 */
bool FontManager::unloadFont(int fontId) {
  Serial.printf("[FontManager] Unloading font ID: %d\n", fontId);

  for (auto& entry : g_sdFonts) {
    if (entry.id == fontId && entry.isLoaded) {
      // Remove from renderer first
      if (g_renderer != nullptr) {
        g_renderer->removeFont(fontId);
      }

      cleanupFontData(&entry);
      g_loadedFontCount--;

      Serial.printf("[FontManager] Font ID %d unloaded successfully\n", fontId);
      printMemoryUsage();
      return true;
    }
  }

  return false;
}

/**
 * @brief Unloads all SD card fonts and frees memory
 */
void FontManager::unloadAllSDFonts() {
  Serial.println("[FontManager] Unloading all SD streaming fonts");

  if (g_renderer != nullptr) {
    g_renderer->removeAllStreamingFonts();
  }

  // Clear the storage vectors to free memory
  g_fontFamilyStorage.clear();
  g_fontStorage.clear();

  // Force vector memory deallocation
  g_fontFamilyStorage.shrink_to_fit();
  g_fontStorage.shrink_to_fit();

  // Mark all SD fonts as unloaded
  for (auto& entry : g_sdFonts) {
    cleanupFontData(&entry);
  }

  g_loadedFontCount = 0;

  Serial.println("[FontManager] All SD fonts unloaded");
  printMemoryUsage();
}

/**
 * @brief Gets information about a specific font
 */
const FontManager::FontInfo* FontManager::getFontInfo(int fontId) {
  static FontInfo info;

  switch (fontId) {
    case ATKINSON_HYPERLEGIBLE_8_FONT_ID:
      info = {"Atkinson Hyperlegible 8", "Atkinson Hyperlegible", fontId, 8, true};
      return &info;
    case ATKINSON_HYPERLEGIBLE_10_FONT_ID:
      info = {"Atkinson Hyperlegible 10", "Atkinson Hyperlegible", fontId, 10, true};
      return &info;
    case ATKINSON_HYPERLEGIBLE_12_FONT_ID:
      info = {"Atkinson Hyperlegible 12", "Atkinson Hyperlegible", fontId, 12, true};
      return &info;
    case ATKINSON_HYPERLEGIBLE_14_FONT_ID:
      info = {"Atkinson Hyperlegible 14", "Atkinson Hyperlegible", fontId, 14, true};
      return &info;
    case ATKINSON_HYPERLEGIBLE_16_FONT_ID:
      info = {"Atkinson Hyperlegible 16", "Atkinson Hyperlegible", fontId, 16, true};
      return &info;
    case ATKINSON_HYPERLEGIBLE_18_FONT_ID:
      info = {"Atkinson Hyperlegible 18", "Atkinson Hyperlegible", fontId, 18, true};
      return &info;
    default:
      for (const auto& entry : g_sdFonts) {
        if (entry.id == fontId) {
          info = {entry.family + " " + std::to_string(entry.size), entry.family, fontId, entry.size, false};
          return &info;
        }
      }
      return nullptr;
  }
}

/**
 * @brief Gets all available fonts
 */
std::vector<FontManager::FontInfo> FontManager::getAllAvailableFonts() {
  std::vector<FontInfo> fonts;

  fonts.push_back({"Atkinson Hyperlegible 8", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_8_FONT_ID, 8, true});
  fonts.push_back({"Atkinson Hyperlegible 10", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_10_FONT_ID, 10, true});
  fonts.push_back({"Atkinson Hyperlegible 12", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_12_FONT_ID, 12, true});
  fonts.push_back({"Atkinson Hyperlegible 14", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_14_FONT_ID, 14, true});
  fonts.push_back({"Atkinson Hyperlegible 16", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_16_FONT_ID, 16, true});
  fonts.push_back({"Atkinson Hyperlegible 18", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_18_FONT_ID, 18, true});

  for (const auto& entry : g_sdFonts) {
    fonts.push_back({entry.family + " " + std::to_string(entry.size), entry.family, entry.id, entry.size, false});
  }

  return fonts;
}

/**
 * @brief Gets all fonts belonging to a specific family
 */
std::vector<FontManager::FontInfo> FontManager::getFontsByFamily(const std::string& family) {
  std::vector<FontInfo> result;

  if (family == "Atkinson Hyperlegible") {
    result.push_back({"Atkinson Hyperlegible 8", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_8_FONT_ID, 8, true});
    result.push_back({"Atkinson Hyperlegible 10", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_10_FONT_ID, 10, true});
    result.push_back({"Atkinson Hyperlegible 12", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_12_FONT_ID, 12, true});
    result.push_back({"Atkinson Hyperlegible 14", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_14_FONT_ID, 14, true});
    result.push_back({"Atkinson Hyperlegible 16", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_16_FONT_ID, 16, true});
    result.push_back({"Atkinson Hyperlegible 18", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_18_FONT_ID, 18, true});
  }

  for (const auto& entry : g_sdFonts) {
    if (entry.family == family) {
      result.push_back({entry.family + " " + std::to_string(entry.size), entry.family, entry.id, entry.size, false});
    }
  }

  std::sort(result.begin(), result.end(), [](const FontInfo& a, const FontInfo& b) { return a.size < b.size; });
  return result;
}

/**
 * @brief Gets all available font families
 */
std::vector<std::string> FontManager::getAllFamilies() {
  std::vector<std::string> families;
  families.push_back("Atkinson Hyperlegible");

  for (const auto& entry : g_sdFonts) {
    if (std::find(families.begin(), families.end(), entry.family) == families.end()) {
      families.push_back(entry.family);
    }
  }
  return families;
}

/**
 * @brief Checks if a specific font is loaded
 */
bool FontManager::isFontLoaded(int fontId) {
  if (fontId >= ATKINSON_HYPERLEGIBLE_8_FONT_ID && fontId <= ATKINSON_HYPERLEGIBLE_18_FONT_ID) {
    return true;
  }

  for (const auto& entry : g_sdFonts) {
    if (entry.id == fontId) {
      return entry.isLoaded;
    }
  }
  return false;
}

/**
 * @brief Sets the progress callback for font operations
 */
void FontManager::setProgressCallback(ProgressCallback callback) { g_progressCallback = callback; }

/**
 * @brief Prints font manager statistics to serial output
 */
void FontManager::printFontStats() {
  Serial.println("=== Font Manager Stats ===");
  Serial.printf("Built-in fonts: 6\n");
  Serial.printf("SD fonts discovered: %d\n", (int)g_sdFonts.size());

  int loadedCount = 0;
  for (const auto& entry : g_sdFonts) {
    if (entry.isLoaded) loadedCount++;
  }
  Serial.printf("SD fonts loaded: %d (max: %d)\n", loadedCount, g_maxLoadedFonts);
  Serial.printf("Permanent font storage size: %d fonts, %d families\n", (int)g_fontStorage.size(),
                (int)g_fontFamilyStorage.size());

  Serial.println("\nSD Font Families:");
  for (const auto& entry : g_sdFonts) {
    Serial.printf("  %s: %dpt %s\n", entry.family.c_str(), entry.size, entry.isLoaded ? "(loaded)" : "");
  }
  Serial.println("========================");
}

/**
 * @brief Prints memory usage statistics
 */
void FontManager::printMemoryUsage() {
  Serial.println("=== Memory Usage ===");
  Serial.printf("Free heap: %u bytes (%u KB)\n", getFreeHeap(), getFreeHeap() / 1024);
  Serial.printf("Largest free block: %u bytes\n", ESP.getMaxAllocHeap());
  Serial.printf("Font families loaded: %d\n", (int)g_fontFamilyStorage.size());
  Serial.printf("Fonts loaded: %d\n", (int)g_fontStorage.size());
  Serial.printf("SD font entries: %d\n", (int)g_sdFonts.size());
  Serial.println("===================");
}

/**
 * @brief Gets font ID for a specific family and size
 */
int FontManager::getFontId(const std::string& family, int size) {
  if (family == "Atkinson Hyperlegible") {
    switch (size) {
      case 8:
        return ATKINSON_HYPERLEGIBLE_8_FONT_ID;
      case 10:
        return ATKINSON_HYPERLEGIBLE_10_FONT_ID;
      case 12:
        return ATKINSON_HYPERLEGIBLE_12_FONT_ID;
      case 14:
        return ATKINSON_HYPERLEGIBLE_14_FONT_ID;
      case 16:
        return ATKINSON_HYPERLEGIBLE_16_FONT_ID;
      case 18:
        return ATKINSON_HYPERLEGIBLE_18_FONT_ID;
      default:
        return ATKINSON_HYPERLEGIBLE_12_FONT_ID;
    }
  }

  for (const auto& entry : g_sdFonts) {
    if (entry.family == family && entry.size == size) {
      return entry.id;
    }
  }

  return ATKINSON_HYPERLEGIBLE_12_FONT_ID;
}