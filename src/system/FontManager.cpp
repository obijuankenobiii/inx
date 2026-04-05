#include "FontManager.h"

#include <system/font/all.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>

#include "SDCardManager.h"
#include "ExternalFont.h"
#include "system/Fonts.h"

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
};

static std::vector<SDFontEntry> g_sdFonts;
static int g_nextSDFontId = FontManager::SD_FONT_START_ID;
static GfxRenderer* g_renderer = nullptr;
static FontManager::ProgressCallback g_progressCallback = nullptr;

static std::vector<std::unique_ptr<EpdFontFamily>> g_fontFamilyStorage;
static std::vector<std::unique_ptr<EpdFont>> g_fontStorage;

/**
 * @brief Extracts font size from filename
 * @param filename The font filename
 * @return The font size in points, or 0 if not found
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
 * @param filename The font filename
 * @return The font style (regular, bold, italic, bolditalic)
 */
static std::string extractStyleFromFilename(const std::string& filename) {
  if (filename.find("bolditalic") != std::string::npos) return "bolditalic";
  if (filename.find("bold") != std::string::npos) return "bold";
  if (filename.find("italic") != std::string::npos) return "italic";
  return "regular";
}

/**
 * @brief Loads a binary font file from SD card
 * @param binPath Path to the .bin font file
 * @return Pointer to loaded EpdFont, or nullptr on failure
 */
static EpdFont* loadBinaryFont(const std::string& binPath) {
  // Create streaming font instead of loading everything
  ExternalFont* streamingFont = new ExternalFont();
  if (!streamingFont->load(binPath.c_str())) {
    delete streamingFont;
    return nullptr;
  }

  // Create EpdFont wrapper
  EpdFont* font = new EpdFont(streamingFont->getData());

  // Store streaming font for cleanup
  static std::vector<std::unique_ptr<ExternalFont>> streamingFonts;
  streamingFonts.push_back(std::unique_ptr<ExternalFont>(streamingFont));

  g_fontStorage.push_back(std::unique_ptr<EpdFont>(font));
  return g_fontStorage.back().get();
}

/**
 * @brief Initializes the font manager with built-in fonts
 * @param renderer The graphics renderer to register fonts with
 */
void FontManager::initialize(GfxRenderer& renderer) {
  g_renderer = &renderer;

  static EpdFont SystemFont8Regular(&SystemFont_8_regular);
  static EpdFontFamily SystemFont8FontFamily(&SystemFont8Regular, nullptr, nullptr, nullptr);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_8_FONT_ID, SystemFont8FontFamily);

  static EpdFont SystemFont10Regular(&SystemFont_10_regular);
  static EpdFont SystemFont10Bold(&SystemFont_10_bold);
  static EpdFont SystemFont10Italic(&SystemFont_10_italic);
  static EpdFont SystemFont10BoldItalic(&SystemFont_10_bolditalic);
  static EpdFontFamily SystemFont10FontFamily(
      &SystemFont10Regular, &SystemFont10Bold, &SystemFont10Italic,
      &SystemFont10BoldItalic);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_10_FONT_ID, SystemFont10FontFamily);

  static EpdFont SystemFont12Regular(&SystemFont_12_regular);
  static EpdFont SystemFont12Bold(&SystemFont_12_bold);
  static EpdFont SystemFont12Italic(&SystemFont_12_italic);
  static EpdFont SystemFont12BoldItalic(&SystemFont_12_bolditalic);
  static EpdFontFamily SystemFont12FontFamily(
      &SystemFont12Regular, &SystemFont12Bold, &SystemFont12Italic,
      &SystemFont12BoldItalic);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_12_FONT_ID, SystemFont12FontFamily);

  static EpdFont SystemFont14Regular(&SystemFont_14_regular);
  static EpdFont SystemFont14Bold(&SystemFont_14_bold);
  static EpdFont SystemFont14Italic(&SystemFont_14_italic);
  static EpdFont SystemFont14BoldItalic(&SystemFont_14_bolditalic);
  static EpdFontFamily SystemFont14FontFamily(
      &SystemFont14Regular, &SystemFont14Bold, &SystemFont14Italic,
      &SystemFont14BoldItalic);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_14_FONT_ID, SystemFont14FontFamily);

  static EpdFont SystemFont16Regular(&SystemFont_16_regular);
  static EpdFont SystemFont16Bold(&SystemFont_16_bold);
  static EpdFont SystemFont16Italic(&SystemFont_16_italic);
  static EpdFont SystemFont16BoldItalic(&SystemFont_16_bolditalic);
  static EpdFontFamily SystemFont16FontFamily(
      &SystemFont16Regular, &SystemFont16Bold, &SystemFont16Italic,
      &SystemFont16BoldItalic);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_16_FONT_ID, SystemFont16FontFamily);

  static EpdFont SystemFont18Regular(&SystemFont_18_regular);
  static EpdFont SystemFont18Bold(&SystemFont_18_bold);
  static EpdFont SystemFont18Italic(&SystemFont_18_italic);
  static EpdFont SystemFont18BoldItalic(&SystemFont_18_bolditalic);
  static EpdFontFamily SystemFont18FontFamily(
      &SystemFont18Regular, &SystemFont18Bold, &SystemFont18Italic,
      &SystemFont18BoldItalic);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_18_FONT_ID, SystemFont18FontFamily);
}

/**
 * @brief Gets the next font ID in sequence
 * @param currentFontId Current font ID
 * @return Next font ID in the sequence
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
 * @param currentFontId Current font ID
 * @return Previous font ID in the sequence
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
 * @param sdPath Path to scan for fonts
 * @return true if scan completed successfully, false otherwise
 */
bool FontManager::scanSDFonts(const char* sdPath) {
  if (!SdMan.ready()) {
    Serial.println("SD Card not ready");
    return false;
  }

  if (!SdMan.exists(sdPath)) {
    SdMan.mkdir(sdPath);
    return false;
  }

  g_sdFonts.clear();
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
    g_sdFonts.push_back(entry);

    Serial.printf("Found font: %s %dpt (ID: %d)\n", entry.family.c_str(), entry.size, entry.id);
  }

  Serial.printf("Scanned %d font families, found %d font sizes\n", (int)families.size(), (int)g_sdFonts.size());
  return true;
}

/**
 * @brief Loads a specific font from SD card by ID
 * @param fontId Font ID to load
 * @param renderer Graphics renderer to register the font with
 * @return true if font loaded successfully, false otherwise
 */
/**
 * @brief Loads a specific font from SD card by ID
 * @param fontId Font ID to load
 * @param renderer Graphics renderer to register the font with
 * @return true if font loaded successfully, false otherwise
 */
bool FontManager::loadFontFromSD(int fontId, GfxRenderer& renderer) {
  Serial.printf("=== loadFontFromSD called for ID %d ===\n", fontId);

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

  if (entry->isLoaded) return true;

  // Helper lambda to load a style if the path exists
  auto loadStyleStream = [&](const std::string& path, EpdFont** fontTarget) -> std::unique_ptr<ExternalFont> {
    if (path.empty() || !SdMan.exists(path.c_str())) return nullptr;

    std::unique_ptr<ExternalFont> stream(new ExternalFont());
    if (stream->load(path.c_str())) {
      *fontTarget = new EpdFont(stream->getData());
      g_fontStorage.push_back(std::unique_ptr<EpdFont>(*fontTarget));
      return stream;
    }
    return nullptr;
  };

  // 1. Load Regular (Required)
  std::unique_ptr<ExternalFont> regStream = loadStyleStream(entry->regularPath, &entry->regularFont);
  if (!regStream) {
    Serial.printf("[FontManager] Failed to load regular style for %d\n", fontId);
    return false;
  }

  // 2. Try to load Bold, Italic, and BoldItalic
  std::unique_ptr<ExternalFont> boldStream = loadStyleStream(entry->boldPath, &entry->boldFont);
  std::unique_ptr<ExternalFont> italStream = loadStyleStream(entry->italicPath, &entry->italicFont);
  std::unique_ptr<ExternalFont> bitalStream = loadStyleStream(entry->boldItalicPath, &entry->boldItalic);

  // 3. Fallback logic: if a style is missing, use Regular
  if (!entry->boldFont)       entry->boldFont = entry->regularFont;
  if (!entry->italicFont)     entry->italicFont = entry->regularFont;
  if (!entry->boldItalic) entry->boldItalic = (entry->boldFont != entry->regularFont) ? entry->boldFont : entry->regularFont;

  // 4. Create the Font Family
  EpdFontFamily* fontFamily = new EpdFontFamily(entry->regularFont, entry->boldFont, entry->italicFont, entry->boldItalic);
  g_fontFamilyStorage.push_back(std::unique_ptr<EpdFontFamily>(fontFamily));
  entry->fontFamily = fontFamily;
  entry->isLoaded = true;

  // 5. Register each style's file handle with the renderer
  renderer.insertStreamingFont(entry->id, std::move(regStream), *fontFamily);
  if (boldStream)  renderer.insertStreamingFont(entry->id, std::move(boldStream), *fontFamily);
  if (italStream)  renderer.insertStreamingFont(entry->id, std::move(italStream), *fontFamily);
  if (bitalStream) renderer.insertStreamingFont(entry->id, std::move(bitalStream), *fontFamily);

  Serial.printf("[FontManager] Registered streaming family: %s %dpt\n", entry->family.c_str(), entry->size);
  return true;
}

/**
 * @brief Ensures a font is ready for use, loading it if necessary
 * @param fontId Font ID to ensure is ready
 * @param renderer Graphics renderer to use for loading
 * @return true if font is ready, false otherwise
 */
bool FontManager::ensureFontReady(int fontId, GfxRenderer& renderer) {
  Serial.printf("ensureFontReady called for ID %d\n", fontId);

  if (fontId >= ATKINSON_HYPERLEGIBLE_8_FONT_ID && fontId <= ATKINSON_HYPERLEGIBLE_18_FONT_ID) {
    Serial.println("Font is built-in, already ready");
    return true;
  }

  for (auto& entry : g_sdFonts) {
    if (entry.id == fontId) {
      if (!entry.isLoaded) {
        Serial.printf("Font not loaded, calling loadFontFromSD for ID %d\n", fontId);
        return loadFontFromSD(fontId, renderer);
      }
      Serial.println("Font already loaded");
      return true;
    }
  }

  Serial.printf("Font ID %d not found in any font list!\n", fontId);
  return false;
}

/**
 * @brief Unloads a font from memory and closes SD handles
 */
bool FontManager::unloadFont(int fontId) {
  Serial.printf("[FontManager] Unloading font ID: %d\n", fontId);
  
  // Use g_renderer as suggested by the compiler
  if (g_renderer != nullptr) {
    g_renderer->removeFont(fontId); 
    return true;
  }
  
  return false;
}

/**
 * @brief Unloads all SD card fonts
 */
void FontManager::unloadAllSDFonts() { 
  Serial.println("[FontManager] Unloading all SD streaming fonts");
  
  if (g_renderer != nullptr) {
    g_renderer->removeAllStreamingFonts();
  }
}

/**
 * @brief Gets information about a specific font
 * @param fontId Font ID to get info for
 * @return Pointer to FontInfo structure, or nullptr if not found
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
 * @return Vector of FontInfo structures
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
 * @param family Font family name
 * @return Vector of FontInfo structures for the family
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
 * @return Vector of font family names
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
 * @param fontId Font ID to check
 * @return true if font is loaded, false otherwise
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
 * @param callback Progress callback function
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
  Serial.printf("SD fonts loaded: %d\n", loadedCount);
  Serial.printf("Permanent font storage size: %d fonts, %d families\n", (int)g_fontStorage.size(),
                (int)g_fontFamilyStorage.size());

  Serial.println("\nSD Font Families:");
  for (const auto& entry : g_sdFonts) {
    Serial.printf("  %s: %dpt %s\n", entry.family.c_str(), entry.size, entry.isLoaded ? "(loaded)" : "");
  }
  Serial.println("========================");
}

/**
 * @brief Gets font ID for a specific family and size
 * @param family Font family name
 * @param size Font size in points
 * @return Font ID, or default font ID if not found
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