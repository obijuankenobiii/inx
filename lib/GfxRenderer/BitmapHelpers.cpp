/**
 * @file BitmapHelpers.cpp
 * @brief Definitions for BitmapHelpers.
 */

#include "BitmapHelpers.h"

#include <Print.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "Bitmap.h"

/**
 * @brief Precomputed red channel contribution to grayscale (BT.601 coefficients)
 *
 * Values are scaled to avoid floating point: gray = (77*r + 150*g + 29*b) / 256
 * Maximum sum is 253 (not 255) due to integer truncation.
 */
static const uint8_t LUT_R[256] = {
    0,  0,  0,  0,  1,  1,  1,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  8,  8,
    8,  9,  9,  9,  9,  10, 10, 10, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16, 17,
    17, 17, 18, 18, 18, 18, 19, 19, 19, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25,
    26, 26, 26, 27, 27, 27, 27, 28, 28, 28, 29, 29, 29, 30, 30, 30, 30, 31, 31, 31, 32, 32, 32, 33, 33, 33, 33, 34, 34,
    34, 35, 35, 35, 36, 36, 36, 36, 37, 37, 37, 38, 38, 38, 39, 39, 39, 39, 40, 40, 40, 41, 41, 41, 42, 42, 42, 42, 43,
    43, 43, 44, 44, 44, 45, 45, 45, 45, 46, 46, 46, 47, 47, 47, 48, 48, 48, 48, 49, 49, 49, 50, 50, 50, 51, 51, 51, 51,
    52, 52, 52, 53, 53, 53, 54, 54, 54, 54, 55, 55, 55, 56, 56, 56, 57, 57, 57, 57, 58, 58, 58, 59, 59, 59, 60, 60, 60,
    60, 61, 61, 61, 62, 62, 62, 63, 63, 63, 63, 64, 64, 64, 65, 65, 65, 66, 66, 66, 66, 67, 67, 67, 68, 68, 68, 69, 69,
    69, 69, 70, 70, 70, 71, 71, 71, 72, 72, 72, 72, 73, 73, 73, 74, 74, 74, 75, 75, 75, 75, 76, 76};

/**
 * @brief Precomputed green channel contribution to grayscale (BT.601 coefficients)
 */
static const uint8_t LUT_G[256] = {
    0,   0,   1,   1,   2,   2,   3,   4,   4,   5,   5,   6,   7,   7,   8,   8,   9,   10,  10,  11,  11,  12,
    12,  13,  14,  14,  15,  15,  16,  17,  17,  18,  18,  19,  19,  20,  21,  21,  22,  22,  23,  24,  24,  25,
    25,  26,  26,  27,  28,  28,  29,  29,  30,  31,  31,  32,  32,  33,  33,  34,  35,  35,  36,  36,  37,  38,
    38,  39,  39,  40,  41,  41,  42,  42,  43,  43,  44,  45,  45,  46,  46,  47,  48,  48,  49,  49,  50,  50,
    51,  52,  52,  53,  53,  54,  55,  55,  56,  56,  57,  57,  58,  59,  59,  60,  60,  61,  62,  62,  63,  63,
    64,  64,  65,  66,  66,  67,  67,  68,  69,  69,  70,  70,  71,  71,  72,  73,  73,  74,  75,  75,  76,  76,
    77,  78,  78,  79,  79,  80,  80,  81,  82,  82,  83,  83,  84,  85,  85,  86,  86,  87,  87,  88,  89,  89,
    90,  90,  91,  92,  92,  93,  93,  94,  95,  95,  96,  96,  97,  97,  98,  99,  99,  100, 100, 101, 102, 102,
    103, 103, 104, 104, 105, 106, 106, 107, 107, 108, 109, 109, 110, 110, 111, 111, 112, 113, 113, 114, 114, 115,
    116, 116, 117, 117, 118, 118, 119, 120, 120, 121, 121, 122, 123, 123, 124, 124, 125, 125, 126, 127, 127, 128,
    128, 129, 130, 130, 131, 131, 132, 132, 133, 134, 134, 135, 135, 136, 137, 137, 138, 138, 139, 139, 140, 141,
    141, 142, 142, 143, 144, 144, 145, 145, 146, 146, 147, 148, 148, 149};

/**
 * @brief Precomputed blue channel contribution to grayscale (BT.601 coefficients)
 */
static const uint8_t LUT_B[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,
    6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,
    9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13,
    13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 20, 20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23,
    23, 23, 23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 25, 26, 26, 26, 26,
    26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28};

/**
 * @brief Convert RGB to grayscale using precomputed BT.601 coefficients
 *
 * @param r Red channel (0-255)
 * @param g Green channel (0-255)
 * @param b Blue channel (0-255)
 * @return Grayscale value (0-255)
 */
uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b) { return LUT_R[r] + LUT_G[g] + LUT_B[b]; }


constexpr bool USE_BRIGHTNESS = false;       
constexpr int BRIGHTNESS_BOOST = 10;         
constexpr bool GAMMA_CORRECTION = false;     
constexpr float CONTRAST_FACTOR = 1.15f;     
constexpr bool USE_NOISE_DITHERING = false;  



static inline int applyGamma(int gray) {
  if (!GAMMA_CORRECTION) return gray;
  
  
  const int product = gray * 255;
  
  int x = gray;
  if (x > 0) {
    x = (x + product / x) >> 1;
    x = (x + product / x) >> 1;
  }
  return x > 255 ? 255 : x;
}



static inline int applyContrast(int gray) {
  
  
  constexpr int factorNum = static_cast<int>(CONTRAST_FACTOR * 100);
  int adjusted = ((gray - 128) * factorNum) / 100 + 128;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;
  return adjusted;
}

int adjustPixel(int gray) {
  if (!USE_BRIGHTNESS) return gray;

  
  gray = applyContrast(gray);
  gray += BRIGHTNESS_BOOST;
  if (gray > 255) gray = 255;
  if (gray < 0) gray = 0;
  gray = applyGamma(gray);

  return gray;
}


uint8_t quantizeSimple(int gray) {
  if (gray < 45) {
    return 0;
  } else if (gray < 70) {
    return 1;
  } else if (gray < 140) {
    return 2;
  } else {
    return 3;
  }
}



static inline uint8_t quantizeNoise(int gray, int x, int y) {
  uint32_t hash = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  hash = (hash ^ (hash >> 13)) * 1274126177u;
  const int threshold = static_cast<int>(hash >> 24);

  const int scaled = gray * 3;
  if (scaled < 255) {
    return (scaled + threshold >= 255) ? 1 : 0;
  } else if (scaled < 510) {
    return ((scaled - 255) + threshold >= 255) ? 2 : 1;
  } else {
    return ((scaled - 510) + threshold >= 255) ? 3 : 2;
  }
}


uint8_t quantize(int gray, int x, int y) {
  if (USE_NOISE_DITHERING) {
    return quantizeNoise(gray, x, y);
  } else {
    return quantizeSimple(gray);
  }
}



uint8_t quantize1bit(int gray, int x, int y) {
  gray = adjustPixel(gray);

  
  uint32_t hash = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  hash = (hash ^ (hash >> 13)) * 1274126177u;
  const int threshold = static_cast<int>(hash >> 24);  

  
  
  const int adjustedThreshold = 128 + ((threshold - 128) / 2);  
  return (gray >= adjustedThreshold) ? 1 : 0;
}

void createBmpHeader(BmpHeader* bmpHeader, int width, int height, BmpRowOrder rowOrder) {
  if (!bmpHeader) return;

  
  std::memset(bmpHeader, 0, sizeof(BmpHeader));

  uint32_t rowSize = (width + 31) / 32 * 4;
  uint32_t imageSize = rowSize * height;
  uint32_t fileSize = sizeof(BmpHeader) + imageSize;

  bmpHeader->fileHeader.bfType = 0x4D42;
  bmpHeader->fileHeader.bfSize = fileSize;
  bmpHeader->fileHeader.bfReserved1 = 0;
  bmpHeader->fileHeader.bfReserved2 = 0;
  bmpHeader->fileHeader.bfOffBits = sizeof(BmpHeader);

  bmpHeader->infoHeader.biSize = sizeof(bmpHeader->infoHeader);
  bmpHeader->infoHeader.biWidth = width;
  bmpHeader->infoHeader.biHeight = (rowOrder == BmpRowOrder::TopDown) ? -height : height;
  bmpHeader->infoHeader.biPlanes = 1;
  bmpHeader->infoHeader.biBitCount = 1;
  bmpHeader->infoHeader.biCompression = 0;
  bmpHeader->infoHeader.biSizeImage = imageSize;
  bmpHeader->infoHeader.biXPelsPerMeter = 2835;  
  bmpHeader->infoHeader.biYPelsPerMeter = 2835;  
  bmpHeader->infoHeader.biClrUsed = 2;
  bmpHeader->infoHeader.biClrImportant = 2;

  
  bmpHeader->colors[0].rgbBlue = 0;
  bmpHeader->colors[0].rgbGreen = 0;
  bmpHeader->colors[0].rgbRed = 0;
  bmpHeader->colors[0].rgbReserved = 0;

  
  bmpHeader->colors[1].rgbBlue = 255;
  bmpHeader->colors[1].rgbGreen = 255;
  bmpHeader->colors[1].rgbRed = 255;
  bmpHeader->colors[1].rgbReserved = 0;
}

namespace {

inline void epubWebWrite16(Print& out, uint16_t v) {
  out.write(static_cast<uint8_t>(v & 0xFF));
  out.write(static_cast<uint8_t>((v >> 8) & 0xFF));
}

inline void epubWebWrite32(Print& out, uint32_t v) {
  out.write(static_cast<uint8_t>(v & 0xFF));
  out.write(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.write(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.write(static_cast<uint8_t>((v >> 24) & 0xFF));
}

inline void epubWebWrite32Signed(Print& out, int32_t v) { epubWebWrite32(out, static_cast<uint32_t>(v)); }

}  

uint8_t epubWebRgb565ToGray8Rounded(uint16_t p) {
  const int r = ((p >> 11) & 0x1F) << 3;
  const int g = ((p >> 5) & 0x3F) << 2;
  const int b = (p & 0x1F) << 3;
  return static_cast<uint8_t>((r * 77 + g * 150 + b * 29 + 128) / 256);
}

void epubWebContainDimensionsFloor(int srcW, int srcH, int maxW, int maxH, int* outW, int* outH) {
  int dw = srcW;
  int dh = srcH;
  if (srcW > maxW || srcH > maxH) {
    const double sx = static_cast<double>(maxW) / static_cast<double>(srcW);
    const double sy = static_cast<double>(maxH) / static_cast<double>(srcH);
    const double s = std::min(sx, sy);
    dw = std::max(1, static_cast<int>(std::floor(static_cast<double>(srcW) * s)));
    dh = std::max(1, static_cast<int>(std::floor(static_cast<double>(srcH) * s)));
  }
  *outW = dw;
  *outH = dh;
}

void epubWebWrite2BitBmpHeader(Print& bmpOut, int width, int height) {
  const int bytesPerRow = (width * 2 + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  bmpOut.write('B');
  bmpOut.write('M');
  epubWebWrite32(bmpOut, 70u + static_cast<uint32_t>(imageSize));
  epubWebWrite32(bmpOut, 0);
  epubWebWrite32(bmpOut, 70);
  epubWebWrite32(bmpOut, 40);
  epubWebWrite32Signed(bmpOut, width);
  epubWebWrite32Signed(bmpOut, -height);
  epubWebWrite16(bmpOut, 1);
  epubWebWrite16(bmpOut, 2);
  epubWebWrite32(bmpOut, 0);
  epubWebWrite32(bmpOut, static_cast<uint32_t>(imageSize));
  epubWebWrite32(bmpOut, 2835);
  epubWebWrite32(bmpOut, 2835);
  epubWebWrite32(bmpOut, 4);
  epubWebWrite32(bmpOut, 4);
  static const uint8_t kPal[16] = {0, 0, 0, 0, 85, 85, 85, 0, 170, 170, 170, 0, 255, 255, 255, 0};
  for (size_t i = 0; i < sizeof(kPal); i++) {
    bmpOut.write(kPal[i]);
  }
}

bool EpubWeb2BitRowPacker::init(int width) {
  freeBuffers();
  if (width <= 0) return false;
  dw = width;
  bytesPerRow = (dw * 2 + 31) / 32 * 4;
  rowBuffer = static_cast<uint8_t*>(std::calloc(static_cast<size_t>(bytesPerRow), 1));
  errorBuffers = static_cast<int16_t*>(std::calloc(static_cast<size_t>(dw) * 2u, sizeof(int16_t)));
  if (!rowBuffer || !errorBuffers) {
    freeBuffers();
    return false;
  }
  rowIndex = 0;
  return true;
}

void EpubWeb2BitRowPacker::freeBuffers() {
  std::free(rowBuffer);
  rowBuffer = nullptr;
  std::free(errorBuffers);
  errorBuffers = nullptr;
  dw = 0;
  bytesPerRow = 0;
  rowIndex = 0;
}

bool EpubWeb2BitRowPacker::writeGrayRow(Print& bmpOut, const uint8_t* grayRow) {
  if (!rowBuffer || !errorBuffers || !grayRow || dw <= 0) return false;
  std::memset(rowBuffer, 0, static_cast<size_t>(bytesPerRow));
  int16_t* cur = &errorBuffers[(rowIndex & 1) * dw];
  int16_t* nxt = &errorBuffers[((rowIndex + 1) & 1) * dw];
  std::memset(nxt, 0, static_cast<size_t>(dw) * sizeof(int16_t));

  for (int x = 0; x < dw; x++) {
    int16_t corrected = static_cast<int16_t>(grayRow[x]) + cur[x];
    if (corrected < 0) corrected = 0;
    if (corrected > 255) corrected = 255;

    uint8_t twoBit = 0;
    uint8_t quantized = 0;
    if (corrected < 42) {
      twoBit = 0;
      quantized = 0;
    } else if (corrected < 127) {
      twoBit = 1;
      quantized = 85;
    } else if (corrected < 212) {
      twoBit = 2;
      quantized = 170;
    } else {
      twoBit = 3;
      quantized = 255;
    }

    const int16_t err = static_cast<int16_t>(corrected - static_cast<int16_t>(quantized));
    if (x < dw - 1) cur[x + 1] += static_cast<int16_t>((err * 7) / 16);
    nxt[x] += static_cast<int16_t>((err * 5) / 16);
    if (x > 0) nxt[x - 1] += static_cast<int16_t>((err * 3) / 16);
    if (x < dw - 1) nxt[x + 1] += static_cast<int16_t>((err * 1) / 16);

    rowBuffer[(x * 2) / 8] |= static_cast<uint8_t>(twoBit << (6 - ((x * 2) % 8)));
  }

  bmpOut.write(rowBuffer, static_cast<size_t>(bytesPerRow));
  rowIndex++;
  return true;
}