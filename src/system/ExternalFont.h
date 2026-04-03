#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "SDCardManager.h"
#include "EpdFontData.h"

// Forward declaration
class EpdFontFamily;

class ExternalFont {
public:
    ExternalFont();
    ~ExternalFont();
    
    bool load(const char* path);
    void unload();
    
    // Returns the metadata structure used by the renderer
    EpdFontData* getData() { return m_fontData; }
    size_t getMemoryUsage() const { return m_metadataSize; }
    
    // Fetches a specific chunk of the bitmap from the SD card
    bool getGlyphBitmap(uint32_t offset, uint32_t length, uint8_t* outputBuffer);

private:
    bool loadMetadata(const char* path);
    bool readGlyphData(uint32_t offset, uint32_t length, uint8_t* buffer);
    
    EpdFontData* m_fontData;
    EpdGlyph* m_glyphs;
    EpdUnicodeInterval* m_intervals;
    std::string m_filePath;
    size_t m_metadataSize;
    
    // FsFile is a stack object in SdFat, not a pointer
    FsFile m_file;  
};