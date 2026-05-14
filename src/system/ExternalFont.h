#pragma once
#include <string>
#include "SDCardManager.h"
#include "EpdFontData.h"

class ExternalFont {
public:
    ExternalFont();
    ~ExternalFont();
    bool load(const char* path);
    void unload();
    void setLowercaseGlyphBitmapCacheEnabled(bool enabled);
    void prewarmText(const char* utf8Text);
    bool getGlyphMetadata(uint32_t codePoint, EpdGlyph& outGlyph);
    bool getGlyphBitmap(uint32_t offset, uint32_t length, uint8_t* outputBuffer);
    EpdFontData* getData() { return m_fontData; }

private:
  bool readGlyphEntryAtIndex(uint32_t index, uint8_t out24[24]) const;
  bool readCodepointAtIndex(uint32_t index, uint32_t& outCp) const;
  void decodeGlyphRow(const uint8_t entry[24], EpdGlyph& out) const;
  bool metaCacheLookup(uint32_t cp, EpdGlyph& out);
  void metaCacheStore(uint32_t cp, const EpdGlyph& g);
  void metaCacheClear();
  bool bitmapCacheLookup(uint32_t offset, uint32_t length, uint8_t* outputBuffer);
  void bitmapCacheStore(uint32_t offset, uint32_t length, const uint8_t* data);
  void bitmapCacheClear();
  bool bitmapCacheCanStore(uint32_t offset, uint32_t length) const;
  void rememberLowercaseGlyphOffset(uint32_t offset);

  static constexpr size_t kGlyphMetaCacheSlots = 64;
  struct GlyphMetaCacheSlot {
    uint32_t cp = 0xFFFFFFFFu;
    uint32_t stamp = 0;
    EpdGlyph glyph{};
  };
  GlyphMetaCacheSlot m_metaCache[kGlyphMetaCacheSlots];
  uint32_t m_metaCacheGen = 0;

  static constexpr size_t kGlyphBitmapCacheSlots = 26;
  static constexpr size_t kGlyphBitmapCacheMaxBytes = 128;
  struct GlyphBitmapCacheSlot {
    uint32_t offset = 0;
    uint16_t length = 0;
    uint32_t stamp = 0;
    uint8_t data[kGlyphBitmapCacheMaxBytes] = {};
  };
  GlyphBitmapCacheSlot* m_bitmapCache = nullptr;
  uint32_t m_lowercaseGlyphOffsets[kGlyphBitmapCacheSlots] = {};
  uint32_t m_bitmapCacheGen = 0;
  bool m_bitmapCacheEnabled = false;

  EpdFontData* m_fontData;
  std::string m_filePath;
  FsFile m_file;
  uint32_t m_glyphTableStart = 0;
  uint32_t m_glyphCount = 0;
  uint32_t m_bitmapDataStart = 0;
};
