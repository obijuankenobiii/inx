#include "FontManager.h"
#include <builtinFonts/all.h>
#include <algorithm>
#include <cctype>
#include <map>
#include "SDCardManager.h"
#include "system/Fonts.h"
#include <memory> 

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

// PERMANENT STORAGE - keeps fonts alive forever (like static built-in fonts)
static std::vector<std::unique_ptr<EpdFontFamily>> g_fontFamilyStorage;
static std::vector<std::unique_ptr<EpdFont>> g_fontStorage;

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

static EpdFont* loadBinaryFont(const std::string& binPath) {
  FsFile file = SdMan.open(binPath.c_str(), FILE_READ);
  if (!file) return nullptr;

  // 1. Header & Magic Check
  uint32_t magic = 0;
  file.read((uint8_t*)&magic, 4);
  if (magic != 0x45504446) { // "EPDF"
    file.close();
    return nullptr;
  }

  uint32_t version;
  file.read((uint8_t*)&version, 4);

  // 2. Skip the font name string
  uint16_t nameLen;
  file.read((uint8_t*)&nameLen, 2);
  file.seek(file.position() + nameLen); 

  // 3. Global Metrics
  int16_t lineHeight, ascender, descender;
  uint8_t is2Bit;
  file.read((uint8_t*)&lineHeight, 2);
  file.read((uint8_t*)&ascender, 2);
  file.read((uint8_t*)&descender, 2);
  file.read(&is2Bit, 1); // Always 1 for 2-bit mode

  // 4. Unicode Intervals
  uint16_t intervalCount;
  file.read((uint8_t*)&intervalCount, 2);

  EpdUnicodeInterval* intervals = new EpdUnicodeInterval[intervalCount];
  for (int i = 0; i < intervalCount; i++) {
    file.read((uint8_t*)&intervals[i].first, 4);
    file.read((uint8_t*)&intervals[i].last, 4);
    file.read((uint8_t*)&intervals[i].offset, 4);
  }

  // 5. Glyph Headers (The 24-byte Sync Loop)
  uint32_t glyphCount;
  file.read((uint8_t*)&glyphCount, 4);

  EpdGlyph* glyphs = new EpdGlyph[glyphCount];
  for (int i = 0; i < (int)glyphCount; i++) {
    // Read the core 18 bytes of the struct
    file.read((uint8_t*)&glyphs[i].width, 2);      // Offset 0
    file.read((uint8_t*)&glyphs[i].height, 2);     // Offset 2
    file.read((uint8_t*)&glyphs[i].advanceX, 2);   // Offset 4
    file.read((uint8_t*)&glyphs[i].left, 2);       // Offset 6
    file.read((uint8_t*)&glyphs[i].top, 2);        // Offset 8
    file.read((uint8_t*)&glyphs[i].dataLength, 4); // Offset 10
    file.read((uint8_t*)&glyphs[i].dataOffset, 4); // Offset 14 (Total 18 bytes)
    
    // SYNC: Read the remaining 6 bytes added by the Python script
    // 4 bytes for code_point + 2 bytes for the H (uint16) padding
    uint8_t dummy[6];
    file.read(dummy, 6); 
    
    // Now the file pointer is at byte 24 of this glyph, 
    // perfectly aligned for the start of the next glyph.
  }

  // 6. Bitmap Data Loading
  size_t currentPos = file.position();
  size_t bitmapSize = file.size() - currentPos;
  
  if (bitmapSize <= 0) {
    file.close();
    delete[] glyphs;
    delete[] intervals;
    return nullptr; 
  }

  uint8_t* bitmapData = new uint8_t[bitmapSize];
  file.read(bitmapData, bitmapSize);
  file.close();

  // 7. Storage & Final Data Construction
  EpdFontData* fontData = new EpdFontData();
  fontData->bitmap = bitmapData;
  fontData->glyph = glyphs;
  fontData->intervals = intervals;
  fontData->intervalCount = intervalCount;
  fontData->advanceY = lineHeight; 
  fontData->ascender = ascender;
  fontData->descender = descender;
  fontData->is2Bit = (is2Bit != 0);

  // Persistence: Prevent ESP32 memory cleanup of dynamic allocations
  static std::vector<std::unique_ptr<uint8_t[]>> bitmapStorage;
  static std::vector<std::unique_ptr<EpdGlyph[]>> glyphStorage;
  static std::vector<std::unique_ptr<EpdUnicodeInterval[]>> intervalStorage;
  static std::vector<std::unique_ptr<EpdFontData>> fontDataStorage;

  bitmapStorage.push_back(std::unique_ptr<uint8_t[]>(bitmapData));
  glyphStorage.push_back(std::unique_ptr<EpdGlyph[]>(glyphs));
  intervalStorage.push_back(std::unique_ptr<EpdUnicodeInterval[]>(intervals));
  fontDataStorage.push_back(std::unique_ptr<EpdFontData>(fontData));
  
  g_fontStorage.push_back(std::unique_ptr<EpdFont>(new EpdFont(fontData)));
  
  return g_fontStorage.back().get();
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
  Serial.printf("=== loadFontFromSD called for ID %d ===\n", fontId);
  
  SDFontEntry* entry = nullptr;
  for (auto& e : g_sdFonts) {
    if (e.id == fontId) {
      entry = &e;
      break;
    }
  }

  if (!entry) {
    Serial.printf("Font ID %d not found in g_sdFonts!\n", fontId);
    return false;
  }

  if (entry->isLoaded) {
    Serial.printf("Font already loaded: %s %dpt\n", entry->family.c_str(), entry->size);
    return true;
  }

  // Load ONLY this specific font size (NOT all sizes in the family)
  Serial.printf("Loading font: %s %dpt from %s\n", entry->family.c_str(), entry->size, entry->regularPath.c_str());
  
  EpdFont* regularFont = loadBinaryFont(entry->regularPath);
  if (!regularFont) {
    Serial.printf("Failed to load regular font for: %s %dpt\n", entry->family.c_str(), entry->size);
    return false;
  }
  
  // Create fallback fonts from the same data
  EpdFont* boldFont = new EpdFont(regularFont->data);
  EpdFont* italicFont = new EpdFont(regularFont->data);
  EpdFont* boldItalicFont = new EpdFont(regularFont->data);
  
  // Create font family
  EpdFontFamily* fontFamily = new EpdFontFamily(regularFont, boldFont, italicFont, boldItalicFont);
  
  entry->regularFont = regularFont;
  entry->boldFont = boldFont;
  entry->italicFont = italicFont;
  entry->boldItalicFont = boldItalicFont;
  entry->fontFamily = fontFamily;
  entry->isLoaded = true;
  
  renderer.insertFont(entry->id, *fontFamily);
  Serial.printf("Loaded and inserted font: %s %dpt (ID: %d)\n", entry->family.c_str(), entry->size, entry->id);
  
  return true;
}

bool FontManager::ensureFontReady(int fontId, GfxRenderer& renderer) {
  Serial.printf("ensureFontReady called for ID %d\n", fontId);
  
  // Check built-in fonts
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

bool FontManager::unloadFont(int fontId) {
  // Note: With permanent storage, fonts are never unloaded
  // This is intentional to keep them alive like static fonts
  Serial.printf("unloadFont called for %d - fonts are permanently stored\n", fontId);
  return true;
}

void FontManager::unloadAllSDFonts() {
  // With permanent storage, we don't actually unload
  // This prevents memory corruption
  Serial.println("unloadAllSDFonts called - fonts are permanently stored");
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
  Serial.printf("Permanent font storage size: %d fonts, %d families\n", 
                (int)g_fontStorage.size(), (int)g_fontFamilyStorage.size());

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