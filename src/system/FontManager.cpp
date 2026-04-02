#include "FontManager.h"
#include <builtinFonts/all.h>
#include <algorithm>
#include <cctype>
#include <map>
#include "SDCardManager.h"
#include "system/Fonts.h"

// Storage for SD card fonts (updated to store all style paths)
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
  EpdFont* boldItalicFont;
  EpdFontFamily* fontFamily;
  bool isLoaded;
};

static std::vector<SDFontEntry> g_sdFonts;
static int g_nextSDFontId = FontManager::SD_FONT_START_ID;
static GfxRenderer* g_renderer = nullptr;
static FontManager::ProgressCallback g_progressCallback = nullptr;

static int extractSizeFromFilename(const std::string& filename) {
  // Find digits in the filename
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

// Helper to extract style from filename
static std::string extractStyleFromFilename(const std::string& filename) {
  if (filename.find("bolditalic") != std::string::npos) return "bolditalic";
  if (filename.find("bold") != std::string::npos) return "bold";
  if (filename.find("italic") != std::string::npos) return "italic";
  return "regular";
}

// Load binary font file and create EpdFont
static EpdFont* loadBinaryFont(const std::string& binPath) {
  FsFile file = SdMan.open(binPath.c_str(), FILE_READ);
  if (!file) {
    Serial.printf("Failed to open font file: %s\n", binPath.c_str());
    return nullptr;
  }

  size_t fileSize = file.size();
  if (fileSize == 0) {
    file.close();
    return nullptr;
  }

  EpdFontData* fontData = new EpdFontData();
  if (file.read((uint8_t*)fontData, fileSize) != fileSize) {
    delete fontData;
    file.close();
    return nullptr;
  }
  file.close();

  EpdFont* font = new EpdFont(fontData);
  return font;
}

void FontManager::initialize(GfxRenderer& renderer) {
  g_renderer = &renderer;

  // Atkinson Hyperlegible built-in fonts
  static EpdFont atkinson_hyperlegible8RegularFont(&atkinson_hyperlegible_8_regular);
  static EpdFontFamily atkinson_hyperlegible8FontFamily(&atkinson_hyperlegible8RegularFont, nullptr, nullptr, nullptr);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_8_FONT_ID, atkinson_hyperlegible8FontFamily);

  static EpdFont atkinson_hyperlegible10RegularFont(&atkinson_hyperlegible_10_regular);
  static EpdFont atkinson_hyperlegible10BoldFont(&atkinson_hyperlegible_10_bold);
  static EpdFont atkinson_hyperlegible10ItalicFont(&atkinson_hyperlegible_10_italic);
  static EpdFont atkinson_hyperlegible10BoldItalicFont(&atkinson_hyperlegible_10_bolditalic);
  static EpdFontFamily atkinson_hyperlegible10FontFamily(
      &atkinson_hyperlegible10RegularFont, &atkinson_hyperlegible10BoldFont, &atkinson_hyperlegible10ItalicFont,
      &atkinson_hyperlegible10BoldItalicFont);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_10_FONT_ID, atkinson_hyperlegible10FontFamily);

  static EpdFont atkinson_hyperlegible12RegularFont(&atkinson_hyperlegible_12_regular);
  static EpdFont atkinson_hyperlegible12BoldFont(&atkinson_hyperlegible_12_bold);
  static EpdFont atkinson_hyperlegible12ItalicFont(&atkinson_hyperlegible_12_italic);
  static EpdFont atkinson_hyperlegible12BoldItalicFont(&atkinson_hyperlegible_12_bolditalic);
  static EpdFontFamily atkinson_hyperlegible12FontFamily(
      &atkinson_hyperlegible12RegularFont, &atkinson_hyperlegible12BoldFont, &atkinson_hyperlegible12ItalicFont,
      &atkinson_hyperlegible12BoldItalicFont);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_12_FONT_ID, atkinson_hyperlegible12FontFamily);

  static EpdFont atkinson_hyperlegible14RegularFont(&atkinson_hyperlegible_14_regular);
  static EpdFont atkinson_hyperlegible14BoldFont(&atkinson_hyperlegible_14_bold);
  static EpdFont atkinson_hyperlegible14ItalicFont(&atkinson_hyperlegible_14_italic);
  static EpdFont atkinson_hyperlegible14BoldItalicFont(&atkinson_hyperlegible_14_bolditalic);
  static EpdFontFamily atkinson_hyperlegible14FontFamily(
      &atkinson_hyperlegible14RegularFont, &atkinson_hyperlegible14BoldFont, &atkinson_hyperlegible14ItalicFont,
      &atkinson_hyperlegible14BoldItalicFont);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_14_FONT_ID, atkinson_hyperlegible14FontFamily);

  static EpdFont atkinson_hyperlegible16RegularFont(&atkinson_hyperlegible_16_regular);
  static EpdFont atkinson_hyperlegible16BoldFont(&atkinson_hyperlegible_16_bold);
  static EpdFont atkinson_hyperlegible16ItalicFont(&atkinson_hyperlegible_16_italic);
  static EpdFont atkinson_hyperlegible16BoldItalicFont(&atkinson_hyperlegible_16_bolditalic);
  static EpdFontFamily atkinson_hyperlegible16FontFamily(
      &atkinson_hyperlegible16RegularFont, &atkinson_hyperlegible16BoldFont, &atkinson_hyperlegible16ItalicFont,
      &atkinson_hyperlegible16BoldItalicFont);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_16_FONT_ID, atkinson_hyperlegible16FontFamily);

  static EpdFont atkinson_hyperlegible18RegularFont(&atkinson_hyperlegible_18_regular);
  static EpdFont atkinson_hyperlegible18BoldFont(&atkinson_hyperlegible_18_bold);
  static EpdFont atkinson_hyperlegible18ItalicFont(&atkinson_hyperlegible_18_italic);
  static EpdFont atkinson_hyperlegible18BoldItalicFont(&atkinson_hyperlegible_18_bolditalic);
  static EpdFontFamily atkinson_hyperlegible18FontFamily(
      &atkinson_hyperlegible18RegularFont, &atkinson_hyperlegible18BoldFont, &atkinson_hyperlegible18ItalicFont,
      &atkinson_hyperlegible18BoldItalicFont);
  renderer.insertFont(ATKINSON_HYPERLEGIBLE_18_FONT_ID, atkinson_hyperlegible18FontFamily);
}

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

  // Handle SD card fonts
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

  // Find all directories (families)
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

  // Use map to group fonts by family and size
  struct FontGroup {
    std::string family;
    int size;
    std::string regularPath;
    std::string boldPath;
    std::string italicPath;
    std::string boldItalicPath;
  };
  std::map<std::pair<std::string, int>, FontGroup> groups;

  // Scan each family directory
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
      
      if (!file.isDirectory() && filename.length() > 4 && 
          filename.substr(filename.length() - 4) == ".bin") {
        
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

  // Convert groups to SDFontEntry
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
    entry.boldItalicFont = nullptr;
    entry.fontFamily = nullptr;
    entry.isLoaded = false;
    g_sdFonts.push_back(entry);
    
    Serial.printf("Found font: %s %dpt (ID: %d)\n", 
                 entry.family.c_str(), entry.size, entry.id);
  }

  Serial.printf("Scanned %d font families, found %d font sizes\n", 
               (int)families.size(), (int)g_sdFonts.size());
  return true;
}

bool FontManager::loadFontFromSD(int fontId, GfxRenderer& renderer) {
  SDFontEntry* entry = nullptr;
  for (auto& e : g_sdFonts) {
    if (e.id == fontId) {
      entry = &e;
      break;
    }
  }

  if (!entry) {
    Serial.printf("Font ID %d not found\n", fontId);
    return false;
  }

  if (entry->isLoaded) {
    return true;
  }

  Serial.printf("Loading font: %s %dpt\n", entry->family.c_str(), entry->size);

  // Load all available styles - store them as member variables of the entry
  // They will persist as long as the entry exists in g_sdFonts
  EpdFont* regularFont = nullptr;
  EpdFont* boldFont = nullptr;
  EpdFont* italicFont = nullptr;
  EpdFont* boldItalicFont = nullptr;
  
  if (!entry->regularPath.empty()) {
    regularFont = loadBinaryFont(entry->regularPath);
    Serial.printf("Regular font loaded: %s\n", regularFont ? "YES" : "NO");
  }
  if (!entry->boldPath.empty()) {
    boldFont = loadBinaryFont(entry->boldPath);
  }
  if (!entry->italicPath.empty()) {
    italicFont = loadBinaryFont(entry->italicPath);
  }
  if (!entry->boldItalicPath.empty()) {
    boldItalicFont = loadBinaryFont(entry->boldItalicPath);
  }
  
  // Regular font is required
  if (!regularFont) {
    Serial.printf("Failed to load regular font for: %s %dpt\n", entry->family.c_str(), entry->size);
    return false;
  }
  
  // Use regular font as fallback for missing styles
  if (!boldFont) boldFont = new EpdFont(regularFont->data);
  if (!italicFont) italicFont = new EpdFont(regularFont->data);
  if (!boldItalicFont) boldItalicFont = new EpdFont(regularFont->data);

  // Store the fonts in the entry (they will persist)
  entry->regularFont = regularFont;
  entry->boldFont = boldFont;
  entry->italicFont = italicFont;
  entry->boldItalicFont = boldItalicFont;
  
  // Create font family using the stored fonts
  entry->fontFamily = new EpdFontFamily(regularFont, boldFont, italicFont, boldItalicFont);
  entry->isLoaded = true;

  // Insert into renderer
  renderer.insertFont(entry->id, *entry->fontFamily);
  Serial.printf("Inserted font ID %d into renderer\n", entry->id);

  return true;
}

bool FontManager::ensureFontReady(int fontId, GfxRenderer& renderer) {
  if (fontId >= ATKINSON_HYPERLEGIBLE_8_FONT_ID && fontId <= ATKINSON_HYPERLEGIBLE_18_FONT_ID) {
    return true;
  }

  for (auto& entry : g_sdFonts) {
    if (entry.id == fontId) {
      if (!entry.isLoaded) {
        return loadFontFromSD(fontId, renderer);
      }
      return true;
    }
  }
  return false;
}

bool FontManager::unloadFont(int fontId) {
  for (auto it = g_sdFonts.begin(); it != g_sdFonts.end(); ++it) {
    if (it->id == fontId && it->isLoaded) {
      delete it->regularFont;
      delete it->boldFont;
      delete it->italicFont;
      delete it->boldItalicFont;
      delete it->fontFamily;
      it->regularFont = nullptr;
      it->boldFont = nullptr;
      it->italicFont = nullptr;
      it->boldItalicFont = nullptr;
      it->fontFamily = nullptr;
      it->isLoaded = false;
      return true;
    }
  }
  return false;
}

void FontManager::unloadAllSDFonts() {
  for (auto& entry : g_sdFonts) {
    if (entry.isLoaded) {
      delete entry.regularFont;
      delete entry.boldFont;
      delete entry.italicFont;
      delete entry.boldItalicFont;
      delete entry.fontFamily;
      entry.regularFont = nullptr;
      entry.boldFont = nullptr;
      entry.italicFont = nullptr;
      entry.boldItalicFont = nullptr;
      entry.fontFamily = nullptr;
      entry.isLoaded = false;
    }
  }
}

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

void FontManager::setProgressCallback(ProgressCallback callback) { 
  g_progressCallback = callback; 
}

void FontManager::printFontStats() {
  Serial.println("=== Font Manager Stats ===");
  Serial.printf("Built-in fonts: 6\n");
  Serial.printf("SD fonts discovered: %d\n", (int)g_sdFonts.size());

  int loadedCount = 0;
  for (const auto& entry : g_sdFonts) {
    if (entry.isLoaded) loadedCount++;
  }
  Serial.printf("SD fonts loaded: %d\n", loadedCount);

  Serial.println("\nSD Font Families:");
  for (const auto& entry : g_sdFonts) {
    Serial.printf("  %s: %dpt %s\n", entry.family.c_str(), entry.size, entry.isLoaded ? "(loaded)" : "");
  }
  Serial.println("========================");
}

int FontManager::getFontId(const std::string& family, int size) {
    // Check built-in fonts
    if (family == "Atkinson Hyperlegible") {
        switch (size) {
            case 8: return ATKINSON_HYPERLEGIBLE_8_FONT_ID;
            case 10: return ATKINSON_HYPERLEGIBLE_10_FONT_ID;
            case 12: return ATKINSON_HYPERLEGIBLE_12_FONT_ID;
            case 14: return ATKINSON_HYPERLEGIBLE_14_FONT_ID;
            case 16: return ATKINSON_HYPERLEGIBLE_16_FONT_ID;
            case 18: return ATKINSON_HYPERLEGIBLE_18_FONT_ID;
            default: return ATKINSON_HYPERLEGIBLE_12_FONT_ID;
        }
    }
    
    // Check SD card fonts
    for (const auto& entry : g_sdFonts) {
        if (entry.family == family && entry.size == size) {
            return entry.id;
        }
    }
    
    // Fallback to Atkinson Hyperlegible 12pt
    return ATKINSON_HYPERLEGIBLE_12_FONT_ID;
}