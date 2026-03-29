#include "PngToBmpConverter.h"
#include <PNGdec.h>
#include <algorithm>

static PNG png;
static bool debugEnabled = false; 

struct ConvertState {
    uint8_t* bitmap;
    int sw, sh, dw, dh, bpr;
};

static ConvertState* g_state = nullptr;

// --- PNGdec Callbacks ---

static int32_t pngRead(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen) { 
    FsFile* file = (FsFile*)pFile->fHandle;
    return (file) ? file->read(pBuf, iLen) : -1;
}

static int32_t pngSeek(PNGFILE *pFile, int32_t iPosition) { 
    FsFile* file = (FsFile*)pFile->fHandle;
    return (file && file->seek(iPosition)) ? file->position() : -1;
}

static void* pngOpen(const char *szFilename, int32_t *pFileSize) {
    FsFile* file = (FsFile*)szFilename;
    if (!file) return nullptr;
    *pFileSize = file->size();
    file->seek(0);
    return (void*)file;
}

static void pngClose(void *pHandle) {}

// --- High Quality Bayer Dither Callback ---

int PNGDrawCallback(PNGDRAW *pDraw) {
    if (!g_state) return 0;
    
    uint16_t line[pDraw->iWidth];
    png.getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);

    int outY = (pDraw->y * g_state->dh) / g_state->sh;
    if (outY >= g_state->dh) return 1;

    // 4x4 Bayer Matrix (Values 0-255)
    static const uint8_t bayer[4][4] = {
        {  15, 135,  45, 165 },
        { 195,  75, 225, 105 },
        {  60, 180,  30, 150 },
        { 240, 120, 210,  90 }
    };

    for (int x = 0; x < pDraw->iWidth; x++) {
        int outX = (x * g_state->dw) / g_state->sw;
        if (outX >= g_state->dw) continue;

        uint16_t p = line[x];
        
        // Robust RGB to Gray (0-255)
        // Extract 5-6-5 bits and scale to 8-bit
        int r = ((p >> 11) & 0x1F) << 3; 
        int g = ((p >> 5) & 0x3F) << 2;
        int b = (p & 0x1F) << 3;
        
        // Luminance formula: Y = 0.299R + 0.587G + 0.114B
        int gray = (r * 77 + g * 150 + b * 29) >> 8;

        // If pixel is LIGHTER than threshold, set bit to 1 (Index 1: WHITE)
        if (gray > bayer[outX % 4][outY % 4]) {
            g_state->bitmap[outY * g_state->bpr + (outX / 8)] |= (0x80 >> (outX % 8));
        }
        // Else stays 0 (Index 0: BLACK)
    }
    return 1;
}

// --- Class Implementation ---

bool PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetW, int targetH) {
    uint32_t originalPos = pngFile.position();
    pngFile.seek(0);
    
    int rc = png.open((const char*)&pngFile, pngOpen, pngClose, pngRead, pngSeek, PNGDrawCallback);
    if (rc != 0) {
        pngFile.seek(originalPos);
        return false;
    }
    
    int sw = png.getWidth();
    int sh = png.getHeight();
    int dw = (targetW > 0) ? targetW : sw;
    int dh = (targetH > 0) ? targetH : sh;
    
    // BMP scanlines must be padded to 4-byte boundaries
    int bpr = (dw + 31) / 32 * 4;
    size_t dataSize = (size_t)bpr * dh;
    
    uint8_t* buf = (uint8_t*)malloc(dataSize);
    if (!buf) {
        png.close();
        return false;
    }
    memset(buf, 0, dataSize); // Initialize as all BLACK (Index 0)

    ConvertState state = { buf, sw, sh, dw, dh, bpr };
    g_state = &state;
    
    rc = png.decode(NULL, 0);
    png.close();
    
    if (rc == 0) {
        // --- BMP Header (54 bytes) ---
        uint8_t header[54] = {0};
        header[0] = 'B'; header[1] = 'M';
        
        uint32_t offset = 62; // 54 (Header) + 8 (2-color Palette)
        uint32_t fileSize = offset + dataSize;
        
        memcpy(&header[2], &fileSize, 4);
        header[10] = 62;      // Data offset
        header[14] = 40;      // Info Header size
        memcpy(&header[18], &dw, 4);
        int32_t negHeight = -dh; // Top-down BMP
        memcpy(&header[22], &negHeight, 4);
        header[26] = 1;       // Planes
        header[28] = 1;       // Bit count (1-bit)
        memcpy(&header[34], &dataSize, 4);
        
        uint32_t colors = 2;
        memcpy(&header[46], &colors, 4); // Colors Used
        memcpy(&header[50], &colors, 4); // Important Colors
        
        bmpOut.write(header, 54);
        
        // --- Palette (8 bytes) ---
        // Index 0: Black, Index 1: White
        uint8_t pal[8] = {
            0,   0,   0,   0,   // [0] B, G, R, Alpha
            255, 255, 255, 0    // [1] B, G, R, Alpha
        };
        bmpOut.write(pal, 8);
        
        bmpOut.write(buf, dataSize);
    }
    
    free(buf);
    g_state = nullptr;
    pngFile.seek(originalPos);
    return (rc == 0);
}

bool PngToBmpConverter::pngFileTo1BitBmpStream(FsFile& pngFile, Print& bmpOut) {
    return pngFileTo1BitBmpStreamWithSize(pngFile, bmpOut, 0, 0);
}

bool PngToBmpConverter::pngFileTo1BitBmpStreamCentered(FsFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight) {
    return pngFileTo1BitBmpStreamWithSize(pngFile, bmpOut, targetWidth, targetHeight);
}