/**
 * @file JpegRenderer.cpp
 * @brief Definitions for JpegRenderer.
 */

#include "JpegRenderer.h"

#include <GfxRenderer.h>
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

// Precomputed RGB565 -> grayscale table (same concept as TJpg_Decoder path).
static uint8_t sGrayFrom565[65536];
static bool sGrayFrom565Init = false;

inline void ensureGrayFrom565Table() {
  if (sGrayFrom565Init) return;
  for (uint32_t i = 0; i < 65536; i++) {
    const uint8_t r5 = static_cast<uint8_t>((i >> 11) & 0x1F);
    const uint8_t g6 = static_cast<uint8_t>((i >> 5) & 0x3F);
    const uint8_t b5 = static_cast<uint8_t>(i & 0x1F);
    const uint8_t r8 = static_cast<uint8_t>((r5 * 255u) / 31u);
    const uint8_t g8 = static_cast<uint8_t>((g6 * 255u) / 63u);
    const uint8_t b8 = static_cast<uint8_t>((b5 * 255u) / 31u);
    sGrayFrom565[i] = static_cast<uint8_t>((r8 * 77u + g8 * 150u + b8 * 29u) >> 8);
  }
  sGrayFrom565Init = true;
}

inline uint8_t grayFromRgbTable(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t rgb565 =
      static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | ((b & 0xF8u) >> 3));
  return sGrayFrom565[rgb565];
}

}  // namespace

bool JpegRenderer::drawJpeg(FsFile& jpegFile, int x, int y, int targetWidth, int targetHeight, bool cropToFill) const {
  if (!jpegFile || targetWidth <= 0 || targetHeight <= 0 || isUnsupportedJpeg(jpegFile)) {
    return false;
  }
  ensureGrayFrom565Table();
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
      if (scale > 1.0f) scale = 1.0f;
      outWidth = std::max(1, static_cast<int>(std::lround(imageInfo.m_width * scale)));
      outHeight = std::max(1, static_cast<int>(std::lround(imageInfo.m_height * scale)));
    }
    scaleX_fp = static_cast<uint32_t>((static_cast<uint64_t>(cropSrcWidth) << 16) / static_cast<uint32_t>(outWidth));
    scaleY_fp = static_cast<uint32_t>((static_cast<uint64_t>(cropSrcHeight) << 16) / static_cast<uint32_t>(outHeight));
  }

  const int drawOffsetX = x + (targetWidth - outWidth) / 2;
  const int drawOffsetY = y + (targetHeight - outHeight) / 2;
  const int srcYEnd = srcOffsetY + cropSrcHeight;

  uint8_t* mcuRowBuffer = static_cast<uint8_t*>(malloc(static_cast<size_t>(imageInfo.m_width) * imageInfo.m_MCUHeight));
  uint32_t* rowAccum = new (std::nothrow) uint32_t[outWidth]();
  uint16_t* rowCount = new (std::nothrow) uint16_t[outWidth]();
  if (!mcuRowBuffer || !rowAccum || !rowCount) {
    free(mcuRowBuffer);
    delete[] rowAccum;
    delete[] rowCount;
    return false;
  }

  static constexpr uint8_t bayer[4][4] = {
      {15, 195, 60, 240},
      {135, 75, 180, 120},
      {45, 225, 30, 210},
      {165, 105, 150, 90},
  };
  int currentOutY = 0;
  uint32_t nextOutY_srcStart = scaleY_fp;

  for (int mcuY = 0; mcuY < imageInfo.m_MCUSPerCol; mcuY++) {
    for (int mcuX = 0; mcuX < imageInfo.m_MCUSPerRow; mcuX++) {
      if (pjpeg_decode_mcu() != 0) break;
      for (int bY = 0; bY < imageInfo.m_MCUHeight; bY++) {
        for (int bX = 0; bX < imageInfo.m_MCUWidth; bX++) {
          const int pX = mcuX * imageInfo.m_MCUWidth + bX;
          if (pX >= imageInfo.m_width) continue;
          const int off = (bY / 8 * (imageInfo.m_MCUWidth / 8) + bX / 8) * 64 + (bY % 8) * 8 + (bX % 8);
          uint8_t gray = (imageInfo.m_comps == 1) ? imageInfo.m_pMCUBufR[off]
                                                  : grayFromRgbTable(imageInfo.m_pMCUBufR[off], imageInfo.m_pMCUBufG[off],
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
      for (int ox = 0; ox < outWidth; ox++) {
        const int sx = srcOffsetX + static_cast<int>((static_cast<uint64_t>(ox) * scaleX_fp) >> 16);
        if (sx >= srcOffsetX && sx < srcOffsetX + cropSrcWidth && sx < imageInfo.m_width) {
          rowAccum[ox] += srcRow[sx];
          rowCount[ox]++;
        }
      }
      if ((static_cast<uint32_t>(srcY + 1) << 16) >= nextOutY_srcStart && currentOutY < outHeight) {
        const int screenY = drawOffsetY + currentOutY;
        for (int ox = 0; ox < outWidth; ox++) {
          int gray = rowCount[ox] ? static_cast<int>(rowAccum[ox] / rowCount[ox]) : 0;
          if (gray > 180) gray = 255;
          else if (gray < 70) gray = 0;
          const int screenX = drawOffsetX + ox;
          if (gray < bayer[screenX & 3][screenY & 3]) {
            renderer_.drawPixel(screenX, screenY, true);
          }
        }
        currentOutY++;
        memset(rowAccum, 0, static_cast<size_t>(outWidth) * sizeof(uint32_t));
        memset(rowCount, 0, static_cast<size_t>(outWidth) * sizeof(uint16_t));
        nextOutY_srcStart = static_cast<uint32_t>(currentOutY + 1) * scaleY_fp;
      }
    }
  }

  free(mcuRowBuffer);
  delete[] rowAccum;
  delete[] rowCount;
  return currentOutY > 0;
}

bool JpegRenderer::drawJpegFromPath(const std::string& path, int x, int y, int targetWidth, int targetHeight,
                                    bool cropToFill) const {
  FsFile file;
  if (!SdMan.openFileForRead("JRG", path, file)) {
    return false;
  }
  const bool ok = drawJpeg(file, x, y, targetWidth, targetHeight, cropToFill);
  file.close();
  return ok;
}

bool JpegRenderer::getDimensions(FsFile& jpegFile, int* outW, int* outH) {
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

bool JpegRenderer::getDimensions(const std::string& path, int* outW, int* outH) {
  FsFile file;
  if (!SdMan.openFileForRead("JRG", path, file)) {
    return false;
  }
  const bool ok = getDimensions(file, outW, outH);
  file.close();
  return ok;
}

