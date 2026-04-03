#include "ExternalFont.h"
#include <cstring>

ExternalFont::ExternalFont() 
    : m_fontData(nullptr)
    , m_glyphs(nullptr)
    , m_intervals(nullptr)
    , m_metadataSize(0) {
    // m_file initializes itself as "closed" automatically
}

ExternalFont::~ExternalFont() {
    unload();
}

bool ExternalFont::load(const char* path) {
    unload();
    m_filePath = path;
    return loadMetadata(path);
}

void ExternalFont::unload() {
    if (m_file) {
        m_file.close();
    }
    
    if (m_fontData) delete m_fontData;
    if (m_glyphs) delete[] m_glyphs;
    if (m_intervals) delete[] m_intervals;
    
    m_fontData = nullptr;
    m_glyphs = nullptr;
    m_intervals = nullptr;
    m_metadataSize = 0;
}

bool ExternalFont::loadMetadata(const char* path) {
    FsFile file = SdMan.open(path, FILE_READ);
    if (!file) return false;
    
    // 1. Read Header
    uint32_t magic;
    file.read((uint8_t*)&magic, 4);
    if (magic != 0x45504446) { // "EPDF"
        file.close();
        return false;
    }
    
    uint32_t version;
    file.read((uint8_t*)&version, 4);
    
    uint16_t nameLen;
    file.read((uint8_t*)&nameLen, 2);
    file.seek(file.position() + nameLen); // Skip font name string
    
    int16_t lineHeight, ascender, descender;
    uint8_t is2Bit;
    file.read((uint8_t*)&lineHeight, 2);
    file.read((uint8_t*)&ascender, 2);
    file.read((uint8_t*)&descender, 2);
    file.read(&is2Bit, 1);
    
    // 2. Read Intervals
    uint16_t intervalCount;
    file.read((uint8_t*)&intervalCount, 2);
    m_intervals = new EpdUnicodeInterval[intervalCount];
    file.read((uint8_t*)m_intervals, intervalCount * sizeof(EpdUnicodeInterval));
    m_metadataSize += intervalCount * sizeof(EpdUnicodeInterval);
    
    // 3. Read Glyph Metadata
    uint32_t glyphCount;
    file.read((uint8_t*)&glyphCount, 4);
    
    // Calculate where the raw bitmap data starts in the file
    // Header (variable) + Intervals + Glyph table (glyphCount * 24 bytes)
    uint32_t glyphTableSize = glyphCount * 24; 
    uint32_t bitmapDataStart = file.position() + glyphTableSize;
    
    m_glyphs = new EpdGlyph[glyphCount];
    for (uint32_t i = 0; i < glyphCount; i++) {
        file.read((uint8_t*)&m_glyphs[i].width, 2);
        file.read((uint8_t*)&m_glyphs[i].height, 2);
        file.read((uint8_t*)&m_glyphs[i].advanceX, 2);
        file.read((uint8_t*)&m_glyphs[i].left, 2);
        file.read((uint8_t*)&m_glyphs[i].top, 2);
        file.read((uint8_t*)&m_glyphs[i].dataLength, 4);
        
        uint32_t relativeOffset;
        file.read((uint8_t*)&relativeOffset, 4);
        
        uint32_t codePoint;
        file.read((uint8_t*)&codePoint, 4);
        uint16_t padding;
        file.read((uint8_t*)&padding, 2);
        
        // Convert to absolute file offset for direct seeking later
        m_glyphs[i].dataOffset = bitmapDataStart + relativeOffset;
    }
    m_metadataSize += glyphCount * sizeof(EpdGlyph);
    file.close();
    
    // 4. Populate EpdFontData (bitmap stays nullptr)
    m_fontData = new EpdFontData();
    m_fontData->bitmap = nullptr; 
    m_fontData->glyph = m_glyphs;
    m_fontData->intervals = m_intervals;
    m_fontData->intervalCount = intervalCount;
    m_fontData->advanceY = lineHeight;
    m_fontData->ascender = ascender;
    m_fontData->descender = descender;
    m_fontData->is2Bit = (is2Bit != 0);
    
    return true;
}

bool ExternalFont::getGlyphBitmap(uint32_t offset, uint32_t length, uint8_t* outputBuffer) {
    if (length == 0) return true;
    return readGlyphData(offset, length, outputBuffer);
}

bool ExternalFont::readGlyphData(uint32_t offset, uint32_t length, uint8_t* buffer) {
    if (!m_file) {
        m_file = SdMan.open(m_filePath.c_str(), FILE_READ);
        if (!m_file) {
            Serial.println("[StreamingFont] Failed to open file for reading");
            return false;
        }
    }
    
    if (!m_file.seek(offset)) {
        Serial.printf("[StreamingFont] Seek failed to offset %u\n", offset);
        return false;
    }
    
    size_t bytesRead = m_file.read(buffer, length);
    if (bytesRead != length) {
        Serial.printf("[StreamingFont] Read mismatch: expected %u, got %u\n", length, bytesRead);
        return false;
    }

    // DIAGNOSTIC: Check if we are just reading zeros
    if (buffer[0] == 0 && buffer[length-1] == 0) {
        // This is a hint that we might be seeking to a "hole" in the file
    }

    return true;
}