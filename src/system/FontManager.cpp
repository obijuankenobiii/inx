#include "FontManager.h"

#include <Arduino.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "EpdFontFamily.h"
#include "ExternalFont.h"
#include "SDCardManager.h"
#include "system/Fonts.h"

// Static member initialization
std::vector<FontManager::SDFontEntry> FontManager::g_sdFonts;
int FontManager::g_nextSDFontId = FontManager::SD_FONT_START_ID;
GfxRenderer* FontManager::g_renderer = nullptr;
std::vector<std::unique_ptr<EpdFontFamily>> FontManager::g_fontFamilyStorage;
std::vector<std::unique_ptr<EpdFont>> FontManager::g_fontStorage;

int FontManager::g_maxLoadedFonts = 8;  // SD (family,size) slots; glyph tables are on-demand (SD seeks), not RAM
int FontManager::g_loadedFontCount = 0;
bool FontManager::g_scannedForFonts = false;

namespace {
std::vector<std::string> g_sdFamiliesSorted;
}  // namespace

/**
 * @brief Extracts font size from filename (pt), e.g. Regular_14.bin -> 14.
 * Prefers the trailing "_<digits>" stem suffix so names like "4001_Regular_12.bin" still map to 12pt.
 */
static int extractSizeFromFilename(const std::string& filename) {
  const size_t dot = filename.rfind('.');
  const std::string stem = (dot != std::string::npos && dot > 0) ? filename.substr(0, dot) : filename;
  const size_t us = stem.rfind('_');
  if (us != std::string::npos && us + 1 < stem.size()) {
    size_t j = us + 1;
    while (j < stem.size() && isdigit(static_cast<unsigned char>(stem[j]))) {
      ++j;
    }
    if (j > us + 1 && j == stem.size()) {
      int v = 0;
      for (size_t k = us + 1; k < j; ++k) {
        v = v * 10 + (stem[k] - '0');
        if (v > 128) {
          v = 0;
          break;
        }
      }
      if (v > 0) return v;
    }
  }

  for (size_t i = 0; i < filename.length(); i++) {
    if (isdigit(static_cast<unsigned char>(filename[i]))) {
      long v = 0;
      while (i < filename.length() && isdigit(static_cast<unsigned char>(filename[i]))) {
        v = v * 10 + (filename[i] - '0');
        if (v > 128) return 0;
        i++;
      }
      return static_cast<int>(v);
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

  g_fontFamilyStorage.clear();
  g_fontStorage.clear();

  g_loadedFontCount = 0;
  g_scannedForFonts = false;

  static EpdFont literata10RegularFont(&literata_10_regular);
  static EpdFont literata10BoldFont(&literata_10_bold);
  static EpdFont literata10ItalicFont(&literata_10_italic);
  static EpdFont literata10BoldItalicFont(&literata_10_bolditalic);
  static EpdFontFamily literata10RegularFontFamily(&literata10RegularFont, &literata10BoldFont, &literata10ItalicFont,
                                                   &literata10BoldItalicFont);

  static EpdFont literata12RegularFont(&literata_12_regular);
  static EpdFont literata12BoldFont(&literata_12_bold);
  static EpdFont literata12ItalicFont(&literata_12_italic);
  static EpdFont literata12BoldItalicFont(&literata_12_bolditalic);
  static EpdFontFamily literata12RegularFontFamily(&literata12RegularFont, &literata12BoldFont, &literata12ItalicFont,
                                                   &literata12BoldItalicFont);

  static EpdFont literata14RegularFont(&literata_14_regular);
  static EpdFont literata14BoldFont(&literata_14_bold);
  static EpdFont literata14ItalicFont(&literata_14_italic);
  static EpdFont literata14BoldItalicFont(&literata_14_bolditalic);
  static EpdFontFamily literata14RegularFontFamily(&literata14RegularFont, &literata14BoldFont, &literata14ItalicFont,
                                                   &literata14BoldItalicFont);

  static EpdFont literata16RegularFont(&literata_16_regular);
  static EpdFont literata16BoldFont(&literata_16_bold);
  static EpdFont literata16ItalicFont(&literata_16_italic);
  static EpdFont literata16BoldItalicFont(&literata_16_bolditalic);
  static EpdFontFamily literata16RegularFontFamily(&literata16RegularFont, &literata16BoldFont, &literata16ItalicFont,
                                                   &literata16BoldItalicFont);

  static EpdFont literata18RegularFont(&literata_18_regular);
  static EpdFont literata18BoldFont(&literata_18_bold);
  static EpdFont literata18ItalicFont(&literata_18_italic);
  static EpdFont literata18BoldItalicFont(&literata_18_bolditalic);
  static EpdFontFamily literata18RegularFontFamily(&literata18RegularFont, &literata18BoldFont, &literata18ItalicFont,
                                                   &literata18BoldItalicFont);

  static EpdFont montserrat8RegularFont(&montserrat_8_regular);
  static EpdFontFamily montserrat8FontFamily(&montserrat8RegularFont, nullptr, nullptr, nullptr);

  static EpdFont montserrat10RegularFont(&montserrat_10_regular);
  static EpdFont montserrat10BoldFont(&montserrat_10_bold);
  static EpdFont montserrat10ItalicFont(&montserrat_10_italic);
  static EpdFont montserrat10BoldItalicFont(&montserrat_10_bolditalic);
  static EpdFontFamily montserrat10FontFamily(
      &montserrat10RegularFont, &montserrat10BoldFont, &montserrat10ItalicFont,
      &montserrat10BoldItalicFont);

  static EpdFont montserrat12RegularFont(&montserrat_12_regular);
  static EpdFont montserrat12BoldFont(&montserrat_12_bold);
  static EpdFont montserrat12ItalicFont(&montserrat_12_italic);
  static EpdFont montserrat12BoldItalicFont(&montserrat_12_bolditalic);
  static EpdFontFamily montserrat12FontFamily(
      &montserrat12RegularFont, &montserrat12BoldFont, &montserrat12ItalicFont,
      &montserrat12BoldItalicFont);

  static EpdFont montserrat14RegularFont(&montserrat_14_regular);
  static EpdFont montserrat14BoldFont(&montserrat_14_bold);
  static EpdFont montserrat14ItalicFont(&montserrat_14_italic);
  static EpdFont montserrat14BoldItalicFont(&montserrat_14_bolditalic);
  static EpdFontFamily montserrat14FontFamily(
      &montserrat14RegularFont, &montserrat14BoldFont, &montserrat14ItalicFont,
      &montserrat14BoldItalicFont);

  static EpdFont montserrat16RegularFont(&montserrat_16_regular);
  static EpdFont montserrat16BoldFont(&montserrat_16_bold);
  static EpdFont montserrat16ItalicFont(&montserrat_16_italic);
  static EpdFont montserrat16BoldItalicFont(&montserrat_16_bolditalic);
  static EpdFontFamily montserrat16FontFamily(
      &montserrat16RegularFont, &montserrat16BoldFont, &montserrat16ItalicFont,
      &montserrat16BoldItalicFont);

  static EpdFont montserrat18RegularFont(&montserrat_18_regular);
  static EpdFont montserrat18BoldFont(&montserrat_18_bold);
  static EpdFont montserrat18ItalicFont(&montserrat_18_italic);
  static EpdFont montserrat18BoldItalicFont(&montserrat_18_bolditalic);
  static EpdFontFamily montserrat18FontFamily(
      &montserrat18RegularFont, &montserrat18BoldFont, &montserrat18ItalicFont,
      &montserrat18BoldItalicFont);

  static EpdFont montserratClock70RegularFont(&montserrat_clock_70_regular);
  static EpdFont montserratClock70BoldFont(&montserrat_clock_70_bold);
  static EpdFontFamily montserratClock70FontFamily(&montserratClock70RegularFont, &montserratClock70BoldFont, nullptr,
                                                   nullptr);

  renderer.insertFont(LITERATA_10_FONT_ID, literata10RegularFontFamily);
  renderer.insertFont(LITERATA_12_FONT_ID, literata12RegularFontFamily);
  renderer.insertFont(LITERATA_14_FONT_ID, literata14RegularFontFamily);
  renderer.insertFont(LITERATA_16_FONT_ID, literata16RegularFontFamily);
  renderer.insertFont(LITERATA_18_FONT_ID, literata18RegularFontFamily);

  renderer.insertFont(MONTSERRAT_8_FONT_ID, montserrat8FontFamily);
  renderer.insertFont(MONTSERRAT_10_FONT_ID, montserrat10FontFamily);
  renderer.insertFont(MONTSERRAT_12_FONT_ID, montserrat12FontFamily);
  renderer.insertFont(MONTSERRAT_14_FONT_ID, montserrat14FontFamily);
  renderer.insertFont(MONTSERRAT_16_FONT_ID, montserrat16FontFamily);
  renderer.insertFont(MONTSERRAT_18_FONT_ID, montserrat18FontFamily);
  renderer.insertFont(MONTSERRAT_CLOCK_70_FONT_ID, montserratClock70FontFamily);

  Serial.println("[FontManager] Initialized (Literata + Montserrat + SD streaming)");
}

/**
 * @brief Gets the next font ID in sequence
 */
int FontManager::getNextFont(int currentFontId) {
  switch (currentFontId) {
    case LITERATA_10_FONT_ID:
      return LITERATA_12_FONT_ID;
    case LITERATA_12_FONT_ID:
      return LITERATA_14_FONT_ID;
    case LITERATA_14_FONT_ID:
      return LITERATA_16_FONT_ID;
    case LITERATA_16_FONT_ID:
    case LITERATA_18_FONT_ID:
      return LITERATA_18_FONT_ID;
    case MONTSERRAT_8_FONT_ID:
      return MONTSERRAT_10_FONT_ID;
    case MONTSERRAT_10_FONT_ID:
      return MONTSERRAT_12_FONT_ID;
    case MONTSERRAT_12_FONT_ID:
      return MONTSERRAT_14_FONT_ID;
    case MONTSERRAT_14_FONT_ID:
      return MONTSERRAT_16_FONT_ID;
    case MONTSERRAT_16_FONT_ID:
    case MONTSERRAT_18_FONT_ID:
      return MONTSERRAT_18_FONT_ID;
    default:
      break;
  }

  for (const auto& entry : g_sdFonts) {
    if (entry.id == currentFontId) {
      int nextId = -1;
      int nextSize = INT_MAX;
      for (const auto& e : g_sdFonts) {
        if (e.family == entry.family && e.size > entry.size && e.size < nextSize) {
          nextSize = e.size;
          nextId = e.id;
        }
      }
      if (nextId >= 0) {
        return nextId;
      }
      return currentFontId;
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

  if (forceRescan) {
    unloadAllSDFonts();
  }

  g_sdFonts.clear();
  g_nextSDFontId = FontManager::SD_FONT_START_ID;

  if (!SdMan.exists(sdPath)) {
    SdMan.mkdir(sdPath);
    g_scannedForFonts = true;
    rebuildSdReaderFamilyList();
    return false;
  }

  auto root = SdMan.open(sdPath);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    g_scannedForFonts = true;
    rebuildSdReaderFamilyList();
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
    if (group.second.regularPath.empty()) {
      continue;
    }
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
  rebuildSdReaderFamilyList();
  Serial.printf("[FontManager] Scanned %d font families, found %d font sizes\n", (int)families.size(),
                (int)g_sdFonts.size());
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
 * Uses streaming ExternalFont with on-demand glyph table reads (no full index in RAM).
 */
bool FontManager::loadFontFromSD(int fontId, GfxRenderer& renderer, const bool enableGlyphBitmapCache) {
  if (!g_scannedForFonts) {
    (void)scanSDFonts("/fonts", false);
  }

  SDFontEntry* entry = nullptr;
  for (auto& e : g_sdFonts) {
    if (e.id == fontId) {
      entry = &e;
      break;
    }
  }

  if (!entry) {
    Serial.printf("[FontManager] ID %d not found\n", fontId);
    return false;
  }

  if (entry->isLoaded && entry->fontFamily != nullptr) {
    return true;
  }

  while (g_loadedFontCount >= g_maxLoadedFonts) {
    unloadLRUFont();
  }

  auto loadOptionalStream = [&](const std::string& path, const char* label) -> std::unique_ptr<ExternalFont> {
    if (path.empty()) {
      return nullptr;
    }
    auto stream = std::unique_ptr<ExternalFont>(new ExternalFont());
    if (!stream->load(path.c_str(), enableGlyphBitmapCache)) {
      Serial.printf("[FontManager] Skipping %s (failed to load): %s\n", label, path.c_str());
      return nullptr;
    }
    return stream;
  };

  std::unique_ptr<ExternalFont> regularStream(new ExternalFont());
  if (!regularStream->load(entry->regularPath.c_str(), enableGlyphBitmapCache)) {
    Serial.printf("[FontManager] Failed to load regular: %s\n", entry->regularPath.c_str());
    return false;
  }

  entry->regularFont = new EpdFont(regularStream->getData());
  g_fontStorage.push_back(std::unique_ptr<EpdFont>(entry->regularFont));

  std::unique_ptr<ExternalFont> boldStream = loadOptionalStream(entry->boldPath, "bold");
  std::unique_ptr<ExternalFont> italicStream = loadOptionalStream(entry->italicPath, "italic");
  std::unique_ptr<ExternalFont> boldItalicStream = loadOptionalStream(entry->boldItalicPath, "boldItalic");

  entry->boldFont = nullptr;
  entry->italicFont = nullptr;
  entry->boldItalic = nullptr;
  if (boldStream) {
    entry->boldFont = new EpdFont(boldStream->getData());
    g_fontStorage.push_back(std::unique_ptr<EpdFont>(entry->boldFont));
  }
  if (italicStream) {
    entry->italicFont = new EpdFont(italicStream->getData());
    g_fontStorage.push_back(std::unique_ptr<EpdFont>(entry->italicFont));
  }
  if (boldItalicStream) {
    entry->boldItalic = new EpdFont(boldItalicStream->getData());
    g_fontStorage.push_back(std::unique_ptr<EpdFont>(entry->boldItalic));
  }

  entry->fontFamily = new EpdFontFamily(entry->regularFont, entry->boldFont, entry->italicFont, entry->boldItalic);
  g_fontFamilyStorage.push_back(std::unique_ptr<EpdFontFamily>(entry->fontFamily));

  entry->isLoaded = true;
  entry->lastUsed = millis();
  g_loadedFontCount++;

  renderer.insertStreamingFont(entry->id, std::move(regularStream), *(entry->fontFamily));
  if (boldStream) {
    renderer.addStreamingFontStyle(entry->id, EpdFontFamily::BOLD, std::move(boldStream));
  }
  if (italicStream) {
    renderer.addStreamingFontStyle(entry->id, EpdFontFamily::ITALIC, std::move(italicStream));
  }
  if (boldItalicStream) {
    renderer.addStreamingFontStyle(entry->id, EpdFontFamily::BOLD_ITALIC, std::move(boldItalicStream));
  }

  Serial.printf("[FontManager] Loaded font ID %d: %s %dpt (SD streaming, %s, on-demand glyphs)\n", fontId,
                entry->family.c_str(), entry->size, enableGlyphBitmapCache ? "cached" : "stream-only");

  return true;
}

bool FontManager::ensureReaderLayoutFonts(int bodyFontId, GfxRenderer& renderer) {
  const int maxFontId = getMaxFontId(bodyFontId);
  const int headerFontId = getNextFont(bodyFontId);
  int requiredIds[3] = {bodyFontId, maxFontId, headerFontId};
  int requiredCount = 0;

  for (int i = 0; i < 3; ++i) {
    bool seen = false;
    for (int j = 0; j < requiredCount; ++j) {
      if (requiredIds[j] == requiredIds[i]) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      requiredIds[requiredCount++] = requiredIds[i];
    }
  }

  bool needsSdLoad = false;
  for (int i = 0; i < requiredCount; ++i) {
    const int id = requiredIds[i];
    if (id >= SD_FONT_START_ID && !isFontLoaded(id)) {
      needsSdLoad = true;
      break;
    }
  }

  if (needsSdLoad && g_loadedFontCount > 0) {
    unloadAllSDFonts();
  }

  for (int i = 0; i < requiredCount; ++i) {
    const int fontId = requiredIds[i];
    if (fontId >= SD_FONT_START_ID) {
      const bool cacheGlyphBitmaps = fontId == bodyFontId;
      if (!loadFontFromSD(fontId, renderer, cacheGlyphBitmaps)) {
        return false;
      }
    } else if (!ensureFontReady(fontId, renderer)) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Ensures a font is ready for use, loading it if necessary
 */
bool FontManager::ensureFontReady(int fontId, GfxRenderer& renderer) {
  if (fontId >= LITERATA_10_FONT_ID && fontId <= LITERATA_18_FONT_ID) {
    return true;
  }
  if (fontId >= MONTSERRAT_8_FONT_ID && fontId <= MONTSERRAT_18_FONT_ID) {
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
      return true;
    }
  }

  return false;
}

void FontManager::withSdFontsReleasedForHeapIntensiveWork(const int readerBodyFontId, const std::function<void()>& fn) {
  bool hadSdLoaded = false;
  for (const auto& e : g_sdFonts) {
    if (e.isLoaded) {
      hadSdLoaded = true;
      break;
    }
  }
  if (!hadSdLoaded) {
    fn();
    return;
  }

  unloadAllSDFonts();
  fn();

  if (g_renderer != nullptr && readerBodyFontId >= SD_FONT_START_ID) {
    ensureReaderLayoutFonts(readerBodyFontId, *g_renderer);
  }
}

void FontManager::unloadAllSDFonts() {
  Serial.println("[FontManager] Unloading all SD streaming fonts");

  if (g_renderer != nullptr) {
    for (auto& entry : g_sdFonts) {
      if (entry.isLoaded) {
        g_renderer->removeFont(entry.id);
      }
    }
    g_renderer->removeAllStreamingFonts();
  }

  // Clear the storage vectors to free memory
  g_fontFamilyStorage.clear();
  g_fontStorage.clear();

  // Mark all SD fonts as unloaded
  for (auto& entry : g_sdFonts) {
    cleanupFontData(&entry);
  }

  g_loadedFontCount = 0;

  Serial.println("[FontManager] All SD fonts unloaded");
}

/**
 * @brief Gets information about a specific font
 */
const FontManager::FontInfo* FontManager::getFontInfo(int fontId) {
  if (!g_scannedForFonts && fontId >= SD_FONT_START_ID) {
    (void)scanSDFonts("/fonts", false);
  }

  static FontInfo info;

  switch (fontId) {
    case MONTSERRAT_8_FONT_ID:
      info = {"Montserrat 8", "Montserrat", fontId, 8, true};
      return &info;
    case MONTSERRAT_10_FONT_ID:
      info = {"Montserrat 10", "Montserrat", fontId, 10, true};
      return &info;
    case MONTSERRAT_12_FONT_ID:
      info = {"Montserrat 12", "Montserrat", fontId, 12, true};
      return &info;
    case MONTSERRAT_14_FONT_ID:
      info = {"Montserrat 14", "Montserrat", fontId, 14, true};
      return &info;
    case MONTSERRAT_16_FONT_ID:
      info = {"Montserrat 16", "Montserrat", fontId, 16, true};
      return &info;
    case MONTSERRAT_18_FONT_ID:
      info = {"Montserrat 18", "Montserrat", fontId, 18, true};
      return &info;
    case LITERATA_10_FONT_ID:
      info = {"Literata 10", "Literata", fontId, 10, true};
      return &info;
    case LITERATA_12_FONT_ID:
      info = {"Literata 12", "Literata", fontId, 12, true};
      return &info;
    case LITERATA_14_FONT_ID:
      info = {"Literata 14", "Literata", fontId, 14, true};
      return &info;
    case LITERATA_16_FONT_ID:
      info = {"Literata 16", "Literata", fontId, 16, true};
      return &info;
    case LITERATA_18_FONT_ID:
      info = {"Literata 18", "Literata", fontId, 18, true};
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
  if (!g_scannedForFonts) {
    (void)scanSDFonts("/fonts", false);
  }

  std::vector<FontInfo> fonts;

  fonts.push_back({"Montserrat 8", "Montserrat", MONTSERRAT_8_FONT_ID, 8, true});
  fonts.push_back({"Montserrat 10", "Montserrat", MONTSERRAT_10_FONT_ID, 10, true});
  fonts.push_back({"Montserrat 12", "Montserrat", MONTSERRAT_12_FONT_ID, 12, true});
  fonts.push_back({"Montserrat 14", "Montserrat", MONTSERRAT_14_FONT_ID, 14, true});
  fonts.push_back({"Montserrat 16", "Montserrat", MONTSERRAT_16_FONT_ID, 16, true});
  fonts.push_back({"Montserrat 18", "Montserrat", MONTSERRAT_18_FONT_ID, 18, true});

  fonts.push_back({"Literata 10", "Literata", LITERATA_10_FONT_ID, 10, true});
  fonts.push_back({"Literata 12", "Literata", LITERATA_12_FONT_ID, 12, true});
  fonts.push_back({"Literata 14", "Literata", LITERATA_14_FONT_ID, 14, true});
  fonts.push_back({"Literata 16", "Literata", LITERATA_16_FONT_ID, 16, true});
  fonts.push_back({"Literata 18", "Literata", LITERATA_18_FONT_ID, 18, true});

  for (const auto& entry : g_sdFonts) {
    fonts.push_back({entry.family + " " + std::to_string(entry.size), entry.family, entry.id, entry.size, false});
  }

  return fonts;
}

/**
 * @brief Gets all fonts belonging to a specific family
 */
std::vector<FontManager::FontInfo> FontManager::getFontsByFamily(const std::string& family) {
  if (!g_scannedForFonts && family != "Montserrat" && family != "Literata") {
    (void)scanSDFonts("/fonts", false);
  }

  std::vector<FontInfo> result;

  if (family == "Montserrat") {
    result.push_back({"Montserrat 8", "Montserrat", MONTSERRAT_8_FONT_ID, 8, true});
    result.push_back({"Montserrat 10", "Montserrat", MONTSERRAT_10_FONT_ID, 10, true});
    result.push_back({"Montserrat 12", "Montserrat", MONTSERRAT_12_FONT_ID, 12, true});
    result.push_back({"Montserrat 14", "Montserrat", MONTSERRAT_14_FONT_ID, 14, true});
    result.push_back({"Montserrat 16", "Montserrat", MONTSERRAT_16_FONT_ID, 16, true});
    result.push_back({"Montserrat 18", "Montserrat", MONTSERRAT_18_FONT_ID, 18, true});
  }

  if (family == "Literata") {
    result.push_back({"Literata 10", "Literata", LITERATA_10_FONT_ID, 10, true});
    result.push_back({"Literata 12", "Literata", LITERATA_12_FONT_ID, 12, true});
    result.push_back({"Literata 14", "Literata", LITERATA_14_FONT_ID, 14, true});
    result.push_back({"Literata 16", "Literata", LITERATA_16_FONT_ID, 16, true});
    result.push_back({"Literata 18", "Literata", LITERATA_18_FONT_ID, 18, true});
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
  if (!g_scannedForFonts) {
    (void)scanSDFonts("/fonts", false);
  }

  std::vector<std::string> families;
  families.push_back("Montserrat");
  families.push_back("Literata");

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
  if (fontId >= MONTSERRAT_8_FONT_ID && fontId <= MONTSERRAT_18_FONT_ID) {
    return true;
  }
  if (fontId >= LITERATA_10_FONT_ID && fontId <= LITERATA_18_FONT_ID) {
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
 * @brief Prints font manager statistics to serial output
 */
void FontManager::printFontStats() {
  Serial.println("=== Font Manager Stats ===");
  Serial.printf("Built-in fonts: Literata + Montserrat (embedded)\n");
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
 * @brief Gets font ID for a specific family and size
 */
int FontManager::getFontId(const std::string& family, int size) {
  if (family == "Montserrat") {
    switch (size) {
      case 8:
        return MONTSERRAT_8_FONT_ID;
      case 10:
        return MONTSERRAT_10_FONT_ID;
      case 12:
        return MONTSERRAT_12_FONT_ID;
      case 14:
        return MONTSERRAT_14_FONT_ID;
      case 16:
        return MONTSERRAT_16_FONT_ID;
      case 18:
        return MONTSERRAT_18_FONT_ID;
      default:
        return MONTSERRAT_12_FONT_ID;
    }
  }
  if (family == "Literata") {
    switch (size) {
      case 10:
        return LITERATA_10_FONT_ID;
      case 12:
        return LITERATA_12_FONT_ID;
      case 14:
        return LITERATA_14_FONT_ID;
      case 16:
        return LITERATA_16_FONT_ID;
      case 18:
        return LITERATA_18_FONT_ID;
      default:
        return LITERATA_14_FONT_ID;
    }
  }

  for (const auto& entry : g_sdFonts) {
    if (entry.family == family && entry.size == size) {
      return entry.id;
    }
  }

  return LITERATA_14_FONT_ID;
}

int FontManager::getMaxFontId(int currentFontId) {
  if (currentFontId >= MONTSERRAT_8_FONT_ID && currentFontId <= MONTSERRAT_18_FONT_ID) {
    return MONTSERRAT_18_FONT_ID;
  }
  if (currentFontId >= LITERATA_10_FONT_ID && currentFontId <= LITERATA_18_FONT_ID) {
    return LITERATA_18_FONT_ID;
  }
  for (const auto& entry : g_sdFonts) {
    if (entry.id == currentFontId) {
      int bestId = entry.id;
      int bestSize = entry.size;
      for (const auto& e : g_sdFonts) {
        if (e.family == entry.family && e.size > bestSize) {
          bestSize = e.size;
          bestId = e.id;
        }
      }
      return bestId;
    }
  }
  return currentFontId;
}

void FontManager::rebuildSdReaderFamilyList() {
  g_sdFamiliesSorted.clear();
  std::set<std::string> uniq;
  for (const auto& e : g_sdFonts) {
    uniq.insert(e.family);
  }
  g_sdFamiliesSorted.assign(uniq.begin(), uniq.end());
}

uint32_t FontManager::readerFontFamilyOptionCount() {
  if (!g_scannedForFonts) {
    (void)scanSDFonts("/fonts", false);
  }
  return 2u + static_cast<uint32_t>(g_sdFamiliesSorted.size());
}

std::vector<std::string> FontManager::readerFontFamilyEnumLabels() {
  if (!g_scannedForFonts) {
    (void)scanSDFonts("/fonts", false);
  }
  std::vector<std::string> out;
  out.push_back("Literata");
  out.push_back("Montserrat");
  out.insert(out.end(), g_sdFamiliesSorted.begin(), g_sdFamiliesSorted.end());
  return out;
}

std::string FontManager::readerFontFamilyLabel(uint8_t slot) {
  if (!g_scannedForFonts && slot >= 2u) {
    (void)scanSDFonts("/fonts", false);
  }
  if (slot == 0) {
    return "Literata";
  }
  if (slot == 1) {
    return "Montserrat";
  }
  const size_t idx = static_cast<size_t>(slot) - 2u;
  if (idx < g_sdFamiliesSorted.size()) {
    return g_sdFamiliesSorted[idx];
  }
  return "Literata";
}

void FontManager::clampReaderFontFamilySlot(uint8_t& slot) {
  if (!g_scannedForFonts) {
    if (static_cast<uint32_t>(slot) < 2u) {
      return;
    }
    return;
  }
  const uint32_t n = readerFontFamilyOptionCount();
  if (static_cast<uint32_t>(slot) >= n) {
    slot = 0;
  }
}

int FontManager::getFontIdNearestPointSize(const std::string& family, int preferredPt) {
  if (!g_scannedForFonts && family != "Montserrat" && family != "Literata") {
    (void)scanSDFonts("/fonts", false);
  }
  int smallestGeId = -1;
  int smallestGeSize = INT_MAX;
  int largestLtId = -1;
  int largestLtSize = -1;
  bool any = false;
  for (const auto& e : g_sdFonts) {
    if (e.family != family) {
      continue;
    }
    any = true;
    if (e.size == preferredPt) {
      return e.id;
    }
    if (e.size > preferredPt && e.size < smallestGeSize) {
      smallestGeSize = e.size;
      smallestGeId = e.id;
    }
    if (e.size < preferredPt && e.size > largestLtSize) {
      largestLtSize = e.size;
      largestLtId = e.id;
    }
  }
  if (!any) {
    return LITERATA_14_FONT_ID;
  }
  if (smallestGeId >= 0) {
    return smallestGeId;
  }
  if (largestLtId >= 0) {
    return largestLtId;
  }
  return LITERATA_14_FONT_ID;
}
