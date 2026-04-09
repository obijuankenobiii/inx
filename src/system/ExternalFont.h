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
    bool getGlyphMetadata(uint32_t codePoint, EpdGlyph& outGlyph);
    bool getGlyphBitmap(uint32_t offset, uint32_t length, uint8_t* outputBuffer);
    EpdFontData* getData() { return m_fontData; }
private:
    EpdFontData* m_fontData;
    std::string m_filePath;
    FsFile m_file;  
    uint32_t m_glyphTableStart;
    uint32_t m_glyphCount;
    uint32_t m_bitmapDataStart;
    std::vector<uint8_t> m_tableCache;
};