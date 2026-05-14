#include "ExternalFont.h"

#include <Arduino.h>
#include <Utf8.h>
#include <cstring>
#include <new>

ExternalFont::ExternalFont() : m_fontData(nullptr) {}

ExternalFont::~ExternalFont() { unload(); }

void ExternalFont::metaCacheClear() {
  for (size_t i = 0; i < kGlyphMetaCacheSlots; ++i) {
    m_metaCache[i].cp = 0xFFFFFFFFu;
    m_metaCache[i].stamp = 0;
    m_metaCache[i].glyph = EpdGlyph{};
  }
  m_metaCacheGen = 0;
}

void ExternalFont::bitmapCacheClear() {
  if (m_bitmapCache) {
    for (size_t i = 0; i < kGlyphBitmapCacheSlots; ++i) {
      m_bitmapCache[i].offset = 0;
      m_bitmapCache[i].length = 0;
      m_bitmapCache[i].stamp = 0;
    }
  }
  memset(m_lowercaseGlyphOffsets, 0, sizeof(m_lowercaseGlyphOffsets));
  m_bitmapCacheGen = 0;
}

void ExternalFont::setLowercaseGlyphBitmapCacheEnabled(const bool enabled) {
  m_bitmapCacheEnabled = enabled;
  if (!enabled) {
    delete[] m_bitmapCache;
    m_bitmapCache = nullptr;
    bitmapCacheClear();
    return;
  }
  if (!m_bitmapCache) {
    m_bitmapCache = new (std::nothrow) GlyphBitmapCacheSlot[kGlyphBitmapCacheSlots]();
  }
  bitmapCacheClear();
}

void ExternalFont::unload() {
  if (m_file) m_file.close();
  if (m_fontData) {
    delete m_fontData;
    m_fontData = nullptr;
  }
  delete[] m_bitmapCache;
  m_bitmapCache = nullptr;
  m_bitmapCacheEnabled = false;
  m_glyphTableStart = 0;
  m_glyphCount = 0;
  m_bitmapDataStart = 0;
  metaCacheClear();
  bitmapCacheClear();
}

bool ExternalFont::load(const char* path) {
  unload();
  m_filePath = path;
  m_file = SdMan.open(path, FILE_READ);
  if (!m_file) return false;

  uint32_t magic, version;
  m_file.read(reinterpret_cast<uint8_t*>(&magic), 4);
  m_file.read(reinterpret_cast<uint8_t*>(&version), 4);

  uint16_t nameLen;
  m_file.read(reinterpret_cast<uint8_t*>(&nameLen), 2);
  m_file.seek(m_file.position() + nameLen);

  m_fontData = new EpdFontData();
  int16_t lineHeight, ascender, descender;
  m_file.read(reinterpret_cast<uint8_t*>(&lineHeight), 2);
  m_file.read(reinterpret_cast<uint8_t*>(&ascender), 2);
  m_file.read(reinterpret_cast<uint8_t*>(&descender), 2);
  uint8_t is2Bit;
  m_file.read(&is2Bit, 1);

  int lh = static_cast<int>(lineHeight);
  if (lh < 1) lh = 12;
  if (lh > 255) lh = 255;
  m_fontData->advanceY = static_cast<uint8_t>(lh);
  m_fontData->ascender = ascender;
  m_fontData->descender = descender;
  m_fontData->is2Bit = (is2Bit != 0);
  m_fontData->bitmap = nullptr;
  m_fontData->glyph = nullptr;
  m_fontData->intervals = nullptr;
  m_fontData->intervalCount = 0;

  uint16_t intervalCount;
  m_file.read(reinterpret_cast<uint8_t*>(&intervalCount), 2);
  m_file.seek(m_file.position() + (intervalCount * 12));

  m_file.read(reinterpret_cast<uint8_t*>(&m_glyphCount), 4);
  m_glyphTableStart = m_file.position();

  const uint32_t tableBytes = m_glyphCount * 24u;
  if (!m_file.seek(m_glyphTableStart + tableBytes)) {
    Serial.println("[ExternalFont] Seek past glyph table failed");
    return false;
  }
  m_bitmapDataStart = m_file.position();

  metaCacheClear();
  bitmapCacheClear();
  Serial.printf("[ExternalFont] On-demand glyph table: %u glyphs, meta %u-slot (~%u B), bitmap a-z %u-slot max (~%u B)\n",
                m_glyphCount, static_cast<unsigned>(kGlyphMetaCacheSlots), static_cast<unsigned>(sizeof(m_metaCache)),
                static_cast<unsigned>(kGlyphBitmapCacheSlots),
                static_cast<unsigned>(kGlyphBitmapCacheSlots * sizeof(GlyphBitmapCacheSlot)));
  return true;
}

bool ExternalFont::readGlyphEntryAtIndex(uint32_t index, uint8_t out24[24]) const {
  if (index >= m_glyphCount || out24 == nullptr) {
    return false;
  }
  FsFile* useFile = const_cast<FsFile*>(&m_file);
  if (!*useFile || !useFile->isOpen()) {
    *useFile = SdMan.open(m_filePath.c_str(), FILE_READ);
    if (!*useFile) {
      return false;
    }
  }
  const uint32_t pos = m_glyphTableStart + index * 24u;
  if (!useFile->seek(pos)) {
    return false;
  }
  return useFile->read(out24, 24) == 24;
}

bool ExternalFont::readCodepointAtIndex(uint32_t index, uint32_t& outCp) const {
  if (index >= m_glyphCount) {
    return false;
  }
  FsFile* useFile = const_cast<FsFile*>(&m_file);
  if (!*useFile || !useFile->isOpen()) {
    *useFile = SdMan.open(m_filePath.c_str(), FILE_READ);
    if (!*useFile) {
      return false;
    }
  }
  const uint32_t pos = m_glyphTableStart + index * 24u + 18u;
  if (!useFile->seek(pos)) {
    return false;
  }
  return useFile->read(reinterpret_cast<uint8_t*>(&outCp), 4) == 4;
}

void ExternalFont::decodeGlyphRow(const uint8_t entry[24], EpdGlyph& out) const {
  uint16_t w = 0, h = 0, ax = 0;
  int16_t lef = 0, tp = 0;
  uint32_t dlen = 0, rel = 0;
  memcpy(&w, entry + 0, 2);
  memcpy(&h, entry + 2, 2);
  memcpy(&ax, entry + 4, 2);
  memcpy(&lef, entry + 6, 2);
  memcpy(&tp, entry + 8, 2);
  memcpy(&dlen, entry + 10, 4);
  memcpy(&rel, entry + 14, 4);
  out.width = static_cast<uint8_t>(w > 255 ? 255 : w);
  out.height = static_cast<uint8_t>(h > 255 ? 255 : h);
  out.advanceX = static_cast<uint8_t>(ax > 255 ? 255 : ax);
  out.left = lef;
  out.top = tp;
  out.dataLength = static_cast<uint16_t>(dlen > 0xFFFFu ? 0xFFFFu : dlen);
  out.dataOffset = m_bitmapDataStart + rel;
}

bool ExternalFont::metaCacheLookup(uint32_t cp, EpdGlyph& out) {
  for (size_t i = 0; i < kGlyphMetaCacheSlots; ++i) {
    if (m_metaCache[i].cp == cp) {
      out = m_metaCache[i].glyph;
      m_metaCache[i].stamp = ++m_metaCacheGen;
      return true;
    }
  }
  return false;
}

void ExternalFont::metaCacheStore(uint32_t cp, const EpdGlyph& g) {
  size_t slot = 0;
  uint32_t bestStamp = 0xFFFFFFFFu;
  for (size_t i = 0; i < kGlyphMetaCacheSlots; ++i) {
    if (m_metaCache[i].cp == 0xFFFFFFFFu) {
      slot = i;
      break;
    }
    if (m_metaCache[i].stamp < bestStamp) {
      bestStamp = m_metaCache[i].stamp;
      slot = i;
    }
  }
  m_metaCache[slot].cp = cp;
  m_metaCache[slot].glyph = g;
  m_metaCache[slot].stamp = ++m_metaCacheGen;
}

bool ExternalFont::bitmapCacheLookup(uint32_t offset, uint32_t length, uint8_t* outputBuffer) {
  if (!m_bitmapCache || !outputBuffer || !bitmapCacheCanStore(offset, length)) {
    return false;
  }
  for (size_t i = 0; i < kGlyphBitmapCacheSlots; ++i) {
    if (m_bitmapCache[i].length == length && m_bitmapCache[i].offset == offset) {
      memcpy(outputBuffer, m_bitmapCache[i].data, length);
      m_bitmapCache[i].stamp = ++m_bitmapCacheGen;
      return true;
    }
  }
  return false;
}

void ExternalFont::bitmapCacheStore(uint32_t offset, uint32_t length, const uint8_t* data) {
  if (!m_bitmapCache || !data || !bitmapCacheCanStore(offset, length)) {
    return;
  }
  size_t slot = 0;
  uint32_t bestStamp = 0xFFFFFFFFu;
  for (size_t i = 0; i < kGlyphBitmapCacheSlots; ++i) {
    if (m_bitmapCache[i].length == 0) {
      slot = i;
      break;
    }
    if (m_bitmapCache[i].stamp < bestStamp) {
      bestStamp = m_bitmapCache[i].stamp;
      slot = i;
    }
  }
  m_bitmapCache[slot].offset = offset;
  m_bitmapCache[slot].length = static_cast<uint16_t>(length);
  memcpy(m_bitmapCache[slot].data, data, length);
  m_bitmapCache[slot].stamp = ++m_bitmapCacheGen;
}

bool ExternalFont::bitmapCacheCanStore(const uint32_t offset, const uint32_t length) const {
  if (!m_bitmapCacheEnabled || !m_bitmapCache || offset == 0 || length == 0 || length > kGlyphBitmapCacheMaxBytes) {
    return false;
  }
  for (size_t i = 0; i < kGlyphBitmapCacheSlots; ++i) {
    if (m_lowercaseGlyphOffsets[i] == offset) {
      return true;
    }
  }
  return false;
}

void ExternalFont::rememberLowercaseGlyphOffset(const uint32_t offset) {
  if (!m_bitmapCacheEnabled || offset == 0) {
    return;
  }
  for (size_t i = 0; i < kGlyphBitmapCacheSlots; ++i) {
    if (m_lowercaseGlyphOffsets[i] == offset) {
      return;
    }
    if (m_lowercaseGlyphOffsets[i] == 0) {
      m_lowercaseGlyphOffsets[i] = offset;
      return;
    }
  }
}

void ExternalFont::prewarmText(const char* utf8Text) {
  if (!utf8Text || !m_bitmapCacheEnabled || !m_bitmapCache) {
    return;
  }

  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(utf8Text);
  uint32_t cp = 0;
  uint8_t buffer[kGlyphBitmapCacheMaxBytes];
  while ((cp = utf8NextCodepoint(&ptr))) {
    EpdGlyph glyph{};
    if (!getGlyphMetadata(cp, glyph)) {
      if (!getGlyphMetadata(REPLACEMENT_GLYPH, glyph)) {
        continue;
      }
    }
    rememberLowercaseGlyphOffset(glyph.dataOffset);
    if (glyph.dataLength > 0 && glyph.dataLength <= sizeof(buffer)) {
      getGlyphBitmap(glyph.dataOffset, glyph.dataLength, buffer);
    }
  }
}

bool ExternalFont::getGlyphMetadata(uint32_t cp, EpdGlyph& out) {
  if (m_glyphCount == 0) {
    return false;
  }

  if (metaCacheLookup(cp, out)) {
    return true;
  }

  int32_t low = 0;
  int32_t high = static_cast<int32_t>(m_glyphCount) - 1;
  int32_t found = -1;

  while (low <= high) {
    const int32_t mid = low + (high - low) / 2;
    uint32_t midCp = 0;
    if (!readCodepointAtIndex(static_cast<uint32_t>(mid), midCp)) {
      return false;
    }

    if (midCp == cp) {
      found = mid;
      break;
    }

    if (midCp < cp) {
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }

  if (found < 0) {
    return false;
  }

  uint8_t entry[24];
  if (!readGlyphEntryAtIndex(static_cast<uint32_t>(found), entry)) {
    return false;
  }
  decodeGlyphRow(entry, out);
  metaCacheStore(cp, out);
  if (cp >= static_cast<uint32_t>('a') && cp <= static_cast<uint32_t>('z')) {
    rememberLowercaseGlyphOffset(out.dataOffset);
  }
  return true;
}

bool ExternalFont::getGlyphBitmap(uint32_t absoluteOffset, uint32_t length, uint8_t* buffer) {
  if (length == 0) return true;
  if (!buffer) return false;

  if (bitmapCacheLookup(absoluteOffset, length, buffer)) {
    return true;
  }

  if (!m_file || !m_file.isOpen()) {
    m_file = SdMan.open(m_filePath.c_str(), FILE_READ);
    if (!m_file) {
      Serial.printf("[ExternalFont] ERR: Could not reopen %s for bitmap read\n", m_filePath.c_str());
      return false;
    }
  }

  if (!m_file.seek(absoluteOffset)) {
    Serial.printf("[ExternalFont] ERR: Seek failed to %u\n", absoluteOffset);
    return false;
  }

  const size_t bytesRead = m_file.read(buffer, length);
  if (bytesRead != length) {
    Serial.printf("[ExternalFont] ERR: Read mismatch. Expected %u, got %u\n", length, static_cast<unsigned>(bytesRead));
    return false;
  }

  bitmapCacheStore(absoluteOffset, length, buffer);
  return true;
}
