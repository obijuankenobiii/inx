#pragma once

/**
 * @file BitmapUtil.h
 * @brief Public interface and types for BitmapUtil.
 */

#include <cstdint>
#include <cstring>

#include "ImageToneDither.h"

class Print;
class GfxRenderer;

struct BmpHeader;

uint8_t quantize(int gray, int x, int y);
uint8_t quantizeSimple(int gray);
ImageToneSample quantizeTwoBitImage(int gray);
uint8_t adjustTwoBitImageLevelForDisplay(uint8_t level);
uint8_t mapQualityGray2Level(uint8_t level, bool deviceIsX3);

// Shared by JpegRender/PngRender/ImageRender's quality (GRAY2) pixel-plane selection: given an
// already-computed display level (post adjustTwoBitImageLevelForDisplay), true if the current
// bit-plane should have this pixel set. `renderModeValue` is `static_cast<uint8_t>(GfxRenderer::RenderMode)`
// (kept as uint8_t here to avoid a GfxRenderer.h header dependency); only GRAY2_LSB/GRAY2_MSB return true,
// every other render mode returns false since this helper is only meaningful during a quality pass.
bool qualityGray2PixelSet(uint8_t level, uint8_t renderModeValue, bool deviceIsX3);

// Captures per-pixel quantized levels (post adjustTwoBitImageLevelForDisplay, values 0-3) during a
// TwoBit decode so a second render call for the opposite GRAY2 bit-plane can replay them without
// re-decoding. Caller allocates `levels` with capacity >= targetWidth*targetHeight (the upper bound of
// what the renderer could produce, since the actual output always fits within the requested target
// box). JpegRender/PngRender::render() fill in the actual output geometry and capture per-pixel levels
// using outWidth as the row stride. `captured` is left false (and levels untouched) if capacity is
// insufficient or capture isn't applicable (e.g. mode != TwoBit).
struct ImageLevelCapture {
  uint8_t* levels = nullptr;
  int capacity = 0;
  int outWidth = 0;
  int outHeight = 0;
  int drawOffsetX = 0;
  int drawOffsetY = 0;
  bool captured = false;
};

// Replays a previously captured level buffer for the given GRAY2 bit-plane, skipping decode entirely.
void replayImageLevelCapture(const ImageLevelCapture& capture, const GfxRenderer& renderer, bool deviceIsX3);

// Image grayscale-content analysis. While active, every 2-bit image level that passes through
// adjustTwoBitImageLevelForDisplay() (i.e. every rendered image pixel, for JPEG/PNG/BMP alike) is tallied.
// Render an image in 2-bit mode between begin/end, then query the mid-gray fraction to decide whether it has
// continuous tone worth grayscale (vs essentially 1-bit comic/line art).
void beginImageLevelAnalysis();
void endImageLevelAnalysis();
uint32_t imageLevelAnalysisMidPercent();  // % of sampled pixels at mid-gray levels (1 or 2); 0 if none sampled
uint8_t quantize1bit(int gray, int x, int y);
int adjustOneBitPixel(int gray);
int adjustPixel(int gray);

uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b);

bool bmpTo1BitBmpScaled(const char* srcPath, const char* dstPath, int targetMaxWidth, int targetMaxHeight);

enum class BmpRowOrder { BottomUp, TopDown };

void createBmpHeader(BmpHeader* bmpHeader, int width, int height, BmpRowOrder rowOrder);

class Atkinson1BitDitherer {
 public:
  explicit Atkinson1BitDitherer(int width) : width(width) {
    errorRow0 = new int16_t[width + 4]();
    errorRow1 = new int16_t[width + 4]();
    errorRow2 = new int16_t[width + 4]();
  }

  ~Atkinson1BitDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }

  Atkinson1BitDitherer(const Atkinson1BitDitherer& other) = delete;

  Atkinson1BitDitherer& operator=(const Atkinson1BitDitherer& other) = delete;

  uint8_t processPixel(int gray, int x) {
    gray = adjustOneBitPixel(gray);

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

  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};

uint8_t epubWebRgb565ToGray8Rounded(uint16_t rgb565LittleEndian);
void epubWebContainDimensionsFloor(int srcW, int srcH, int maxW, int maxH, int* outW, int* outH);
void epubWebWrite2BitBmpHeader(Print& bmpOut, int width, int height);

struct EpubWeb2BitRowPacker {
  int dw = 0;
  int bytesPerRow = 0;
  uint8_t* rowBuffer = nullptr;
  FourToneImageDitherer* ditherer = nullptr;
  int rowIndex = 0;

  bool init(int width);
  void freeBuffers();
  bool writeGrayRow(Print& bmpOut, const uint8_t* grayRow);
};
