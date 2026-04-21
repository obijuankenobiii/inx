#include "PngToBmpConverter.h"

#include <BitmapHelpers.h>
#include <PNGdec.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

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

    const int lw = pDraw->iWidth;
    if (lw <= 0) return 1;
    uint16_t* line = static_cast<uint16_t*>(malloc(static_cast<size_t>(lw) * sizeof(uint16_t)));
    if (!line) return 0;
    png.getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);

    int outY = (pDraw->y * g_state->dh) / g_state->sh;
    if (outY >= g_state->dh) {
        free(line);
        return 1;
    }

    // 4x4 Bayer Matrix (Values 0-255)
    static const uint8_t bayer[4][4] = {
        {  15, 135,  45, 165 },
        { 195,  75, 225, 105 },
        {  60, 180,  30, 150 },
        { 240, 120, 210,  90 }
    };

    for (int x = 0; x < lw; x++) {
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
    free(line);
    return 1;
}

// --- Class Implementation ---

bool PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetW, int targetH,
                                                       bool cropToFill) {
    uint32_t originalPos = pngFile.position();
    pngFile.seek(0);
    
    int rc = png.open((const char*)&pngFile, pngOpen, pngClose, pngRead, pngSeek, PNGDrawCallback);
    if (rc != 0) {
        pngFile.seek(originalPos);
        return false;
    }
    
    int sw = png.getWidth();
    int sh = png.getHeight();
    int dw;
    int dh;
    if (targetW > 0 && targetH > 0) {
        if (cropToFill) {
            dw = targetW;
            dh = targetH;
        } else {
            float sx = static_cast<float>(targetW) / static_cast<float>(sw);
            float sy = static_cast<float>(targetH) / static_cast<float>(sh);
            float s = std::min(sx, sy);
            if (s > 1.0f) {
                s = 1.0f;
            }
            dw = std::max(1, static_cast<int>(std::lround(static_cast<float>(sw) * s)));
            dh = std::max(1, static_cast<int>(std::lround(static_cast<float>(sh) * s)));
        }
    } else {
        dw = sw;
        dh = sh;
    }
    
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

bool PngToBmpConverter::pngFileTo1BitBmpStreamCentered(FsFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight,
                                                       bool cropToFill) {
    return pngFileTo1BitBmpStreamWithSize(pngFile, bmpOut, targetWidth, targetHeight, cropToFill);
}

namespace {

struct PngThumbGrayState {
  uint8_t* gray;
  int sw;
  int sh;
  int dw;
  int dh;
};

static PngThumbGrayState* g_pngThumbGray = nullptr;

static inline void pngThumbWrite16(Print& out, uint16_t v) {
  out.write(static_cast<uint8_t>(v & 0xFF));
  out.write(static_cast<uint8_t>((v >> 8) & 0xFF));
}

static inline void pngThumbWrite32(Print& out, uint32_t v) {
  out.write(static_cast<uint8_t>(v & 0xFF));
  out.write(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.write(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.write(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static inline void pngThumbWrite32Signed(Print& out, int32_t v) {
  pngThumbWrite32(out, static_cast<uint32_t>(v));
}

static void writeBmpHeader2BitThumb(Print& bmpOut, int width, int height) {
  const int bytesPerRow = (width * 2 + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  bmpOut.write('B');
  bmpOut.write('M');
  pngThumbWrite32(bmpOut, 70 + static_cast<uint32_t>(imageSize));
  pngThumbWrite32(bmpOut, 0);
  pngThumbWrite32(bmpOut, 70);
  pngThumbWrite32(bmpOut, 40);
  pngThumbWrite32Signed(bmpOut, width);
  pngThumbWrite32Signed(bmpOut, -height);
  pngThumbWrite16(bmpOut, 1);
  pngThumbWrite16(bmpOut, 2);
  pngThumbWrite32(bmpOut, 0);
  pngThumbWrite32(bmpOut, static_cast<uint32_t>(imageSize));
  pngThumbWrite32(bmpOut, 2835);
  pngThumbWrite32(bmpOut, 2835);
  pngThumbWrite32(bmpOut, 4);
  pngThumbWrite32(bmpOut, 4);
  static const uint8_t kPal[16] = {0, 0, 0, 0, 85, 85, 85, 0, 170, 170, 170, 0, 255, 255, 255, 0};
  for (size_t i = 0; i < sizeof(kPal); i++) {
    bmpOut.write(kPal[i]);
  }
}

int PNGDrawCallbackThumbGray(PNGDRAW* pDraw) {
  if (!g_pngThumbGray || !g_pngThumbGray->gray) {
    return 0;
  }
  const int lw = pDraw->iWidth;
  if (lw <= 0) return 1;
  uint16_t* line = static_cast<uint16_t*>(malloc(static_cast<size_t>(lw) * sizeof(uint16_t)));
  if (!line) return 0;
  png.getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);
  PngThumbGrayState* st = g_pngThumbGray;
  for (int x = 0; x < lw; x++) {
    const int outX = (x * st->dw) / st->sw;
    const int outY = (pDraw->y * st->dh) / st->sh;
    if (outX < 0 || outX >= st->dw || outY < 0 || outY >= st->dh) {
      continue;
    }
    const uint16_t p = line[x];
    const int r = ((p >> 11) & 0x1F) << 3;
    const int g = ((p >> 5) & 0x3F) << 2;
    const int b = (p & 0x1F) << 3;
    const int gray = (r * 77 + g * 150 + b * 29) >> 8;
    st->gray[outY * st->dw + outX] = static_cast<uint8_t>(gray);
  }
  free(line);
  return 1;
}

}  // namespace

struct PngEpubWebDrawState {
  Print* bmpOut = nullptr;
  EpubWeb2BitRowPacker* packer = nullptr;
  int sw = 0;
  int sh = 0;
  int dw = 0;
  int dh = 0;
  uint8_t* grayDw = nullptr;
  int oy = 0;
  int lastMappedSy = -1;
  bool haveCachedDownsample = false;
};

static PngEpubWebDrawState* g_pngEpubWeb = nullptr;

static int PNGDrawCallbackEpubWeb(PNGDRAW* pDraw) {
  if (!g_pngEpubWeb || !g_pngEpubWeb->bmpOut || !g_pngEpubWeb->packer || !g_pngEpubWeb->grayDw) {
    return 0;
  }
  PngEpubWebDrawState* st = g_pngEpubWeb;
  if (st->oy >= st->dh) {
    return 0;
  }

  const int sy = (st->dh <= 1) ? 0 : std::min(st->sh - 1, (st->oy * st->sh) / st->dh);
  if (pDraw->y < sy) {
    return 1;
  }
  if (pDraw->y > sy) {
    return 0;
  }

  const int lw = pDraw->iWidth;
  if (lw <= 0) {
    return 1;
  }
  uint16_t* line = static_cast<uint16_t*>(malloc(static_cast<size_t>(lw) * sizeof(uint16_t)));
  if (!line) {
    return 0;
  }
  png.getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);

  if (!(st->haveCachedDownsample && st->lastMappedSy == sy)) {
    for (int ox = 0; ox < st->dw; ox++) {
      const int sx = (st->dw <= 1) ? 0 : std::min(st->sw - 1, (ox * st->sw) / st->dw);
      st->grayDw[ox] = epubWebRgb565ToGray8Rounded(line[sx]);
    }
    st->haveCachedDownsample = true;
    st->lastMappedSy = sy;
  }
  free(line);

  while (st->oy < st->dh) {
    const int syNeed = (st->dh <= 1) ? 0 : std::min(st->sh - 1, (st->oy * st->sh) / st->dh);
    if (syNeed != pDraw->y) {
      break;
    }
    if (!st->packer->writeGrayRow(*st->bmpOut, st->grayDw)) {
      return 0;
    }
    st->oy++;
  }

  return (st->oy >= st->dh) ? 0 : 1;
}

bool PngToBmpConverter::pngFileTo2BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetW, int targetH,
                                                      bool cropToFill) {
  const uint32_t originalPos = pngFile.position();
  pngFile.seek(0);

  int rc = png.open((const char*)&pngFile, pngOpen, pngClose, pngRead, pngSeek, PNGDrawCallbackThumbGray);
  if (rc != 0) {
    pngFile.seek(originalPos);
    return false;
  }

  const int sw = png.getWidth();
  const int sh = png.getHeight();
  int dw = 0;
  int dh = 0;
  if (targetW > 0 && targetH > 0) {
    if (cropToFill) {
      dw = targetW;
      dh = targetH;
    } else {
      const float sx = static_cast<float>(targetW) / static_cast<float>(sw);
      const float sy = static_cast<float>(targetH) / static_cast<float>(sh);
      float s = std::min(sx, sy);
      if (s > 1.0f) {
        s = 1.0f;
      }
      dw = std::max(1, static_cast<int>(std::lround(static_cast<float>(sw) * s)));
      dh = std::max(1, static_cast<int>(std::lround(static_cast<float>(sh) * s)));
    }
  } else {
    dw = sw;
    dh = sh;
  }

  const size_t grayBytes = static_cast<size_t>(dw) * static_cast<size_t>(dh);
  uint8_t* gray = static_cast<uint8_t*>(malloc(grayBytes));
  if (!gray) {
    png.close();
    pngFile.seek(originalPos);
    return false;
  }
  memset(gray, 0, grayBytes);

  PngThumbGrayState st;
  st.gray = gray;
  st.sw = sw;
  st.sh = sh;
  st.dw = dw;
  st.dh = dh;
  g_pngThumbGray = &st;
  rc = png.decode(nullptr, 0);
  png.close();
  g_pngThumbGray = nullptr;

  if (rc != 0) {
    free(gray);
    pngFile.seek(originalPos);
    return false;
  }

  writeBmpHeader2BitThumb(bmpOut, dw, dh);
  const int bytesPerRow = (dw * 2 + 31) / 32 * 4;
  uint8_t* rowBuffer = static_cast<uint8_t*>(malloc(static_cast<size_t>(bytesPerRow)));
  auto* errorBuffer = static_cast<int16_t*>(malloc(static_cast<size_t>(dw) * 2 * sizeof(int16_t)));
  if (!rowBuffer || !errorBuffer) {
    free(rowBuffer);
    free(errorBuffer);
    free(gray);
    pngFile.seek(originalPos);
    return false;
  }
  memset(errorBuffer, 0, static_cast<size_t>(dw) * 2 * sizeof(int16_t));

  for (int y = 0; y < dh; y++) {
    memset(rowBuffer, 0, static_cast<size_t>(bytesPerRow));
    int16_t* currentError = &errorBuffer[(y & 1) * dw];
    int16_t* nextError = &errorBuffer[((y + 1) & 1) * dw];
    if (y < dh - 1) {
      memset(nextError, 0, static_cast<size_t>(dw) * sizeof(int16_t));
    }

    const uint8_t* srcRow = gray + y * dw;
    for (int x = 0; x < dw; x++) {
      int16_t corrected = static_cast<int16_t>(srcRow[x]) + currentError[x];
      if (corrected < 0) {
        corrected = 0;
      }
      if (corrected > 255) {
        corrected = 255;
      }

      uint8_t twoBit = 0;
      uint8_t quantized = 0;
      if (corrected < 48) {
        twoBit = 0;
        quantized = 0;
      } else if (corrected < 112) {
        twoBit = 1;
        quantized = 85;
      } else if (corrected < 192) {
        twoBit = 2;
        quantized = 170;
      } else {
        twoBit = 3;
        quantized = 255;
      }

      const int16_t error = static_cast<int16_t>(corrected - quantized);
      if (x < dw - 1) {
        currentError[x + 1] += static_cast<int16_t>((error * 7) / 16);
      }
      nextError[x] += static_cast<int16_t>((error * 5) / 16);
      if (x > 0) {
        nextError[x - 1] += static_cast<int16_t>((error * 3) / 16);
      }
      if (x < dw - 1) {
        nextError[x + 1] += static_cast<int16_t>((error * 1) / 16);
      }

      rowBuffer[(x * 2) / 8] |= static_cast<uint8_t>(twoBit << (6 - ((x * 2) % 8)));
    }
    bmpOut.write(rowBuffer, static_cast<size_t>(bytesPerRow));
  }

  free(errorBuffer);
  free(rowBuffer);
  free(gray);
  pngFile.seek(originalPos);
  return true;
}

bool PngToBmpConverter::pngFileToEpubWebStyle2BitBmpStream(FsFile& pngFile, Print& bmpOut) {
  const uint32_t originalPos = pngFile.position();
  pngFile.seek(0);

  int rc = png.open((const char*)&pngFile, pngOpen, pngClose, pngRead, pngSeek, PNGDrawCallbackEpubWeb);
  if (rc != 0) {
    pngFile.seek(originalPos);
    return false;
  }

  const int sw = png.getWidth();
  const int sh = png.getHeight();
  if (sw <= 0 || sh <= 0) {
    png.close();
    pngFile.seek(originalPos);
    return false;
  }

  int dw = 0;
  int dh = 0;
  epubWebContainDimensionsFloor(sw, sh, 500, 820, &dw, &dh);

  epubWebWrite2BitBmpHeader(bmpOut, dw, dh);

  uint8_t* grayDw = static_cast<uint8_t*>(malloc(static_cast<size_t>(dw)));
  EpubWeb2BitRowPacker packer;
  if (!grayDw || !packer.init(dw)) {
    free(grayDw);
    packer.freeBuffers();
    png.close();
    pngFile.seek(originalPos);
    return false;
  }

  PngEpubWebDrawState st;
  st.bmpOut = &bmpOut;
  st.packer = &packer;
  st.sw = sw;
  st.sh = sh;
  st.dw = dw;
  st.dh = dh;
  st.grayDw = grayDw;
  st.oy = 0;
  st.lastMappedSy = -1;
  st.haveCachedDownsample = false;

  g_pngEpubWeb = &st;
  rc = png.decode(nullptr, 0);
  png.close();
  g_pngEpubWeb = nullptr;

  packer.freeBuffers();
  free(grayDw);
  pngFile.seek(originalPos);
  return (rc == 0) && (st.oy == dh);
}