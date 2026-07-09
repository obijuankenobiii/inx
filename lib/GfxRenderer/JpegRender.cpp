/**
 * @file JpegRender.cpp
 * @brief Definitions for JpegRender.
 */

#include "JpegRender.h"

#include <SDCardManager.h>
#include <picojpeg.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "BitmapUtil.h"
#include "GfxRenderer.h"

// The whole firmware builds with -Os (flash-size priority), which leaves real speed on the table for
// this file's per-pixel tone-curve/dither/scale math - the hot loop in every JPEG render. Opting just
// this translation unit's own code into -O2 trades a small amount of flash for meaningfully faster
// decode, without touching the global build flags (and their flash-size risk) for the rest of the
// firmware. Placed after all includes so it doesn't also retroactively apply to inlined header code
// (e.g. SdFat) pulled in above - that caused an attribute-mismatch warning with no benefit.
#pragma GCC optimize("O2")

namespace {
// picojpeg pulls bytes through this callback in small, decoder-driven chunks; without buffering here
// that means one FsFile::read() (a real SD/SPI transaction, tens of ms on a slow card) per chunk. A
// bigger buffer means far fewer of those round trips for the same file. `bufferSize` lets callers pick
// the size: a small stack buffer is enough for getDimensions() (which only reads the header before
// bailing out), while the full decode in render() uses a larger heap buffer (see kJpegDecodeBufferSize).
struct JpegReadContext {
  FsFile& file;
  uint8_t* buffer;
  size_t bufferSize;
  size_t bufferPos;
  size_t bufferFilled;
};

unsigned char jpegReadCallback(unsigned char* pBuf, unsigned char bufSize, unsigned char* pBytesActRead,
                               void* pCallbackData) {
  auto* context = static_cast<JpegReadContext*>(pCallbackData);
  if (!context || !context->file || !context->buffer) {
    return PJPG_STREAM_READ_ERROR;
  }
  if (context->bufferPos >= context->bufferFilled) {
    context->bufferFilled = context->file.read(context->buffer, context->bufferSize);
    context->bufferPos = 0;
    if (context->bufferFilled == 0) {
      *pBytesActRead = 0;
      return 0;
    }
  }
  const size_t available = context->bufferFilled - context->bufferPos;
  const size_t toRead = std::min(static_cast<size_t>(bufSize), available);
  memcpy(pBuf, context->buffer + context->bufferPos, toRead);
  context->bufferPos += toRead;
  *pBytesActRead = static_cast<unsigned char>(toRead);
  return 0;
}

// Scans JPEG marker segments up to SOF looking for a progressive encoding, which the decoder can't
// handle. Reads through a small local buffer instead of one file.read() call per byte - marker
// segments before SOF (APPn/EXIF/etc.) can span hundreds of bytes, and each FsFile::read() call has
// real per-call overhead on top of the underlying SD transaction.
bool isUnsupportedJpeg(FsFile& file) {
  const uint32_t originalPos = file.position();
  file.seek(0);

  uint8_t buf[256];
  size_t bufLen = 0;
  size_t bufPos = 0;
  bool isProgressive = false;

  auto logicalPos = [&]() -> uint32_t { return file.position() - static_cast<uint32_t>(bufLen - bufPos); };
  auto nextByte = [&](uint8_t& out) -> bool {
    if (bufPos >= bufLen) {
      bufLen = file.read(buf, sizeof(buf));
      bufPos = 0;
      if (bufLen == 0) return false;
    }
    out = buf[bufPos++];
    return true;
  };
  auto skip = [&](const uint32_t count) {
    file.seek(logicalPos() + count);
    bufLen = 0;
    bufPos = 0;
  };

  uint8_t b;
  while (nextByte(b)) {
    if (b != 0xFF) continue;
    if (!nextByte(b)) break;
    while (b == 0xFF) {
      if (!nextByte(b)) break;
    }
    const uint8_t marker = b;
    if (marker == 0xC2 || marker == 0xC9 || marker == 0xCA) {
      isProgressive = true;
      break;
    }
    if (marker == 0xC0 || marker == 0xC1) {
      isProgressive = false;
      break;
    }
    if (marker != 0xD8 && marker != 0xD9 && marker != 0x01 && !(marker >= 0xD0 && marker <= 0xD7)) {
      uint8_t lenHi;
      uint8_t lenLo;
      if (!nextByte(lenHi) || !nextByte(lenLo)) break;
      const uint16_t len = (static_cast<uint16_t>(lenHi) << 8) | lenLo;
      if (len < 2) break;
      skip(len - 2);
    }
  }
  file.seek(originalPos);
  return isProgressive;
}

inline uint8_t grayFromRgb(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint8_t>((77 * static_cast<int>(r) + 150 * static_cast<int>(g) + 29 * static_cast<int>(b) + 128) >>
                              8);
}

constexpr int kJpegDitherSolidBlackMax = 20;
constexpr int kJpegDitherSolidWhiteMin = 255;    // Changed from 255 - more light grays
constexpr int kJpegTwoBitSolidBlackMax = 10;     // Snap dark tones to clean black instead of dithering them to gray
constexpr int kJpegTwoBitSolidWhiteMin = 224;    // Keep upper mids from blowing out to white too early
constexpr int kJpegTwoBitContrastPercent = 128;  // Restore midtone separation closer to the quality render
constexpr int kJpegTwoBitSharpenThreshold = 18;
constexpr int kJpegTwoBitSharpenPercent = 80;
constexpr int kJpegTwoBitSharpenMax = 130;
constexpr int kJpegTwoBitEdgeThreshold = 0;
constexpr int kJpegTwoBitEdgeMaxDarken = 0;       // Reduced from 36
constexpr int kJpegTwoBitHighlightThreshold = 5;  // Reduced from 8 - detect more highlights
constexpr int kJpegTwoBitHighlightMaxLift = 50;   // Reduced from 100 - less over-lifting
constexpr int kJpegTwoBitShadowStart = 1;         // Increased from 10
constexpr int kJpegTwoBitShadowMaxDarken = 0;     // Keep at 0 (already is)
constexpr int kJpegTwoBitShadowTextureLiftMin = 52;
constexpr int kJpegTwoBitShadowTextureLiftMax = 126;
constexpr int kJpegTwoBitShadowTextureLift = 6;
constexpr int kJpegTwoBitMidtoneLiftMin = 104;
constexpr int kJpegTwoBitMidtoneLiftMax = 188;
constexpr int kJpegTwoBitMidtoneLift = 8;
constexpr int kJpegTwoBitMediumMixStart = 96;
constexpr int kJpegTwoBitMediumMixFull = 148;
constexpr int kJpegTwoBitMediumMixDetailMin = 2;
constexpr int kJpegTwoBitMediumMixDetailFull = 28;
constexpr int kJpegTwoBitQualitySolidBlackMax = 12;
constexpr int kJpegTwoBitQualitySolidWhiteMin = 218;
constexpr int kJpegTwoBitQualityContrastPercent = 162;
constexpr int kJpegTwoBitQualityShadowContrastPercent = 122;
constexpr int kJpegTwoBitQualitySharpenThreshold = 3;
constexpr int kJpegTwoBitQualitySharpenPercent = 105;
constexpr int kJpegTwoBitQualitySharpenMax = 38;
constexpr int kJpegTwoBitQualityShadowKnee = 96;
constexpr int kJpegTwoBitQualityShadowDarkenMax = 6;
constexpr int kJpegTwoBitQualityMicroDither = 8;

int jpegTwoBitTone(const int gray) {
  const int adjusted = ((gray - 128) * kJpegTwoBitContrastPercent) / 100 + 128;
  return std::max(0, std::min(255, adjusted));
}

int jpegTwoBitDetailTone(const int gray, const int leftGray, const int rightGray, const int x, const int y) {
  const int neighbor = (leftGray + rightGray) / 2;
  const int detail = gray - neighbor;
  const int darkEdge = neighbor - gray;
  const int lightEdge = gray - neighbor;
  int sharpenedGray = gray;
  if (std::abs(detail) > kJpegTwoBitSharpenThreshold) {
    const int boost =
        std::max(-kJpegTwoBitSharpenMax, std::min(kJpegTwoBitSharpenMax, (detail * kJpegTwoBitSharpenPercent) / 100));
    sharpenedGray = std::max(0, std::min(255, gray + boost));
  }

  int tone = jpegTwoBitTone(sharpenedGray);
  if (gray < kJpegTwoBitShadowStart) {
    const int shadowDarken = ((kJpegTwoBitShadowStart - gray) * kJpegTwoBitShadowMaxDarken) / kJpegTwoBitShadowStart;
    tone = std::max(0, tone - shadowDarken);
  }
  if (lightEdge > kJpegTwoBitHighlightThreshold) {
    const int lift = std::min(kJpegTwoBitHighlightMaxLift, (lightEdge - kJpegTwoBitHighlightThreshold) * 3);
    tone = std::max(tone, jpegTwoBitTone(std::min(255, gray + lift)));
  }
  if (darkEdge > kJpegTwoBitEdgeThreshold) {
    const int edgeDarken = std::min(kJpegTwoBitEdgeMaxDarken, darkEdge - kJpegTwoBitEdgeThreshold);
    tone = std::max(0, tone - edgeDarken);
  }
  if (gray >= kJpegTwoBitShadowTextureLiftMin && gray <= kJpegTwoBitShadowTextureLiftMax) {
    tone = std::min(255, tone + kJpegTwoBitShadowTextureLift);
  }
  if (gray >= kJpegTwoBitMidtoneLiftMin && gray <= kJpegTwoBitMidtoneLiftMax) {
    tone = std::min(255, tone + kJpegTwoBitMidtoneLift);
  }
  return std::max(0, std::min(255, tone));
}

// Shared quality (GRAY2) curve. `shadowLiftPerKnee` is applied across the shadow knee: positive
// lifts shadows (X3, which renders darker), negative darkens them (X4 reference).
int jpegQualityToneCommon(const int gray, const int leftGray, const int rightGray, const int x, const int y,
                          const int shadowLiftPerKnee) {
  if (gray <= kJpegTwoBitQualitySolidBlackMax) {
    return 0;
  }
  if (gray >= kJpegTwoBitQualitySolidWhiteMin) {
    return 255;
  }

  const int neighbor = (leftGray + rightGray) / 2;
  const int detail = gray - neighbor;
  int sharpenedGray = gray;
  if (std::abs(detail) > kJpegTwoBitQualitySharpenThreshold) {
    const int boost =
        std::max(-kJpegTwoBitQualitySharpenMax,
                 std::min(kJpegTwoBitQualitySharpenMax, (detail * kJpegTwoBitQualitySharpenPercent) / 100));
    sharpenedGray = std::max(0, std::min(255, gray + boost));
  }

  int tone;
  if (sharpenedGray < 128) {
    tone = ((sharpenedGray - 64) * kJpegTwoBitQualityShadowContrastPercent) / 100 + 64;
  } else {
    tone = ((sharpenedGray - 128) * kJpegTwoBitQualityContrastPercent) / 100 + 128;
  }
  if (gray < kJpegTwoBitQualityShadowKnee) {
    const int kneeDepth = kJpegTwoBitQualityShadowKnee - gray;
    tone += (kneeDepth * shadowLiftPerKnee) / kJpegTwoBitQualityShadowKnee;
  }

  if (tone <= 8) {
    return 0;
  }
  if (tone >= 238) {
    return 255;
  }

  if (gray > kJpegTwoBitQualitySolidBlackMax + 10 && gray < kJpegTwoBitQualitySolidWhiteMin - 10) {
    // Tiny ordered bias keeps soft art texture from collapsing into a single flat gray band.
    const int latticeA = ((x * 13 + y * 7 + ((x ^ y) * 3)) & 15) - 8;
    const int latticeB = (((x + y * 3) * 5) & 7) - 4;
    tone += ((latticeA + latticeB) * kJpegTwoBitQualityMicroDither) / 12;
  }

  return std::max(0, std::min(255, tone));
}

// ============================================================================================
// JPEG tone curve, shared by both devices (X3 and X4). Any visual difference between the two panels
// is handled entirely by their waveform data (lut_x4_quality/lut_grayscale for X4, the
// lut_x3_*_img/lut_x3_*_gray tables for X3), not by device-specific software tone mapping.
//   quality == true  -> GRAY2 quality curve
//   quality == false -> medium (GRAYSCALE) detail curve
// ============================================================================================
int jpegTone(const int gray, const int leftGray, const int rightGray, const int x, const int y, const bool quality) {
  if (quality) {
    return jpegQualityToneCommon(gray, leftGray, rightGray, x, y, -kJpegTwoBitQualityShadowDarkenMax);
  }
  return jpegTwoBitDetailTone(gray, leftGray, rightGray, x, y);
}

// MEDIUM grayscale image-level -> lut_grayscale entry (code). Bit0 = LSB plane (BW RAM, cmd 0x24),
// Bit1 = MSB plane (RED RAM, cmd 0x26); the 2-bit value is the LUT entry index (0b00..0b11).
// Image levels: 0 = white (lightest), 2 = light gray, 1 = dark gray, 3 = black (darkest).
// Your lut_grayscale: 0b00 = black, 0b01 = light gray, 0b10 = medium gray, 0b11 = dark gray.
// EDIT these 4 values to match what each tone should look like on the panel.
// On-panel brightness order is 00 (lightest) -> 11 -> 10 -> 01 (darkest) - the entries render
// inverse to their drive-bit labels. Map image tones to that real order:
// No-flicker lut_grayscale on-panel brightness order: 00 (lightest) -> 11 -> 10 -> 01 (darkest).
// Map image tones to that order. EDIT these 4 values if a shade is off on your panel.
constexpr uint8_t kGrayscaleCodeForLevel[4] = {
    0b00,  // level 0  white      -> entry 00 (lightest)
    0b10,  // level 1  dark gray  -> entry 10
    0b11,  // level 2  light gray -> entry 11
    0b01,  // level 3  black      -> entry 01 (darkest)
};

// Medium/fast (GRAYSCALE) mode uses an opposite bit polarity from quality (GRAY2) mode (this path
// SETs the ink bit, GRAY2 CLEARs it - see the differing drawPixel() state args below), so the level<->
// waveform-table derivation validated for GRAY2's X3 fix does not carry over here without separately
// re-deriving and testing X3's lut_x3_*_gray tables. Left device-specific until that's done, rather
// than risk breaking a mode that wasn't part of the reported issue.
constexpr uint8_t kX3GrayscaleCodeForLevel[4] = {
    0b00,  // level 0  white
    0b11,  // level 1  dark gray
    0b10,  // level 2  light gray
    0b01,  // level 3  black
};

int quantizeGray(const int corrected, const ImageRenderMode mode) {
  if (mode == ImageRenderMode::TwoBit) {
    return quantizeTwoBitImage(corrected).value;
  }
  return corrected < 128 ? 0 : 255;
}

// Two-bit-mode plane decision from an already-resolved dither level (0-3). Split out from
// drawQuantizedPixel() so a captured/replayed render (which stores level directly - see
// JpegLevelCapture) doesn't have to redo the q->level conversion.
void drawPixelForLevel(const GfxRenderer& renderer, const int x, const int y, const uint8_t level) {
  const GfxRenderer::RenderMode renderMode = renderer.getRenderMode();
  const uint8_t grayscaleCode =
      (renderer.deviceIsX3() ? kX3GrayscaleCodeForLevel[level & 3] : kGrayscaleCodeForLevel[level & 3]);
  if (renderMode == GfxRenderer::BW) {
    if (level > 0) {
      renderer.drawPixel(x, y, true);
    }
  } else if (renderMode == GfxRenderer::GRAYSCALE_MSB && ((grayscaleCode & 0b10) != 0)) {
    renderer.drawPixel(x, y, false);
  } else if (renderMode == GfxRenderer::GRAYSCALE_LSB && ((grayscaleCode & 0b01) != 0)) {
    renderer.drawPixel(x, y, false);
  } else if (renderMode == GfxRenderer::GRAY2_LSB && ((mapQualityGray2Level(level) & 0b01) == 0)) {
    renderer.drawPixel(x, y, true);
  } else if (renderMode == GfxRenderer::GRAY2_MSB && ((mapQualityGray2Level(level) & 0b10) == 0)) {
    renderer.drawPixel(x, y, true);
  }
}

void drawQuantizedPixel(const GfxRenderer& renderer, const int x, const int y, const int q,
                        const ImageRenderMode mode) {
  if (mode == ImageRenderMode::OneBit) {
    if (q == 0) {
      renderer.drawPixel(x, y, true);
    }
    return;
  }
  drawPixelForLevel(renderer, x, y, adjustTwoBitImageLevelForDisplay(FourToneImageDitherer::levelFromValue(q)));
}

}  // namespace

bool JpegRender::render(FsFile& jpegFile, int x, int y, int targetWidth, int targetHeight, bool cropToFill,
                        const ImageRenderMode mode, const bool quality, JpegLevelCapture* capture) const {
  const uint32_t tRenderStart = millis();
  if (!jpegFile || targetWidth <= 0 || targetHeight <= 0 || isUnsupportedJpeg(jpegFile)) {
    return false;
  }
  const uint32_t tAfterHeaderScan = millis();
  constexpr size_t kJpegDecodeBufferSize = 4096;
  std::unique_ptr<uint8_t[]> readBuffer(new (std::nothrow) uint8_t[kJpegDecodeBufferSize]);
  if (!readBuffer) {
    return false;
  }
  JpegReadContext context = {jpegFile, readBuffer.get(), kJpegDecodeBufferSize, 0, 0};
  pjpeg_image_info_t imageInfo;
  if (pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0) != 0) {
    return false;
  }
  const uint32_t tAfterInit = millis();

  int outWidth = imageInfo.m_width;
  int outHeight = imageInfo.m_height;
  uint32_t scaleX_fp = 65536;
  uint32_t scaleY_fp = 65536;
  int srcOffsetX = 0;
  int srcOffsetY = 0;
  int cropSrcWidth = imageInfo.m_width;
  int cropSrcHeight = imageInfo.m_height;

  {
    const float sx = static_cast<float>(targetWidth) / static_cast<float>(imageInfo.m_width);
    const float sy = static_cast<float>(targetHeight) / static_cast<float>(imageInfo.m_height);
    if (cropToFill) {
      const float scale = std::max(sx, sy);
      cropSrcWidth = std::max(1, static_cast<int>(targetWidth / scale));
      cropSrcHeight = std::max(1, static_cast<int>(targetHeight / scale));
      srcOffsetX = std::max(0, (imageInfo.m_width - cropSrcWidth) / 2);
      srcOffsetY = std::max(0, (imageInfo.m_height - cropSrcHeight) / 2);
      outWidth = targetWidth;
      outHeight = targetHeight;
    } else {
      float scale = std::min(sx, sy);
      outWidth = std::max(1, static_cast<int>(std::lround(imageInfo.m_width * scale)));
      outHeight = std::max(1, static_cast<int>(std::lround(imageInfo.m_height * scale)));
    }
    scaleX_fp = static_cast<uint32_t>((static_cast<uint64_t>(cropSrcWidth) << 16) / static_cast<uint32_t>(outWidth));
    scaleY_fp = static_cast<uint32_t>((static_cast<uint64_t>(cropSrcHeight) << 16) / static_cast<uint32_t>(outHeight));
  }

  const int drawOffsetX = x + (targetWidth - outWidth) / 2;
  const int drawOffsetY = y + (targetHeight - outHeight) / 2;
  const int srcYEnd = srcOffsetY + cropSrcHeight;
  const bool verticalUpscale = outHeight > cropSrcHeight;
  const bool horizontalUpscale = outWidth > cropSrcWidth;

  const bool captureRequested = capture != nullptr && mode == ImageRenderMode::TwoBit;
  if (captureRequested) {
    const size_t pixelCount = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight);
    const size_t needed = (pixelCount + 3) / 4;  // 2 bits/pixel, 4 pixels/byte
    if (needed > 0 && needed <= capture->capacity) {
      capture->width = outWidth;
      capture->height = outHeight;
      capture->drawOffsetX = drawOffsetX;
      capture->drawOffsetY = drawOffsetY;
      capture->captured = true;
      // Bits are OR'd in below (4 pixels/byte), so start from a clean slate - matters if the decode
      // fails partway (stale bits from an earlier, larger capture) or the buffer is being reused.
      memset(capture->values, 0, needed);
    } else {
      capture = nullptr;  // doesn't fit the caller's buffer - render normally, just skip capturing
    }
  } else {
    capture = nullptr;
  }

  uint8_t* mcuRowBuffer = static_cast<uint8_t*>(malloc(static_cast<size_t>(imageInfo.m_width) * imageInfo.m_MCUHeight));
  uint8_t* scaledRow = static_cast<uint8_t*>(malloc(static_cast<size_t>(outWidth)));
  uint8_t* prevScaledRow = verticalUpscale ? static_cast<uint8_t*>(malloc(static_cast<size_t>(outWidth))) : nullptr;
  uint8_t* blendedRow = verticalUpscale ? static_cast<uint8_t*>(malloc(static_cast<size_t>(outWidth))) : nullptr;
  uint32_t* rowAccum = new (std::nothrow) uint32_t[outWidth]();
  uint16_t* rowCount = new (std::nothrow) uint16_t[outWidth]();
  FourToneImageDitherer* twoBitDitherer = nullptr;
  Atkinson1BitDitherer* oneBitDitherer = nullptr;
  if (mode == ImageRenderMode::TwoBit) {
    twoBitDitherer = new (std::nothrow) FourToneImageDitherer(outWidth);
  } else {
    oneBitDitherer = new (std::nothrow) Atkinson1BitDitherer(outWidth);
  }
  if (!mcuRowBuffer || !scaledRow || (verticalUpscale && (!prevScaledRow || !blendedRow)) || !rowAccum || !rowCount ||
      (mode == ImageRenderMode::TwoBit && (!twoBitDitherer || !twoBitDitherer->ok())) ||
      (mode == ImageRenderMode::OneBit && !oneBitDitherer)) {
    free(mcuRowBuffer);
    free(scaledRow);
    free(prevScaledRow);
    free(blendedRow);
    delete[] rowAccum;
    delete[] rowCount;
    delete twoBitDitherer;
    delete oneBitDitherer;
    return false;
  }

  const bool qualityTone = quality;

  int currentOutY = 0;
  uint32_t nextOutY_srcStart = scaleY_fp;
  bool hasPrevScaledRow = false;

  auto buildScaledRow = [&](const uint8_t* srcRow, uint8_t* row) {
    for (int ox = 0; ox < outWidth; ox++) {
      if (horizontalUpscale) {
        const uint32_t srcFp = static_cast<uint32_t>((static_cast<uint64_t>(ox) * scaleX_fp));
        int sx0 = srcOffsetX + static_cast<int>(srcFp >> 16);
        if (sx0 < srcOffsetX) sx0 = srcOffsetX;
        if (sx0 >= srcOffsetX + cropSrcWidth) sx0 = srcOffsetX + cropSrcWidth - 1;
        const int sx1 = std::min(srcOffsetX + cropSrcWidth - 1, sx0 + 1);
        const uint32_t frac = srcFp & 0xFFFFu;
        const uint32_t invFrac = 65536u - frac;
        row[ox] = static_cast<uint8_t>((srcRow[sx0] * invFrac + srcRow[sx1] * frac + 32768u) >> 16);
      } else {
        int sxStart = srcOffsetX + static_cast<int>((static_cast<uint64_t>(ox) * scaleX_fp) >> 16);
        int sxEnd = srcOffsetX + static_cast<int>((static_cast<uint64_t>(ox + 1) * scaleX_fp) >> 16);
        sxStart = std::max(srcOffsetX, std::min(srcOffsetX + cropSrcWidth - 1, sxStart));
        sxEnd = std::max(sxStart + 1, std::min(srcOffsetX + cropSrcWidth, sxEnd));
        uint32_t sum = 0;
        for (int sx = sxStart; sx < sxEnd; sx++) {
          sum += srcRow[sx];
        }
        row[ox] = static_cast<uint8_t>(sum / static_cast<uint32_t>(sxEnd - sxStart));
      }
    }
  };

  auto blendScaledRows = [&](const uint8_t* upper, const uint8_t* lower, uint32_t frac, uint8_t* row) {
    if (frac == 0) {
      memcpy(row, upper, static_cast<size_t>(outWidth));
      return;
    }
    if (frac >= 65536u) {
      memcpy(row, lower, static_cast<size_t>(outWidth));
      return;
    }
    const uint32_t invFrac = 65536u - frac;
    for (int ox = 0; ox < outWidth; ox++) {
      row[ox] = static_cast<uint8_t>((upper[ox] * invFrac + lower[ox] * frac + 32768u) >> 16);
    }
  };

  auto emitOutputRow = [&](const int screenY, const uint8_t* row) {
    for (int step = 0; step < outWidth; step++) {
      const int ox = step;
      const int gray = row[ox];

      int q;
      const int solidBlackMax = mode == ImageRenderMode::TwoBit ? kJpegTwoBitSolidBlackMax : kJpegDitherSolidBlackMax;
      const int solidWhiteMin = mode == ImageRenderMode::TwoBit ? kJpegTwoBitSolidWhiteMin : kJpegDitherSolidWhiteMin;
      if (gray <= solidBlackMax) {
        q = 0;
      } else if (gray >= solidWhiteMin) {
        q = 255;
      } else {
        if (mode == ImageRenderMode::TwoBit) {
          const int leftGray = ox > 0 ? row[ox - 1] : gray;
          const int rightGray = ox + 1 < outWidth ? row[ox + 1] : gray;
          const int tone = jpegTone(gray, leftGray, rightGray, drawOffsetX + ox, screenY, qualityTone);
          q = (qualityTone ? twoBitDitherer->processQuality(tone, step) : twoBitDitherer->process(tone, step)).value;
        } else if (oneBitDitherer) {
          q = oneBitDitherer->processPixel(gray, step) ? 255 : 0;
        } else {
          q = quantizeGray(gray, mode);
        }
      }
      if (mode == ImageRenderMode::TwoBit) {
        const uint8_t level = adjustTwoBitImageLevelForDisplay(FourToneImageDitherer::levelFromValue(q));
        if (capture) {
          const size_t pixelIndex = static_cast<size_t>(screenY - drawOffsetY) * outWidth + ox;
          capture->values[pixelIndex / 4] |= static_cast<uint8_t>((level & 0x3) << ((pixelIndex % 4) * 2));
        }
        drawPixelForLevel(renderer_, drawOffsetX + ox, screenY, level);
      } else if (q == 0) {
        renderer_.drawPixel(drawOffsetX + ox, screenY, true);
      }
    }
    if (mode == ImageRenderMode::TwoBit) {
      twoBitDitherer->nextRow();
    } else if (oneBitDitherer) {
      oneBitDitherer->nextRow();
    }
  };

  uint32_t mcuDecodeMs = 0;
  uint32_t rowProcessMs = 0;
  for (int mcuY = 0; mcuY < imageInfo.m_MCUSPerCol; mcuY++) {
    const uint32_t tMcuStart = millis();
    for (int mcuX = 0; mcuX < imageInfo.m_MCUSPerRow; mcuX++) {
      if (pjpeg_decode_mcu() != 0) break;
      for (int bY = 0; bY < imageInfo.m_MCUHeight; bY++) {
        for (int bX = 0; bX < imageInfo.m_MCUWidth; bX++) {
          const int pX = mcuX * imageInfo.m_MCUWidth + bX;
          if (pX >= imageInfo.m_width) continue;
          const int off = (bY / 8 * (imageInfo.m_MCUWidth / 8) + bX / 8) * 64 + (bY % 8) * 8 + (bX % 8);
          uint8_t gray = (imageInfo.m_comps == 1) ? imageInfo.m_pMCUBufR[off]
                                                  : grayFromRgb(imageInfo.m_pMCUBufR[off], imageInfo.m_pMCUBufG[off],
                                                                imageInfo.m_pMCUBufB[off]);
          mcuRowBuffer[bY * imageInfo.m_width + pX] = gray;
        }
      }
    }
    const uint32_t tMcuEnd = millis();
    mcuDecodeMs += tMcuEnd - tMcuStart;

    for (int yInMcu = 0; yInMcu < imageInfo.m_MCUHeight && (mcuY * imageInfo.m_MCUHeight + yInMcu) < imageInfo.m_height;
         yInMcu++) {
      const uint32_t tRowStart = millis();
      const int srcY = mcuY * imageInfo.m_MCUHeight + yInMcu;
      if (srcY < srcOffsetY || srcY >= srcYEnd) continue;
      const uint8_t* srcRow = mcuRowBuffer + yInMcu * imageInfo.m_width;

      buildScaledRow(srcRow, scaledRow);

      if (verticalUpscale) {
        const int cropY = srcY - srcOffsetY;
        if (!hasPrevScaledRow || cropY == 0) {
          while (currentOutY < outHeight) {
            const uint32_t srcFp = static_cast<uint32_t>(static_cast<uint64_t>(currentOutY) * scaleY_fp);
            if ((srcFp >> 16) > 0) {
              break;
            }
            emitOutputRow(drawOffsetY + currentOutY, scaledRow);
            currentOutY++;
          }
        } else {
          const uint32_t prevBase = static_cast<uint32_t>(cropY - 1) << 16;
          const uint32_t currBase = static_cast<uint32_t>(cropY) << 16;
          while (currentOutY < outHeight) {
            const uint32_t srcFp = static_cast<uint32_t>(static_cast<uint64_t>(currentOutY) * scaleY_fp);
            if (srcFp > currBase) {
              break;
            }
            const uint32_t frac = srcFp <= prevBase ? 0u : std::min<uint32_t>(65536u, srcFp - prevBase);
            blendScaledRows(prevScaledRow, scaledRow, frac, blendedRow);
            emitOutputRow(drawOffsetY + currentOutY, blendedRow);
            currentOutY++;
          }
        }
        memcpy(prevScaledRow, scaledRow, static_cast<size_t>(outWidth));
        hasPrevScaledRow = true;
        rowProcessMs += millis() - tRowStart;
        continue;
      }

      for (int ox = 0; ox < outWidth; ox++) {
        rowAccum[ox] += scaledRow[ox];
        rowCount[ox]++;
      }
      bool emittedOutputRow = false;
      const uint32_t cropRowsSeen = static_cast<uint32_t>(srcY - srcOffsetY + 1);
      while ((cropRowsSeen << 16) >= nextOutY_srcStart && currentOutY < outHeight) {
        const int screenY = drawOffsetY + currentOutY;
        for (int ox = 0; ox < outWidth; ox++) {
          scaledRow[ox] = rowCount[ox] ? static_cast<uint8_t>(rowAccum[ox] / rowCount[ox]) : 0;
        }
        emitOutputRow(screenY, scaledRow);
        currentOutY++;
        emittedOutputRow = true;
        nextOutY_srcStart = static_cast<uint32_t>(currentOutY + 1) * scaleY_fp;
      }
      if (emittedOutputRow) {
        memset(rowAccum, 0, static_cast<size_t>(outWidth) * sizeof(uint32_t));
        memset(rowCount, 0, static_cast<size_t>(outWidth) * sizeof(uint16_t));
      }
      rowProcessMs += millis() - tRowStart;
    }
  }

  while (verticalUpscale && hasPrevScaledRow && currentOutY < outHeight) {
    emitOutputRow(drawOffsetY + currentOutY, prevScaledRow);
    currentOutY++;
  }

  free(mcuRowBuffer);
  free(scaledRow);
  free(prevScaledRow);
  free(blendedRow);
  delete[] rowAccum;
  delete[] rowCount;
  delete twoBitDitherer;
  delete oneBitDitherer;
  const uint32_t tEnd = millis();
  Serial.printf(
      "[%lu] [IMG-TIMING] JPEG %dx%d->%dx%d mode=%d quality=%d capture=%d: headerScan=%lums init=%lums "
      "mcuDecode=%lums rowProcess=%lums decode+draw=%lums total=%lums\n",
      tEnd, imageInfo.m_width, imageInfo.m_height, outWidth, outHeight, static_cast<int>(mode),
      static_cast<int>(quality), capture ? 1 : 0, static_cast<unsigned long>(tAfterHeaderScan - tRenderStart),
      static_cast<unsigned long>(tAfterInit - tAfterHeaderScan), static_cast<unsigned long>(mcuDecodeMs),
      static_cast<unsigned long>(rowProcessMs), static_cast<unsigned long>(tEnd - tAfterInit),
      static_cast<unsigned long>(tEnd - tRenderStart));
  return currentOutY > 0;
}

bool JpegRender::fromPath(const std::string& path, int x, int y, int targetWidth, int targetHeight, bool cropToFill,
                          const ImageRenderMode mode, const bool quality, JpegLevelCapture* capture) const {
  const uint32_t tOpenStart = millis();
  FsFile file;
  if (!SdMan.openFileForRead("JRG", path, file)) {
    return false;
  }
  const uint32_t tOpenEnd = millis();
  const bool ok = render(file, x, y, targetWidth, targetHeight, cropToFill, mode, quality, capture);
  file.close();
  Serial.printf("[%lu] [IMG-TIMING] fromPath %s: open=%lums render=%lums\n", millis(), path.c_str(),
                static_cast<unsigned long>(tOpenEnd - tOpenStart), static_cast<unsigned long>(millis() - tOpenEnd));
  return ok;
}

void JpegRender::replayCapture(const JpegLevelCapture& capture, const ImageRenderMode mode) const {
  if (!capture.captured || !capture.values) {
    return;
  }
  const uint32_t tStart = millis();
  for (int row = 0; row < capture.height; row++) {
    const int screenY = capture.drawOffsetY + row;
    for (int ox = 0; ox < capture.width; ox++) {
      const size_t pixelIndex = static_cast<size_t>(row) * capture.width + ox;
      const uint8_t level = (capture.values[pixelIndex / 4] >> ((pixelIndex % 4) * 2)) & 0x3;
      drawPixelForLevel(renderer_, capture.drawOffsetX + ox, screenY, level);
    }
  }
  Serial.printf("[%lu] [IMG-TIMING] JPEG replay %dx%d: %lums\n", millis(), capture.width, capture.height,
                static_cast<unsigned long>(millis() - tStart));
}

bool JpegRender::getDimensions(FsFile& jpegFile, int* outW, int* outH) {
  if (!outW || !outH || !jpegFile) {
    return false;
  }
  *outW = 0;
  *outH = 0;
  const uint32_t pos = jpegFile.position();
  jpegFile.seek(0);
  uint8_t headerBuf[512];
  JpegReadContext context = {jpegFile, headerBuf, sizeof(headerBuf), 0, 0};
  pjpeg_image_info_t imageInfo;
  const bool ok = pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0) == 0;
  if (ok) {
    *outW = imageInfo.m_width;
    *outH = imageInfo.m_height;
  }
  jpegFile.seek(pos);
  return ok && *outW > 0 && *outH > 0;
}

bool JpegRender::getDimensions(const std::string& path, int* outW, int* outH) {
  FsFile file;
  if (!SdMan.openFileForRead("JRG", path, file)) {
    return false;
  }
  const bool ok = getDimensions(file, outW, outH);
  file.close();
  return ok;
}
