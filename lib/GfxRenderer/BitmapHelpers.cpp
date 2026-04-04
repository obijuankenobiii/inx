/**
 * @file BitmapHelpers.cpp
 * @brief BMP image processing utilities for e-ink displays
 *
 * Provides grayscale conversion, image scaling, and dithering functionality
 * optimized for e-ink display characteristics.
 */

#include "BitmapHelpers.h"

#include <HardwareSerial.h>
#include <SdFat.h>

#include <cstdint>

#include "SDCardManager.h"

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

/** @brief Brightness offset added to all pixels (0-255 range) */
constexpr int BRIGHTNESS_BOOST = 0;

/** @brief Contrast multiplier (1.0 = no change, <1 reduces contrast, >1 increases) */
constexpr float CONTRAST_FACTOR = 1.1f;

/** @brief Enable gamma correction for midtone brightness */
constexpr bool USE_GAMMA_CORRECTION = false;

/** @brief Enable noise dithering for 4-level output */
constexpr bool USE_NOISE_DITHERING = true;

/**
 * @brief Convert RGB to grayscale using precomputed BT.601 coefficients
 *
 * @param r Red channel (0-255)
 * @param g Green channel (0-255)
 * @param b Blue channel (0-255)
 * @return Grayscale value (0-255)
 */
uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b) { return LUT_R[r] + LUT_G[g] + LUT_B[b]; }

/**
 * @brief Apply gamma correction to improve midtone visibility on e-ink
 *
 * Uses shadows-only gamma that preserves light grays (above 180) unchanged.
 * This prevents highlight blowout while brightening dark areas.
 *
 * @param gray Input grayscale value (0-255)
 * @return Gamma-corrected grayscale value (0-255)
 */
[[maybe_unused]] static inline int applyGamma(int gray) {
  if (gray > 180) {
    return gray;
  }

  const int product = gray * 255;
  int x = gray;
  if (x > 0) {
    x = (x + product / x) >> 1;
    x = (x + product / x) >> 1;
  }
  return x > 180 ? 180 : x;
}

/**
 * @brief Apply contrast adjustment around the midpoint (128)
 *
 * @param gray Input grayscale value (0-255)
 * @return Contrast-adjusted grayscale value (0-255)
 */
static inline int applyContrast(int gray) {
  constexpr int factorNum = static_cast<int>(CONTRAST_FACTOR * 100);
  int adjusted = ((gray - 128) * factorNum) / 100 + 128;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;
  return adjusted;
}

/**
 * @brief Apply full image adjustment pipeline (contrast, brightness, gamma)
 *
 * @param gray Input grayscale value (0-255)
 * @return Adjusted grayscale value optimized for e-ink display
 */
int adjustPixel(int gray) {
  gray = applyContrast(gray);
  gray += BRIGHTNESS_BOOST;
  if (gray > 255) gray = 255;
  if (gray < 0) gray = 0;
  if (USE_GAMMA_CORRECTION) {
    gray = applyGamma(gray);
  }
  return gray;
}

/**
 * @brief Simple 4-level quantization without dithering
 *
 * Thresholds are tuned for e-ink displays to maximize visible detail.
 *
 * @param gray Grayscale value (0-255)
 * @return Quantized level (0-3)
 */
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

/**
 * @brief Noise dithering for 4-level output using hash-based thresholds
 *
 * Uses integer hash to generate pseudo-random thresholds per pixel,
 * avoiding moiré patterns when downsampling.
 *
 * @param gray Grayscale value (0-255)
 * @param x X coordinate for hash generation
 * @param y Y coordinate for hash generation
 * @return Dithered quantized level (0-3)
 */
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

/**
 * @brief Main quantization function with configurable dithering
 *
 * @param gray Grayscale value (0-255)
 * @param x X coordinate (used for dithering)
 * @param y Y coordinate (used for dithering)
 * @return Quantized level (0-3)
 */
uint8_t quantize(int gray, int x, int y) {
  if (USE_NOISE_DITHERING) {
    return quantizeNoise(gray, x, y);
  } else {
    return quantizeSimple(gray);
  }
}

/**
 * @brief Simple 1-bit quantization (black or white)
 *
 * @param gray Grayscale value (0-255)
 * @param x X coordinate (unused, kept for API consistency)
 * @param y Y coordinate (unused, kept for API consistency)
 * @return 0 for black, 1 for white
 */
uint8_t quantize1bit(int gray, int x, int y) { return gray < 128 ? 0 : 1; }

/** @brief Read 16-bit little-endian value from file */
static uint16_t readLE16(FsFile& f) {
  const int c0 = f.read();
  const int c1 = f.read();
  return static_cast<uint16_t>(c0 & 0xFF) | (static_cast<uint16_t>(c1 & 0xFF) << 8);
}

/** @brief Read 32-bit little-endian value from file */
static uint32_t readLE32(FsFile& f) {
  const int c0 = f.read();
  const int c1 = f.read();
  const int c2 = f.read();
  const int c3 = f.read();
  return static_cast<uint32_t>(c0 & 0xFF) | (static_cast<uint32_t>(c1 & 0xFF) << 8) |
         (static_cast<uint32_t>(c2 & 0xFF) << 16) | (static_cast<uint32_t>(c3 & 0xFF) << 24);
}

/** @brief Write 16-bit little-endian value to output */
static void writeLE16(Print& out, uint16_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
}

/** @brief Write 32-bit little-endian value to output */
static void writeLE32(Print& out, uint32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

/** @brief Write signed 32-bit little-endian value to output */
static void writeLE32Signed(Print& out, int32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

/**
 * @brief Write 1-bit BMP file header
 *
 * @param out Output stream
 * @param width Image width in pixels
 * @param height Image height in pixels
 */
static void writeBmpHeader1bit(Print& out, int width, int height) {
  const int bytesPerRow = (width + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 62 + imageSize;

  out.write('B');
  out.write('M');
  writeLE32(out, fileSize);
  writeLE32(out, 0);
  writeLE32(out, 62);

  writeLE32(out, 40);
  writeLE32Signed(out, width);
  writeLE32Signed(out, -height);
  writeLE16(out, 1);
  writeLE16(out, 1);
  writeLE32(out, 0);
  writeLE32(out, imageSize);
  writeLE32(out, 2835);
  writeLE32(out, 2835);
  writeLE32(out, 2);
  writeLE32(out, 2);

  const uint8_t palette[8] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  out.write(palette, 8);
}

/**
 * @brief Convert 2-bit palette index to grayscale
 *
 * @param index Palette index (0-3)
 * @return Grayscale value
 */
static inline uint8_t palette2bitToGray(uint8_t index) {
  static const uint8_t lut[4] = {0, 85, 170, 255};
  return lut[index & 0x03];
}

/**
 * @brief Convert 1-bit palette index to grayscale
 *
 * @param index Palette index (0-1)
 * @return Grayscale value (0 or 255)
 */
static inline uint8_t palette1bitToGray(uint8_t index) { return (index & 0x01) ? 255 : 0; }

/**
 * @brief Atkinson dithering for 1-bit output without additional contrast adjustment
 *
 * This ditherer is designed for sources that are already contrast-enhanced
 * (like cover.bmp) and should not have adjustPixel() applied.
 */
class RawAtkinson1BitDitherer {
 public:
  /**
   * @brief Construct ditherer for given width
   *
   * @param width Image width in pixels
   */
  explicit RawAtkinson1BitDitherer(int width) : width(width) {
    errorRow0 = new int16_t[width + 4]();
    errorRow1 = new int16_t[width + 4]();
    errorRow2 = new int16_t[width + 4]();
  }

  /** @brief Destructor - cleanup error buffers */
  ~RawAtkinson1BitDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }

  RawAtkinson1BitDitherer(const RawAtkinson1BitDitherer&) = delete;
  RawAtkinson1BitDitherer& operator=(const RawAtkinson1BitDitherer&) = delete;

  /**
   * @brief Process a single pixel with Atkinson dithering
   *
   * @param gray Grayscale value (0-255)
   * @param x X coordinate in the output image
   * @return 1-bit value (0 or 1)
   */
  uint8_t processPixel(int gray, int x) {
    int adjusted = gray + errorRow0[x + 2];
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    uint8_t quantized;
    int quantizedValue;
    if (adjusted < 128) {
      quantized = 0;
      quantizedValue = 0;
    } else {
      quantized = 1;
      quantizedValue = 255;
    }

    int error = (adjusted - quantizedValue) >> 3;
    errorRow0[x + 3] += error;
    errorRow0[x + 4] += error;
    errorRow1[x + 1] += error;
    errorRow1[x + 2] += error;
    errorRow1[x + 3] += error;
    errorRow2[x + 2] += error;

    return quantized;
  }

  /** @brief Advance to next row, shifting error buffers */
  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};

/**
 * @brief Scale a 1-bit or 2-bit BMP to a 1-bit thumbnail
 *
 * Reads a BMP file, scales it to fit within target dimensions while maintaining
 * aspect ratio, applies Atkinson dithering, and writes a 1-bit BMP output.
 *
 * @param srcPath Source BMP file path
 * @param dstPath Destination BMP file path
 * @param targetMaxWidth Maximum output width in pixels
 * @param targetMaxHeight Maximum output height in pixels
 * @return true on success, false on failure
 */
bool bmpTo1BitBmpScaled(const char* srcPath, const char* dstPath, int targetMaxWidth, int targetMaxHeight) {
  FsFile srcFile;
  if (!SdMan.openFileForRead("BMP", srcPath, srcFile)) {
    Serial.printf("[%lu] [BMP] Failed to open source: %s\n", millis(), srcPath);
    return false;
  }

  if (readLE16(srcFile) != 0x4D42) {
    Serial.printf("[%lu] [BMP] Not a BMP file\n", millis());
    srcFile.close();
    return false;
  }

  srcFile.seekCur(8);
  const uint32_t pixelOffset = readLE32(srcFile);
  const uint32_t dibSize = readLE32(srcFile);

  if (dibSize < 40) {
    Serial.printf("[%lu] [BMP] Unsupported DIB header\n", millis());
    srcFile.close();
    return false;
  }

  const int srcWidth = static_cast<int32_t>(readLE32(srcFile));
  const int32_t rawHeight = static_cast<int32_t>(readLE32(srcFile));

  if (rawHeight >= 0) {
    Serial.printf("[%lu] [BMP] Bottom-up BMP not supported, expected top-down\n", millis());
    srcFile.close();
    return false;
  }
  const int srcHeight = -rawHeight;

  srcFile.seekCur(2);
  const uint16_t bpp = readLE16(srcFile);

  if (bpp != 1 && bpp != 2) {
    Serial.printf("[%lu] [BMP] Expected 1 or 2-bit BMP, got %d-bit\n", millis(), bpp);
    srcFile.close();
    return false;
  }

  Serial.printf("[%lu] [BMP] Scaling %dx%d %d-bit BMP to 1-bit thumbnail\n", millis(), srcWidth, srcHeight, bpp);

  int outWidth = srcWidth;
  int outHeight = srcHeight;

  if (srcWidth > targetMaxWidth || srcHeight > targetMaxHeight) {
    const float scaleW = static_cast<float>(targetMaxWidth) / srcWidth;
    const float scaleH = static_cast<float>(targetMaxHeight) / srcHeight;
    const float scale = (scaleW < scaleH) ? scaleW : scaleH;
    outWidth = static_cast<int>(srcWidth * scale);
    outHeight = static_cast<int>(srcHeight * scale);
    if (outWidth < 1) outWidth = 1;
    if (outHeight < 1) outHeight = 1;
  }

  const uint32_t scaleX_fp = (static_cast<uint32_t>(srcWidth) << 16) / outWidth;
  const uint32_t scaleY_fp = (static_cast<uint32_t>(srcHeight) << 16) / outHeight;
  const int maxSrcRowsPerOut = ((scaleY_fp + 0xFFFF) >> 16) + 1;

  Serial.printf("[%lu] [BMP] Output: %dx%d, scale_fp: %lu x %lu\n", millis(), outWidth, outHeight,
                static_cast<unsigned long>(scaleX_fp), static_cast<unsigned long>(scaleY_fp));

  const int srcRowBytes = (srcWidth * bpp + 31) / 32 * 4;
  const int outRowBytes = (outWidth + 31) / 32 * 4;

  auto* srcRows = static_cast<uint8_t*>(malloc(srcRowBytes * maxSrcRowsPerOut));
  auto* outRow = static_cast<uint8_t*>(malloc(outRowBytes));

  if (!srcRows || !outRow) {
    Serial.printf("[%lu] [BMP] Failed to allocate buffers\n", millis());
    free(srcRows);
    free(outRow);
    srcFile.close();
    return false;
  }

  FsFile dstFile;
  if (!SdMan.openFileForWrite("BMP", dstPath, dstFile)) {
    Serial.printf("[%lu] [BMP] Failed to open destination: %s\n", millis(), dstPath);
    free(srcRows);
    free(outRow);
    srcFile.close();
    return false;
  }

  writeBmpHeader1bit(dstFile, outWidth, outHeight);
  RawAtkinson1BitDitherer ditherer(outWidth);

  if (!srcFile.seek(pixelOffset)) {
    Serial.printf("[%lu] [BMP] Failed to seek to pixel data\n", millis());
    free(srcRows);
    free(outRow);
    srcFile.close();
    dstFile.close();
    return false;
  }

  int lastSrcRowRead = -1;

  for (int outY = 0; outY < outHeight; outY++) {
    const int srcYStart = (static_cast<uint32_t>(outY) * scaleY_fp) >> 16;
    int srcYEnd = (static_cast<uint32_t>(outY + 1) * scaleY_fp) >> 16;
    if (srcYEnd <= srcYStart) srcYEnd = srcYStart + 1;
    const int srcRowsNeeded = srcYEnd - srcYStart;

    for (int srcY = srcYStart; srcY < srcYEnd && srcY < srcHeight; srcY++) {
      if (srcY <= lastSrcRowRead) continue;

      while (lastSrcRowRead < srcY - 1) {
        srcFile.seekCur(srcRowBytes);
        lastSrcRowRead++;
      }

      const int bufferSlot = srcY - srcYStart;
      if (srcFile.read(srcRows + bufferSlot * srcRowBytes, srcRowBytes) != srcRowBytes) {
        Serial.printf("[%lu] [BMP] Failed to read row %d\n", millis(), srcY);
        free(srcRows);
        free(outRow);
        srcFile.close();
        dstFile.close();
        return false;
      }
      lastSrcRowRead = srcY;
    }

    memset(outRow, 0, outRowBytes);

    for (int outX = 0; outX < outWidth; outX++) {
      const int srcXStart = (static_cast<uint32_t>(outX) * scaleX_fp) >> 16;
      int srcXEnd = (static_cast<uint32_t>(outX + 1) * scaleX_fp) >> 16;
      if (srcXEnd <= srcXStart) srcXEnd = srcXStart + 1;

      int sum = 0;
      int count = 0;

      for (int dy = 0; dy < srcRowsNeeded && (srcYStart + dy) < srcHeight; dy++) {
        const uint8_t* row = srcRows + dy * srcRowBytes;
        for (int srcX = srcXStart; srcX < srcXEnd && srcX < srcWidth; srcX++) {
          uint8_t gray;
          if (bpp == 2) {
            const int byteIdx = srcX / 4;
            const int bitShift = 6 - (srcX % 4) * 2;
            const uint8_t pixel = (row[byteIdx] >> bitShift) & 0x03;
            gray = palette2bitToGray(pixel);
          } else {
            const int byteIdx = srcX / 8;
            const int bitOffset = 7 - (srcX % 8);
            const uint8_t pixel = (row[byteIdx] >> bitOffset) & 0x01;
            gray = palette1bitToGray(pixel);
          }
          sum += gray;
          count++;
        }
      }

      const uint8_t gray = (count > 0) ? (sum / count) : 0;
      const uint8_t bit = ditherer.processPixel(gray, outX);

      const int byteIdx = outX / 8;
      const int bitOffset = 7 - (outX % 8);
      outRow[byteIdx] |= (bit << bitOffset);
    }

    ditherer.nextRow();
    dstFile.write(outRow, outRowBytes);
  }

  free(srcRows);
  free(outRow);
  srcFile.close();
  dstFile.close();

  Serial.printf("[%lu] [BMP] Successfully created thumbnail: %s\n", millis(), dstPath);
  return true;
}
