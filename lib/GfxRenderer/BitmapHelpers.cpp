#include "BitmapHelpers.h"

#include <cstdint>
#include <cstring>  // Added for memset

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

// Brightness/Contrast adjustments:
constexpr bool USE_BRIGHTNESS = false;       // true: apply brightness/gamma adjustments
constexpr int BRIGHTNESS_BOOST = 10;         // Brightness offset (0-50)
constexpr bool GAMMA_CORRECTION = false;     // Gamma curve (brightens midtones)
constexpr float CONTRAST_FACTOR = 1.15f;     // Contrast multiplier (1.0 = no change, >1 = more contrast)
constexpr bool USE_NOISE_DITHERING = false;  // Hash-based noise dithering

// Integer approximation of gamma correction (brightens midtones)
// Uses a simple curve: out = 255 * sqrt(in/255) ≈ sqrt(in * 255)
static inline int applyGamma(int gray) {
  if (!GAMMA_CORRECTION) return gray;
  // Fast integer square root approximation for gamma ~0.5 (brightening)
  // This brightens dark/mid tones while preserving highlights
  const int product = gray * 255;
  // Newton-Raphson integer sqrt (2 iterations for good accuracy)
  int x = gray;
  if (x > 0) {
    x = (x + product / x) >> 1;
    x = (x + product / x) >> 1;
  }
  return x > 255 ? 255 : x;
}

// Apply contrast adjustment around midpoint (128)
// factor > 1.0 increases contrast, < 1.0 decreases
static inline int applyContrast(int gray) {
  // Integer-based contrast: (gray - 128) * factor + 128
  // Using fixed-point: factor 1.15 ≈ 115/100
  constexpr int factorNum = static_cast<int>(CONTRAST_FACTOR * 100);
  int adjusted = ((gray - 128) * factorNum) / 100 + 128;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;
  return adjusted;
}
// Combined brightness/contrast/gamma adjustment
int adjustPixel(int gray) {
  if (!USE_BRIGHTNESS) return gray;

  // Order: contrast first, then brightness, then gamma
  gray = applyContrast(gray);
  gray += BRIGHTNESS_BOOST;
  if (gray > 255) gray = 255;
  if (gray < 0) gray = 0;
  gray = applyGamma(gray);

  return gray;
}
// Simple quantization without dithering - divide into 4 levels
// The thresholds are fine-tuned to the X4 display
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

// Hash-based noise dithering - survives downsampling without moiré artifacts
// Uses integer hash to generate pseudo-random threshold per pixel
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

// Main quantization function - selects between methods based on config
uint8_t quantize(int gray, int x, int y) {
  if (USE_NOISE_DITHERING) {
    return quantizeNoise(gray, x, y);
  } else {
    return quantizeSimple(gray);
  }
}

// 1-bit noise dithering for fast home screen rendering
// Uses hash-based noise for consistent dithering that works well at small sizes
uint8_t quantize1bit(int gray, int x, int y) {
  gray = adjustPixel(gray);

  // Generate noise threshold using integer hash (no regular pattern to alias)
  uint32_t hash = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  hash = (hash ^ (hash >> 13)) * 1274126177u;
  const int threshold = static_cast<int>(hash >> 24);  // 0-255

  // Simple threshold with noise: gray >= (128 + noise offset) -> white
  // The noise adds variation around the 128 midpoint
  const int adjustedThreshold = 128 + ((threshold - 128) / 2);  // Range: 64-192
  return (gray >= adjustedThreshold) ? 1 : 0;
}

void createBmpHeader(BmpHeader* bmpHeader, int width, int height, BmpRowOrder rowOrder) {
  if (!bmpHeader) return;

  // Zero out the memory to ensure no garbage data if called on uninitialized stack memory
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
  bmpHeader->infoHeader.biXPelsPerMeter = 2835;  // 72 DPI
  bmpHeader->infoHeader.biYPelsPerMeter = 2835;  // 72 DPI
  bmpHeader->infoHeader.biClrUsed = 2;
  bmpHeader->infoHeader.biClrImportant = 2;

  // Color 0 (black)
  bmpHeader->colors[0].rgbBlue = 0;
  bmpHeader->colors[0].rgbGreen = 0;
  bmpHeader->colors[0].rgbRed = 0;
  bmpHeader->colors[0].rgbReserved = 0;

  // Color 1 (white)
  bmpHeader->colors[1].rgbBlue = 255;
  bmpHeader->colors[1].rgbGreen = 255;
  bmpHeader->colors[1].rgbRed = 255;
  bmpHeader->colors[1].rgbReserved = 0;
}