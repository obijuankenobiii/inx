/**
 * @file JpegRender.cpp
 * @brief Definitions for JpegRender.
 */

#include "JpegRender.h"

#include "BitmapUtil.h"
#include "GfxRenderer.h"
#include <SDCardManager.h>
#include <picojpeg.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
struct JpegReadContext {
  FsFile& file;
  uint8_t buffer[512];
  size_t bufferPos;
  size_t bufferFilled;
};

unsigned char jpegReadCallback(unsigned char* pBuf, unsigned char bufSize, unsigned char* pBytesActRead,
                               void* pCallbackData) {
  auto* context = static_cast<JpegReadContext*>(pCallbackData);
  if (!context || !context->file) {
    return PJPG_STREAM_READ_ERROR;
  }
  if (context->bufferPos >= context->bufferFilled) {
    context->bufferFilled = context->file.read(context->buffer, sizeof(context->buffer));
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

bool isUnsupportedJpeg(FsFile& file) {
  const uint32_t originalPos = file.position();
  file.seek(0);
  uint8_t buf[2];
  bool isProgressive = false;
  while (file.read(buf, 1) == 1) {
    if (buf[0] != 0xFF) continue;
    if (file.read(buf, 1) != 1) break;
    while (buf[0] == 0xFF) {
      if (file.read(buf, 1) != 1) break;
    }
    const uint8_t marker = buf[0];
    if (marker == 0xC2 || marker == 0xC9 || marker == 0xCA) {
      isProgressive = true;
      break;
    }
    if (marker == 0xC0 || marker == 0xC1) {
      isProgressive = false;
      break;
    }
    if (marker != 0xD8 && marker != 0xD9 && marker != 0x01 && !(marker >= 0xD0 && marker <= 0xD7)) {
      if (file.read(buf, 2) != 2) break;
      const uint16_t len = (buf[0] << 8) | buf[1];
      if (len < 2) break;
      file.seek(file.position() + len - 2);
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
constexpr int kJpegDitherSolidWhiteMin = 255;  // Changed from 255 - more light grays
constexpr int kJpegTwoBitSolidBlackMax = 10;   // Snap dark tones to clean black instead of dithering them to gray
constexpr int kJpegTwoBitSolidWhiteMin = 224;  // Keep upper mids from blowing out to white too early
constexpr int kJpegTwoBitContrastPercent = 128; // Restore midtone separation closer to the quality render
constexpr int kJpegTwoBitSharpenThreshold = 18;
constexpr int kJpegTwoBitSharpenPercent = 80;
constexpr int kJpegTwoBitSharpenMax = 130;
constexpr int kJpegTwoBitEdgeThreshold = 0;
constexpr int kJpegTwoBitEdgeMaxDarken = 0;    // Reduced from 36
constexpr int kJpegTwoBitHighlightThreshold = 5; // Reduced from 8 - detect more highlights
constexpr int kJpegTwoBitHighlightMaxLift = 50;  // Reduced from 100 - less over-lifting
constexpr int kJpegTwoBitShadowStart = 1;       // Increased from 10
constexpr int kJpegTwoBitShadowMaxDarken = 0;    // Keep at 0 (already is)
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
// X3 quality (GRAY2) shadow lift — X3 renders darker, so the quality curve lifts shadows on X3.
constexpr int kJpegTwoBitQualityX3ShadowLift = 56;
constexpr int kJpegTwoBitQualityMicroDither = 8;

// X3 medium (GRAYSCALE) tone shaping. X3 renders flat/gray, so expand the tonal range instead of just
// lifting: darker blacks + whiter whites (kills the gray cast) and a contrast stretch (kills the
// flatness). Pivot >128 biases it brighter so it doesn't go dark. X3 medium only; quality/X4 untouched.
constexpr int kX3MediumBlackMax = 20;   // output tone <= this -> pure black (lower = keeps more dark gray)
constexpr int kX3MediumWhiteMin = 255;  // output tone >= this -> pure white (higher = keeps more light gray)
constexpr int kX3MediumPivot = 180;     // contrast pivot; higher biases the image brighter
constexpr int kX3MediumContrast = 120;  // % contrast expansion (>100 = punchier / less flat)


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
    const int boost = std::max(-kJpegTwoBitSharpenMax,
                               std::min(kJpegTwoBitSharpenMax, (detail * kJpegTwoBitSharpenPercent) / 100));
    sharpenedGray = std::max(0, std::min(255, gray + boost));
  }

  int tone = jpegTwoBitTone(sharpenedGray);
  if (gray < kJpegTwoBitShadowStart) {
    const int shadowDarken =
        ((kJpegTwoBitShadowStart - gray) * kJpegTwoBitShadowMaxDarken) / kJpegTwoBitShadowStart;
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
    const int boost = std::max(-kJpegTwoBitQualitySharpenMax,
                               std::min(kJpegTwoBitQualitySharpenMax,
                                        (detail * kJpegTwoBitQualitySharpenPercent) / 100));
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
// Per-device JPEG tone. Pick the device function at the call site; tune each independently here.
//   quality == true  -> GRAY2 quality curve
//   quality == false -> medium (GRAYSCALE) detail curve
// ============================================================================================

// X4 reference look (do not lift; shadows are slightly deepened on the quality curve).
int jpegToneX4(const int gray, const int leftGray, const int rightGray, const int x, const int y,
               const bool quality) {
  if (quality) {
    return jpegQualityToneCommon(gray, leftGray, rightGray, x, y, -kJpegTwoBitQualityShadowDarkenMax);
  }
  return jpegTwoBitDetailTone(gray, leftGray, rightGray, x, y);
}

int jpegToneX3(const int gray, const int leftGray, const int rightGray, const int x, const int y,
               const bool quality) {
  // Quality (GRAY2) is unchanged from before: the X4-shared curve with the X3 shadow lift.
  if (quality) {
    return jpegQualityToneCommon(gray, leftGray, rightGray, x, y, kJpegTwoBitQualityX3ShadowLift);
  }

  // Medium (GRAYSCALE): start from the original detail tone, then expand the range so it isn't
  // flat/gray — snap deep tones to black and high tones to white, and stretch contrast around a
  // bright-biased pivot.
  int tone = jpegTwoBitDetailTone(gray, leftGray, rightGray, x, y);
  if (tone <= kX3MediumBlackMax) {
    return 0;
  }
  if (tone >= kX3MediumWhiteMin) {
    return 255;
  }
  tone = ((tone - kX3MediumPivot) * kX3MediumContrast) / 100 + kX3MediumPivot;
  return std::max(0, std::min(255, tone));
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

// X3 fast grayscale does not follow the same effective on-panel order as X4. In particular,
// the old direct plane mapping makes dark gray and black swap places. Keep X3 explicit too.
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

void drawQuantizedPixel(const GfxRenderer& renderer, const int x, const int y, const int q,
                        const ImageRenderMode mode) {
  if (mode == ImageRenderMode::OneBit) {
    if (q == 0) {
      renderer.drawPixel(x, y, true);
    }
    return;
  }

  const uint8_t level = adjustTwoBitImageLevelForDisplay(FourToneImageDitherer::levelFromValue(q));
  const GfxRenderer::RenderMode renderMode = renderer.getRenderMode();
  const uint8_t grayscaleCode =
      (renderer.deviceIsX3() ? kX3GrayscaleCodeForLevel[level & 3] : kGrayscaleCodeForLevel[level & 3]);
  if (renderMode == GfxRenderer::BW) {
    if ((mode == ImageRenderMode::TwoBit && level > 0) ||
        (mode == ImageRenderMode::OneBit && level < 3)) {
      renderer.drawPixel(x, y, true);
    }
  } else if (renderMode == GfxRenderer::GRAYSCALE_MSB &&
             ((grayscaleCode & 0b10) != 0)) {
    renderer.drawPixel(x, y, false);
  } else if (renderMode == GfxRenderer::GRAYSCALE_LSB &&
             ((grayscaleCode & 0b01) != 0)) {
    renderer.drawPixel(x, y, false);
  } else if (renderMode == GfxRenderer::GRAY2_LSB && (level == 0 || level == 2)) {
    renderer.drawPixel(x, y, true);
  } else if (renderMode == GfxRenderer::GRAY2_MSB && (level == 0 || level == 1)) {
    renderer.drawPixel(x, y, true);
  } else if (renderMode == GfxRenderer::GRAY2I_LSB && (level == 1 || level == 3)) {
    renderer.drawPixel(x, y, false);  // white (0x00) base: set bit for dark levels
  } else if (renderMode == GfxRenderer::GRAY2I_MSB && (level == 2 || level == 3)) {
    renderer.drawPixel(x, y, false);
  }
}

}  // namespace

bool JpegRender::render(FsFile& jpegFile, int x, int y, int targetWidth, int targetHeight, bool cropToFill,
                        const ImageRenderMode mode, const bool quality) const {
  if (!jpegFile || targetWidth <= 0 || targetHeight <= 0 || isUnsupportedJpeg(jpegFile)) {
    return false;
  }
  JpegReadContext context = {.file = jpegFile, .bufferPos = 0, .bufferFilled = 0};
  pjpeg_image_info_t imageInfo;
  if (pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0) != 0) {
    return false;
  }

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
  if (!mcuRowBuffer || !scaledRow ||
      (verticalUpscale && (!prevScaledRow || !blendedRow)) || !rowAccum ||
      !rowCount || (mode == ImageRenderMode::TwoBit && (!twoBitDitherer || !twoBitDitherer->ok())) ||
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

  const bool deviceIsX3 = renderer_.deviceIsX3();

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
      const int solidBlackMax =
          mode == ImageRenderMode::TwoBit ? kJpegTwoBitSolidBlackMax : kJpegDitherSolidBlackMax;
      const int solidWhiteMin =
          mode == ImageRenderMode::TwoBit ? kJpegTwoBitSolidWhiteMin : kJpegDitherSolidWhiteMin;
      if (gray <= solidBlackMax) {
        q = 0;
      } else if (gray >= solidWhiteMin) {
        q = 255;
      } else {
        if (mode == ImageRenderMode::TwoBit) {
          const int leftGray = ox > 0 ? row[ox - 1] : gray;
          const int rightGray = ox + 1 < outWidth ? row[ox + 1] : gray;
          const int tone = deviceIsX3
                               ? jpegToneX3(gray, leftGray, rightGray, drawOffsetX + ox, screenY, quality)
                               : jpegToneX4(gray, leftGray, rightGray, drawOffsetX + ox, screenY, quality);
          q = (quality ? twoBitDitherer->processQuality(tone, step)
                       : twoBitDitherer->process(tone, step))
                  .value;
        } else if (oneBitDitherer) {
          q = oneBitDitherer->processPixel(gray, step) ? 255 : 0;
        } else {
          q = quantizeGray(gray, mode);
        }
      }
      drawQuantizedPixel(renderer_, drawOffsetX + ox, screenY, q, mode);
    }
    if (mode == ImageRenderMode::TwoBit) {
      twoBitDitherer->nextRow();
    } else if (oneBitDitherer) {
      oneBitDitherer->nextRow();
    }
  };

  for (int mcuY = 0; mcuY < imageInfo.m_MCUSPerCol; mcuY++) {
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

    for (int yInMcu = 0; yInMcu < imageInfo.m_MCUHeight && (mcuY * imageInfo.m_MCUHeight + yInMcu) < imageInfo.m_height;
         yInMcu++) {
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
  return currentOutY > 0;
}

bool JpegRender::fromPath(const std::string& path, int x, int y, int targetWidth, int targetHeight,
                          bool cropToFill, const ImageRenderMode mode, const bool quality) const {
  FsFile file;
  if (!SdMan.openFileForRead("JRG", path, file)) {
    return false;
  }
  const bool ok = render(file, x, y, targetWidth, targetHeight, cropToFill, mode, quality);
  file.close();
  return ok;
}

bool JpegRender::getDimensions(FsFile& jpegFile, int* outW, int* outH) {
  if (!outW || !outH || !jpegFile) {
    return false;
  }
  *outW = 0;
  *outH = 0;
  const uint32_t pos = jpegFile.position();
  jpegFile.seek(0);
  JpegReadContext context = {.file = jpegFile, .bufferPos = 0, .bufferFilled = 0};
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
