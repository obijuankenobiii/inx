#include "FontManager.h"

#include <Arduino.h>
#include <algorithm>
#include <climits>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "ExternalFont.h"
#include "EpdFontFamily.h"
#include "SDCardManager.h"
#include "system/Fonts.h"

// Static member initialization
std::vector<FontManager::SDFontEntry> FontManager::g_sdFonts;
int FontManager::g_nextSDFontId = FontManager::SD_FONT_START_ID;
GfxRenderer* FontManager::g_renderer = nullptr;
FontManager::ProgressCallback FontManager::g_progressCallback = nullptr;

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
  const std::string stem =
      (dot != std::string::npos && dot > 0) ? filename.substr(0, dot) : filename;
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
  g_fontFamilyStorage.shrink_to_fit();
  g_fontStorage.shrink_to_fit();

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

  static EpdFont atkinson_hyperlegible8RegularFont(&atkinson_hyperlegible_8_regular);
  static EpdFontFamily atkinson_hyperlegible8FontFamily(&atkinson_hyperlegible8RegularFont, nullptr, nullptr,
                                                          nullptr);

  static EpdFont atkinson_hyperlegible10RegularFont(&atkinson_hyperlegible_10_regular);
  static EpdFont atkinson_hyperlegible10BoldFont(&atkinson_hyperlegible_10_bold);
  static EpdFont atkinson_hyperlegible10ItalicFont(&atkinson_hyperlegible_10_italic);
  static EpdFont atkinson_hyperlegible10BoldItalicFont(&atkinson_hyperlegible_10_bolditalic);
  static EpdFontFamily atkinson_hyperlegible10FontFamily(
      &atkinson_hyperlegible10RegularFont, &atkinson_hyperlegible10BoldFont, &atkinson_hyperlegible10ItalicFont,
      &atkinson_hyperlegible10BoldItalicFont);

  static EpdFont atkinson_hyperlegible12RegularFont(&atkinson_hyperlegible_12_regular);
  static EpdFont atkinson_hyperlegible12BoldFont(&atkinson_hyperlegible_12_bold);
  static EpdFont atkinson_hyperlegible12ItalicFont(&atkinson_hyperlegible_12_italic);
  static EpdFont atkinson_hyperlegible12BoldItalicFont(&atkinson_hyperlegible_12_bolditalic);
  static EpdFontFamily atkinson_hyperlegible12FontFamily(
      &atkinson_hyperlegible12RegularFont, &atkinson_hyperlegible12BoldFont, &atkinson_hyperlegible12ItalicFont,
      &atkinson_hyperlegible12BoldItalicFont);

  static EpdFont atkinson_hyperlegible14RegularFont(&atkinson_hyperlegible_14_regular);
  static EpdFont atkinson_hyperlegible14BoldFont(&atkinson_hyperlegible_14_bold);
  static EpdFont atkinson_hyperlegible14ItalicFont(&atkinson_hyperlegible_14_italic);
  static EpdFont atkinson_hyperlegible14BoldItalicFont(&atkinson_hyperlegible_14_bolditalic);
  static EpdFontFamily atkinson_hyperlegible14FontFamily(
      &atkinson_hyperlegible14RegularFont, &atkinson_hyperlegible14BoldFont, &atkinson_hyperlegible14ItalicFont,
      &atkinson_hyperlegible14BoldItalicFont);

  static EpdFont atkinson_hyperlegible16RegularFont(&atkinson_hyperlegible_16_regular);
  static EpdFont atkinson_hyperlegible16BoldFont(&atkinson_hyperlegible_16_bold);
  static EpdFont atkinson_hyperlegible16ItalicFont(&atkinson_hyperlegible_16_italic);
  static EpdFont atkinson_hyperlegible16BoldItalicFont(&atkinson_hyperlegible_16_bolditalic);
  static EpdFontFamily atkinson_hyperlegible16FontFamily(
      &atkinson_hyperlegible16RegularFont, &atkinson_hyperlegible16BoldFont, &atkinson_hyperlegible16ItalicFont,
      &atkinson_hyperlegible16BoldItalicFont);

  static EpdFont atkinson_hyperlegible18RegularFont(&atkinson_hyperlegible_18_regular);
  static EpdFont atkinson_hyperlegible18BoldFont(&atkinson_hyperlegible_18_bold);
  static EpdFont atkinson_hyperlegible18ItalicFont(&atkinson_hyperlegible_18_italic);
  static EpdFont atkinson_hyperlegible18BoldItalicFont(&atkinson_hyperlegible_18_bolditalic);
  static EpdFontFamily atkinson_hyperlegible18FontFamily(
      &atkinson_hyperlegible18RegularFont, &atkinson_hyperlegible18BoldFont, &atkinson_hyperlegible18ItalicFont,
      &atkinson_hyperlegible18BoldItalicFont);

  renderer.insertFont(LITERATA_10_FONT_ID, literata10RegularFontFamily);
  renderer.insertFont(LITERATA_12_FONT_ID, literata12RegularFontFamily);
  renderer.insertFont(LITERATA_14_FONT_ID, literata14RegularFontFamily);
  renderer.insertFont(LITERATA_16_FONT_ID, literata16RegularFontFamily);
  renderer.insertFont(LITERATA_18_FONT_ID, literata18RegularFontFamily);

  renderer.insertFont(ATKINSON_HYPERLEGIBLE_8_FONT_ID, atkinson_hyperlegible8FontFamily);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_10_FONT_ID, atkinson_hyperlegible10FontFamily);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_12_FONT_ID, atkinson_hyperlegible12FontFamily);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_14_FONT_ID, atkinson_hyperlegible14FontFamily);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_16_FONT_ID, atkinson_hyperlegible16FontFamily);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_18_FONT_ID, atkinson_hyperlegible18FontFamily);

  Serial.println("[FontManager] Initialized (Literata + Atkinson + SD streaming)");
  printMemoryUsage();
}

/**
 * @brief Gets the next font ID in sequence
 */
int FontManager::getNextFont(int currentFontId) {
  static const std::unordered_map<int, int> NEXT_FONT = {
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
      {ATKINSON_HYPERLEGIBLE_18_FONT_ID, ATKINSON_HYPERLEGIBLE_18_FONT_ID},
  };

  auto it = NEXT_FONT.find(currentFontId);
  if (it != NEXT_FONT.end()) {
    return it->second;
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
  g_sdFonts.shrink_to_fit();
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
 * Uses streaming ExternalFont with on-demand glyph table reads (no full index in RAM).
 */
bool FontManager::loadFontFromSD(int fontId, GfxRenderer& renderer) {
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
    if (!stream->load(path.c_str())) {
      Serial.printf("[FontManager] Skipping %s (failed to load): %s\n", label, path.c_str());
      return nullptr;
    }
    return stream;
  };

  std::unique_ptr<ExternalFont> regularStream(new ExternalFont());
  if (!regularStream->load(entry->regularPath.c_str())) {
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

  entry->fontFamily =
      new EpdFontFamily(entry->regularFont, entry->boldFont, entry->italicFont, entry->boldItalic);
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

  Serial.printf("[FontManager] Loaded font ID %d: %s %dpt (SD streaming, on-demand glyphs)\n", fontId,
                entry->family.c_str(), entry->size);

  return true;
}

void FontManager::ensureReaderLayoutFonts(int bodyFontId, GfxRenderer& renderer) {
  ensureFontReady(bodyFontId, renderer);
  ensureFontReady(getNextFont(bodyFontId), renderer);
  ensureFontReady(getMaxFontId(bodyFontId), renderer);
}

/**
 * @brief Ensures a font is ready for use, loading it if necessary
 */
bool FontManager::ensureFontReady(int fontId, GfxRenderer& renderer) {
  if (fontId >= LITERATA_10_FONT_ID && fontId <= LITERATA_18_FONT_ID) {
    return true;
  }
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
  std::vector<FontInfo> fonts;

  fonts.push_back({"Atkinson Hyperlegible 8", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_8_FONT_ID, 8, true});
  fonts.push_back({"Atkinson Hyperlegible 10", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_10_FONT_ID, 10, true});
  fonts.push_back({"Atkinson Hyperlegible 12", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_12_FONT_ID, 12, true});
  fonts.push_back({"Atkinson Hyperlegible 14", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_14_FONT_ID, 14, true});
  fonts.push_back({"Atkinson Hyperlegible 16", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_16_FONT_ID, 16, true});
  fonts.push_back({"Atkinson Hyperlegible 18", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_18_FONT_ID, 18, true});

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
  std::vector<FontInfo> result;

  if (family == "Atkinson Hyperlegible") {
    result.push_back({"Atkinson Hyperlegible 8", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_8_FONT_ID, 8, true});
    result.push_back({"Atkinson Hyperlegible 10", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_10_FONT_ID, 10, true});
    result.push_back({"Atkinson Hyperlegible 12", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_12_FONT_ID, 12, true});
    result.push_back({"Atkinson Hyperlegible 14", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_14_FONT_ID, 14, true});
    result.push_back({"Atkinson Hyperlegible 16", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_16_FONT_ID, 16, true});
    result.push_back({"Atkinson Hyperlegible 18", "Atkinson Hyperlegible", ATKINSON_HYPERLEGIBLE_18_FONT_ID, 18, true});
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
  std::vector<std::string> families;
  families.push_back("Atkinson Hyperlegible");
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
  if (fontId >= ATKINSON_HYPERLEGIBLE_8_FONT_ID && fontId <= ATKINSON_HYPERLEGIBLE_18_FONT_ID) {
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
 * @brief Sets the progress callback for font operations
 */
void FontManager::setProgressCallback(ProgressCallback callback) { g_progressCallback = callback; }

/**
 * @brief Prints font manager statistics to serial output
 */
void FontManager::printFontStats() {
  Serial.println("=== Font Manager Stats ===");
  Serial.printf("Built-in fonts: Literata + Atkinson (embedded)\n");
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
  if (currentFontId >= ATKINSON_HYPERLEGIBLE_8_FONT_ID && currentFontId <= ATKINSON_HYPERLEGIBLE_18_FONT_ID) {
    return ATKINSON_HYPERLEGIBLE_18_FONT_ID;
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
  return 2u + static_cast<uint32_t>(g_sdFamiliesSorted.size());
}

std::vector<std::string> FontManager::readerFontFamilyEnumLabels() {
  std::vector<std::string> out;
  out.reserve(2u + g_sdFamiliesSorted.size());
  out.push_back("Literata");
  out.push_back("Atkinson Hyperlegible");
  out.insert(out.end(), g_sdFamiliesSorted.begin(), g_sdFamiliesSorted.end());
  return out;
}

std::string FontManager::readerFontFamilyLabel(uint8_t slot) {
  if (slot == 0) {
    return "Literata";
  }
  if (slot == 1) {
    return "Atkinson Hyperlegible";
  }
  const size_t idx = static_cast<size_t>(slot) - 2u;
  if (idx < g_sdFamiliesSorted.size()) {
    return g_sdFamiliesSorted[idx];
  }
  return "Literata";
}

void FontManager::clampReaderFontFamilySlot(uint8_t& slot) {
  const uint32_t n = readerFontFamilyOptionCount();
  if (n == 0u) {
    slot = 0;
    return;
  }
  if (static_cast<uint32_t>(slot) >= n) {
    slot = 0;
  }
}

int FontManager::getFontIdNearestPointSize(const std::string& family, int preferredPt) {
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
