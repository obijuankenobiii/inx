#include "ExternalFont.h"

#include <cstring>

ExternalFont::ExternalFont() : m_fontData(nullptr), m_glyphTableStart(0), m_glyphCount(0), m_bitmapDataStart(0) {}

ExternalFont::~ExternalFont() { unload(); }

void ExternalFont::unload() {
  if (m_file) m_file.close();
  if (m_fontData) {
    delete m_fontData;
    m_fontData = nullptr;
  }
}

bool ExternalFont::load(const char* path) {
  unload();
  m_filePath = path;
  m_file = SdMan.open(path, FILE_READ);
  if (!m_file) return false;

  // 1. Header Navigation
  uint32_t magic, version;
  m_file.read((uint8_t*)&magic, 4);
  m_file.read((uint8_t*)&version, 4);

  uint16_t nameLen;
  m_file.read((uint8_t*)&nameLen, 2);
  m_file.seek(m_file.position() + nameLen);

  // 2. Metrics
  m_fontData = new EpdFontData();
  int16_t lineHeight, ascender, descender;
  m_file.read((uint8_t*)&lineHeight, 2);
  m_file.read((uint8_t*)&ascender, 2);
  m_file.read((uint8_t*)&descender, 2);
  uint8_t is2Bit;
  m_file.read(&is2Bit, 1);

  m_fontData->advanceY = lineHeight;
  m_fontData->ascender = ascender;
  m_fontData->descender = descender;
  m_fontData->is2Bit = (is2Bit != 0);

  // 3. Skip Intervals
  uint16_t intervalCount;
  m_file.read((uint8_t*)&intervalCount, 2);
  m_file.seek(m_file.position() + (intervalCount * 12));

  // 4. Glyph Count & Table Caching
  m_file.read((uint8_t*)&m_glyphCount, 4);

  // Memory calculation: 542 glyphs * 24 bytes = 13,008 bytes
  uint32_t tableSize = m_glyphCount * 24;
  m_tableCache.assign(tableSize, 0);

  if (m_file.read(m_tableCache.data(), tableSize) != tableSize) {
    Serial.println("[ExternalFont] Table cache failed!");
    return false;
  }

  m_bitmapDataStart = m_file.position();

  Serial.printf("[ExternalFont] Cached %d glyphs (%u KB RAM used)\n", m_glyphCount, tableSize / 1024);
  return true;
}

bool ExternalFont::getGlyphMetadata(uint32_t cp, EpdGlyph& out) {
  if (m_tableCache.empty()) return false;

  int32_t low = 0, high = (int32_t)m_glyphCount - 1;
  const uint8_t* table = m_tableCache.data();

  while (low <= high) {
    int32_t mid = low + (high - low) / 2;
    const uint8_t* entry = &table[mid * 24];

    // Codepoint is at offset 18 (Type: uint32_t)
    uint32_t currentCP;
    memcpy(&currentCP, entry + 18, 4);

    if (currentCP == cp) {
      // Metrics (HHhhh)
      memcpy(&out.width, entry + 0, 2);
      memcpy(&out.height, entry + 2, 2);
      memcpy(&out.advanceX, entry + 4, 2);
      memcpy(&out.left, entry + 6, 2);
      memcpy(&out.top, entry + 8, 2);

      // Length and Offset (II)
      uint32_t relOffset;
      memcpy(&out.dataLength, entry + 10, 4);
      memcpy(&relOffset, entry + 14, 4);

      out.dataOffset = m_bitmapDataStart + relOffset;
      return true;
    }

    if (currentCP < cp)
      low = mid + 1;
    else
      high = mid - 1;
  }
  return false;
}

/**
 * @brief Reads raw bitmap bits from the SD card into the provided buffer.
 * @param absoluteOffset The pre-calculated absolute position in the .bin file.
 * @param length Total bytes to read (calculated during metadata fetch).
 * @param buffer The destination RAM buffer (managed by GfxRenderer).
 */
bool ExternalFont::getGlyphBitmap(uint32_t absoluteOffset, uint32_t length, uint8_t* buffer) {
  // 1. Safety check for empty glyphs (like spaces)
  if (length == 0) return true;
  if (!buffer) return false;

  // 2. Ensure the file handle is still valid
  // SD cards can occasionally timeout or close under heavy I/O
  if (!m_file || !m_file.isOpen()) {
    m_file = SdMan.open(m_filePath.c_str(), FILE_READ);
    if (!m_file) {
      Serial.printf("[ExternalFont] ERR: Could not reopen %s for bitmap read\n", m_filePath.c_str());
      return false;
    }
  }

  // 3. Move the file pointer to the start of the bitmap data
  if (!m_file.seek(absoluteOffset)) {
    Serial.printf("[ExternalFont] ERR: Seek failed to %u\n", absoluteOffset);
    return false;
  }

  // 4. Perform the read
  size_t bytesRead = m_file.read(buffer, length);

  // 5. Verify read integrity
  if (bytesRead != length) {
    Serial.printf("[ExternalFont] ERR: Read mismatch. Expected %u, got %u\n", length, bytesRead);
    return false;
  }

  // 6. Diagnostic: Check for "Dead Air" (All Zeros)
  // If the metadata is misaligned, we might be reading empty space between records
  bool hasData = false;
  for (uint32_t i = 0; i < length; i++) {
    if (buffer[i] != 0x00) {
      hasData = true;
      break;
    }
  }

  if (!hasData) {
    // We won't return false here because some glyphs might genuinely be empty,
    // but we log it so you can see if EVERY character is coming back blank.
    // Serial.printf("[ExternalFont] Warning: Glyph at %u is all zeros (transparent)\n", absoluteOffset);
  }

  return true;
}