#pragma once
#include <string>

#include "EpdFontData.h"
#include "SDCardManager.h"

class ExternalFont {
 public:
  /** Construct an ExternalFont with no font loaded. */
  ExternalFont();
  /** Unload the font and release its resources. */
  ~ExternalFont();
  /** Load font metadata from an on-disk font file at path, keeping glyph data on SD for on-demand reads. */
  bool load(const char* path);
  /** Close the font file and release cached glyph data. */
  void unload();
  /** Enable or disable the shared glyph bitmap cache for this font instance. */
  void setGlyphBitmapCacheEnabled(bool enabled);
  /** Look up glyph metadata for a code point, using the metadata cache when possible. */
  bool getGlyphMetadata(uint32_t codePoint, EpdGlyph& outGlyph);
  /** Read length bytes of glyph bitmap data at offset into outputBuffer, using the bitmap cache when possible. */
  bool getGlyphBitmap(uint32_t offset, uint32_t length, uint8_t* outputBuffer);
  /** Return whether the loaded font contains anti-aliased (grayscale) glyph data. */
  bool hasAntiAliasData() const { return m_hasAntiAliasData; }
  /** Return the underlying EpdFontData for the loaded font. */
  EpdFontData* getData() { return m_fontData; }

 private:
  /** Determine whether the loaded 2-bit font actually contains anti-aliased pixel values. */
  bool detectAntiAliasData();
  /** Read the raw 24-byte glyph table entry at index into out24. */
  bool readGlyphEntryAtIndex(uint32_t index, uint8_t out24[24]) const;
  /** Read the code point stored in the glyph table entry at index. */
  bool readCodepointAtIndex(uint32_t index, uint32_t& outCp) const;
  /** Decode a raw 24-byte glyph table row into an EpdGlyph. */
  void decodeGlyphRow(const uint8_t entry[24], EpdGlyph& out) const;
  /** Look up a glyph's metadata in the per-instance metadata cache. */
  bool metaCacheLookup(uint32_t cp, EpdGlyph& out);
  /** Store a glyph's metadata in the per-instance metadata cache, evicting the oldest entry if full. */
  void metaCacheStore(uint32_t cp, const EpdGlyph& g);
  /** Clear the per-instance glyph metadata cache. */
  void metaCacheClear();
  /** Look up cached bitmap data for this font at offset/length, copying it into outputBuffer if found. */
  bool bitmapCacheLookup(uint32_t offset, uint32_t length, uint8_t* outputBuffer);
  /** Store bitmap data for this font in the shared bitmap cache, evicting the oldest entry if full. */
  void bitmapCacheStore(uint32_t offset, uint32_t length, const uint8_t* data);
  /** Clear this font's entries from the shared bitmap cache. */
  void bitmapCacheClear();
  /** Return whether the given offset/length is eligible for bitmap caching. */
  bool bitmapCacheCanStore(uint32_t offset, uint32_t length) const;

  static constexpr size_t kGlyphMetaCacheSlots = 64;
  struct GlyphMetaCacheSlot {
    uint32_t cp = 0xFFFFFFFFu;
    uint32_t stamp = 0;
    EpdGlyph glyph{};
  };
  GlyphMetaCacheSlot m_metaCache[kGlyphMetaCacheSlots];
  uint32_t m_metaCacheGen = 0;

  static constexpr size_t kGlyphBitmapCacheSlots = 32;
  static constexpr size_t kGlyphBitmapCacheMaxBytes = 512;
  struct GlyphBitmapCacheSlot {
    const ExternalFont* owner = nullptr;
    uint32_t offset = 0;
    uint16_t length = 0;
    uint32_t stamp = 0;
    uint8_t data[kGlyphBitmapCacheMaxBytes] = {};
  };
  static GlyphBitmapCacheSlot* s_bitmapCache;
  static uint32_t s_bitmapCacheGen;
  static uint8_t s_bitmapCacheUsers;
  bool m_bitmapCacheEnabled = false;

  EpdFontData* m_fontData;
  std::string m_filePath;
  FsFile m_file;
  uint32_t m_glyphTableStart = 0;
  uint32_t m_glyphCount = 0;
  uint32_t m_bitmapDataStart = 0;
  bool m_hasAntiAliasData = false;
};
