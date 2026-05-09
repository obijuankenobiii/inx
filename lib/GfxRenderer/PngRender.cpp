/**
 * @file PngRender.cpp
 * @brief Definitions for PngRender.
 */

#include "PngRender.h"

#include "GfxRenderer.h"
#include <SDCardManager.h>
#include <miniz.h>

#include <algorithm>
#include <cstring>
#include <new>

namespace {
constexpr int kPngDitherSolidBlackMax = 32;
constexpr int kPngDitherSolidWhiteMin = 255 - kPngDitherSolidBlackMax;
constexpr int kPngTwoBitSolidBlackMax = 3;
constexpr int kPngTwoBitSolidWhiteMin = 252;
constexpr uint8_t kPngSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

enum PngColorType : uint8_t {
  PNG_COLOR_GRAYSCALE = 0,
  PNG_COLOR_RGB = 2,
  PNG_COLOR_PALETTE = 3,
  PNG_COLOR_GRAYSCALE_ALPHA = 4,
  PNG_COLOR_RGBA = 6,
};

struct PngContext {
  mz_stream zstream = {};
  bool zstreamInitialized = false;
  FsFile* file = nullptr;

  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t bitDepth = 0;
  uint8_t colorType = 0;
  uint8_t bytesPerPixel = 0;
  uint32_t rawRowBytes = 0;

  uint8_t* currentRow = nullptr;
  uint8_t* previousRow = nullptr;

  uint32_t chunkBytesRemaining = 0;
  bool idatCrcPending = false;
  bool idatFinished = false;
  uint8_t readBuf[2048];

  uint8_t palette[256 * 3] = {};
  uint8_t paletteAlpha[256] = {};
  int paletteSize = 0;

  bool hasTransparentGray = false;
  bool hasTransparentRgb = false;
  uint16_t transparentGray = 0;
  uint16_t transparentRed = 0;
  uint16_t transparentGreen = 0;
  uint16_t transparentBlue = 0;
};

struct RenderContext {
  const GfxRenderer* renderer;
  int x;
  int y;
  int width;
  ImageRenderMode mode;
  int16_t* errCur;
  int16_t* errNext;
};

bool readBE32(FsFile& file, uint32_t& value) {
  uint8_t buf[4];
  if (file.read(buf, 4) != 4) return false;
  value = (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
          (static_cast<uint32_t>(buf[2]) << 8) | buf[3];
  return true;
}

uint8_t paethPredictor(uint8_t a, uint8_t b, uint8_t c) {
  const int p = static_cast<int>(a) + b - c;
  const int pa = std::abs(p - static_cast<int>(a));
  const int pb = std::abs(p - static_cast<int>(b));
  const int pc = std::abs(p - static_cast<int>(c));
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint8_t>((static_cast<uint32_t>(r) * 77u + static_cast<uint32_t>(g) * 150u +
                               static_cast<uint32_t>(b) * 29u) >>
                              8);
}

uint8_t compositeOverWhite(uint8_t lum, uint8_t alpha) {
  return static_cast<uint8_t>((static_cast<uint16_t>(lum) * alpha + 255u * (255u - alpha)) / 255u);
}

bool findNextIdatChunk(PngContext& ctx) {
  while (true) {
    uint32_t chunkLen;
    if (!readBE32(*ctx.file, chunkLen)) return false;

    uint8_t chunkType[4];
    if (ctx.file->read(chunkType, 4) != 4) return false;

    if (memcmp(chunkType, "IDAT", 4) == 0) {
      ctx.chunkBytesRemaining = chunkLen;
      return true;
    }

    if (!ctx.file->seekCur(chunkLen + 4)) return false;
    if (memcmp(chunkType, "IEND", 4) == 0) return false;
  }
}

bool loadIdatInput(PngContext& ctx) {
  if (ctx.idatFinished) return false;

  while (ctx.chunkBytesRemaining == 0) {
    if (ctx.idatCrcPending) {
      if (!ctx.file->seekCur(4)) {
        ctx.idatFinished = true;
        return false;
      }
      ctx.idatCrcPending = false;
    }

    if (!findNextIdatChunk(ctx)) {
      ctx.idatFinished = true;
      return false;
    }
    ctx.idatCrcPending = true;
  }

  size_t toRead = sizeof(ctx.readBuf);
  if (toRead > ctx.chunkBytesRemaining) toRead = ctx.chunkBytesRemaining;
  const int bytesRead = ctx.file->read(ctx.readBuf, toRead);
  if (bytesRead <= 0) {
    ctx.idatFinished = true;
    return false;
  }

  ctx.chunkBytesRemaining -= static_cast<uint32_t>(bytesRead);
  ctx.zstream.next_in = ctx.readBuf;
  ctx.zstream.avail_in = static_cast<unsigned int>(bytesRead);
  return true;
}

bool inflateRead(PngContext& ctx, uint8_t* dest, size_t len) {
  ctx.zstream.next_out = dest;
  ctx.zstream.avail_out = static_cast<unsigned int>(len);

  while (ctx.zstream.avail_out > 0) {
    if (ctx.zstream.avail_in == 0 && !loadIdatInput(ctx)) return false;

    const unsigned int inBefore = ctx.zstream.avail_in;
    const unsigned int outBefore = ctx.zstream.avail_out;
    const int res = mz_inflate(&ctx.zstream, MZ_NO_FLUSH);
    if (res == MZ_STREAM_END) return ctx.zstream.avail_out == 0;
    if (res != MZ_OK) return false;
    if (ctx.zstream.avail_in == inBefore && ctx.zstream.avail_out == outBefore) return false;
  }

  return true;
}

void releasePng(PngContext& ctx) {
  if (ctx.zstreamInitialized) {
    mz_inflateEnd(&ctx.zstream);
    ctx.zstreamInitialized = false;
  }
  free(ctx.currentRow);
  free(ctx.previousRow);
  ctx.currentRow = nullptr;
  ctx.previousRow = nullptr;
}

bool beginPng(FsFile& pngFile, PngContext& ctx) {
  uint8_t sig[8];
  if (pngFile.read(sig, 8) != 8 || memcmp(sig, kPngSignature, 8) != 0) return false;

  uint32_t ihdrLen;
  if (!readBE32(pngFile, ihdrLen)) return false;
  uint8_t ihdrType[4];
  if (pngFile.read(ihdrType, 4) != 4 || memcmp(ihdrType, "IHDR", 4) != 0 || ihdrLen < 13) return false;

  uint32_t width;
  uint32_t height;
  if (!readBE32(pngFile, width) || !readBE32(pngFile, height)) return false;

  uint8_t ihdrRest[5];
  if (pngFile.read(ihdrRest, 5) != 5) return false;
  const uint8_t bitDepth = ihdrRest[0];
  const uint8_t colorType = ihdrRest[1];
  const uint8_t compression = ihdrRest[2];
  const uint8_t filter = ihdrRest[3];
  const uint8_t interlace = ihdrRest[4];
  pngFile.seekCur(4);

  if (compression != 0 || filter != 0 || interlace != 0 || width == 0 || height == 0 || width > 2048 ||
      height > 3072) {
    return false;
  }

  uint8_t bytesPerPixel = 0;
  uint32_t rawRowBytes = 0;
  switch (colorType) {
    case PNG_COLOR_GRAYSCALE:
      bytesPerPixel = bitDepth == 16 ? 2 : 1;
      rawRowBytes = bitDepth >= 8 ? width * bytesPerPixel : (width * bitDepth + 7) / 8;
      break;
    case PNG_COLOR_RGB:
      bytesPerPixel = bitDepth == 16 ? 6 : 3;
      rawRowBytes = width * bytesPerPixel;
      break;
    case PNG_COLOR_PALETTE:
      bytesPerPixel = 1;
      rawRowBytes = (width * bitDepth + 7) / 8;
      break;
    case PNG_COLOR_GRAYSCALE_ALPHA:
      bytesPerPixel = bitDepth == 16 ? 4 : 2;
      rawRowBytes = width * bytesPerPixel;
      break;
    case PNG_COLOR_RGBA:
      bytesPerPixel = bitDepth == 16 ? 8 : 4;
      rawRowBytes = width * bytesPerPixel;
      break;
    default:
      return false;
  }
  if (rawRowBytes == 0 || rawRowBytes > 16384) return false;

  memset(&ctx, 0, sizeof(ctx));
  ctx.file = &pngFile;
  ctx.width = width;
  ctx.height = height;
  ctx.bitDepth = bitDepth;
  ctx.colorType = colorType;
  ctx.bytesPerPixel = bytesPerPixel;
  ctx.rawRowBytes = rawRowBytes;
  memset(ctx.paletteAlpha, 255, sizeof(ctx.paletteAlpha));

  ctx.currentRow = static_cast<uint8_t*>(malloc(rawRowBytes));
  ctx.previousRow = static_cast<uint8_t*>(calloc(rawRowBytes, 1));
  if (!ctx.currentRow || !ctx.previousRow) {
    releasePng(ctx);
    return false;
  }

  bool foundIdat = false;
  while (!foundIdat) {
    uint32_t chunkLen;
    if (!readBE32(pngFile, chunkLen)) break;
    uint8_t chunkType[4];
    if (pngFile.read(chunkType, 4) != 4) break;

    if (memcmp(chunkType, "PLTE", 4) == 0) {
      int entries = chunkLen / 3;
      if (entries > 256) entries = 256;
      ctx.paletteSize = entries;
      const size_t palBytes = static_cast<size_t>(entries) * 3;
      pngFile.read(ctx.palette, palBytes);
      if (chunkLen > palBytes) pngFile.seekCur(chunkLen - palBytes);
      pngFile.seekCur(4);
    } else if (memcmp(chunkType, "tRNS", 4) == 0) {
      if (colorType == PNG_COLOR_PALETTE) {
        const uint32_t alphaCount = std::min<uint32_t>(chunkLen, sizeof(ctx.paletteAlpha));
        pngFile.read(ctx.paletteAlpha, alphaCount);
        if (chunkLen > alphaCount) pngFile.seekCur(chunkLen - alphaCount);
      } else if (colorType == PNG_COLOR_GRAYSCALE && chunkLen >= 2) {
        uint8_t trns[2];
        pngFile.read(trns, 2);
        ctx.transparentGray = static_cast<uint16_t>(trns[0]) << 8 | trns[1];
        ctx.hasTransparentGray = true;
        if (chunkLen > 2) pngFile.seekCur(chunkLen - 2);
      } else if (colorType == PNG_COLOR_RGB && chunkLen >= 6) {
        uint8_t trns[6];
        pngFile.read(trns, 6);
        ctx.transparentRed = static_cast<uint16_t>(trns[0]) << 8 | trns[1];
        ctx.transparentGreen = static_cast<uint16_t>(trns[2]) << 8 | trns[3];
        ctx.transparentBlue = static_cast<uint16_t>(trns[4]) << 8 | trns[5];
        ctx.hasTransparentRgb = true;
        if (chunkLen > 6) pngFile.seekCur(chunkLen - 6);
      } else {
        pngFile.seekCur(chunkLen);
      }
      pngFile.seekCur(4);
    } else if (memcmp(chunkType, "IDAT", 4) == 0) {
      ctx.chunkBytesRemaining = chunkLen;
      ctx.idatCrcPending = true;
      foundIdat = true;
    } else if (memcmp(chunkType, "IEND", 4) == 0) {
      break;
    } else {
      pngFile.seekCur(chunkLen + 4);
    }
  }

  if (!foundIdat || mz_inflateInit(&ctx.zstream) != MZ_OK) {
    releasePng(ctx);
    return false;
  }
  ctx.zstreamInitialized = true;
  return true;
}

bool decodeScanline(PngContext& ctx) {
  uint8_t filterType;
  if (!inflateRead(ctx, &filterType, 1) || !inflateRead(ctx, ctx.currentRow, ctx.rawRowBytes)) return false;

  const int bpp = ctx.bytesPerPixel;
  switch (filterType) {
    case 0:
      break;
    case 1:
      for (uint32_t i = bpp; i < ctx.rawRowBytes; i++) ctx.currentRow[i] += ctx.currentRow[i - bpp];
      break;
    case 2:
      for (uint32_t i = 0; i < ctx.rawRowBytes; i++) ctx.currentRow[i] += ctx.previousRow[i];
      break;
    case 3:
      for (uint32_t i = 0; i < ctx.rawRowBytes; i++) {
        const uint8_t a = i >= static_cast<uint32_t>(bpp) ? ctx.currentRow[i - bpp] : 0;
        const uint8_t b = ctx.previousRow[i];
        ctx.currentRow[i] += (a + b) / 2;
      }
      break;
    case 4:
      for (uint32_t i = 0; i < ctx.rawRowBytes; i++) {
        const uint8_t a = i >= static_cast<uint32_t>(bpp) ? ctx.currentRow[i - bpp] : 0;
        const uint8_t b = ctx.previousRow[i];
        const uint8_t c = i >= static_cast<uint32_t>(bpp) ? ctx.previousRow[i - bpp] : 0;
        ctx.currentRow[i] += paethPredictor(a, b, c);
      }
      break;
    default:
      return false;
  }
  return true;
}

void convertScanlineToGray(const PngContext& ctx, uint8_t* grayRow) {
  const uint8_t* src = ctx.currentRow;
  const uint32_t w = ctx.width;
  switch (ctx.colorType) {
    case PNG_COLOR_GRAYSCALE:
      if (ctx.bitDepth == 8) {
        for (uint32_t x = 0; x < w; x++) grayRow[x] = (ctx.hasTransparentGray && src[x] == ctx.transparentGray) ? 255 : src[x];
      } else if (ctx.bitDepth == 16) {
        for (uint32_t x = 0; x < w; x++) {
          const uint8_t* p = src + x * 2;
          const uint16_t gray16 = static_cast<uint16_t>(p[0]) << 8 | p[1];
          grayRow[x] = (ctx.hasTransparentGray && gray16 == ctx.transparentGray) ? 255 : p[0];
        }
      } else {
        const int ppb = 8 / ctx.bitDepth;
        const uint8_t mask = (1 << ctx.bitDepth) - 1;
        for (uint32_t x = 0; x < w; x++) {
          const int shift = (ppb - 1 - (x % ppb)) * ctx.bitDepth;
          const uint8_t sample = (src[x / ppb] >> shift) & mask;
          grayRow[x] = (ctx.hasTransparentGray && sample == ctx.transparentGray) ? 255 : sample * 255 / mask;
        }
      }
      break;
    case PNG_COLOR_RGB:
      for (uint32_t x = 0; x < w; x++) {
        const uint8_t* p = src + x * (ctx.bitDepth == 16 ? 6 : 3);
        uint8_t gray = rgbToGray(p[0], p[ctx.bitDepth == 16 ? 2 : 1], p[ctx.bitDepth == 16 ? 4 : 2]);
        if (ctx.hasTransparentRgb) {
          const uint16_t r = ctx.bitDepth == 16 ? (static_cast<uint16_t>(p[0]) << 8 | p[1]) : p[0];
          const uint16_t g = ctx.bitDepth == 16 ? (static_cast<uint16_t>(p[2]) << 8 | p[3]) : p[1];
          const uint16_t b = ctx.bitDepth == 16 ? (static_cast<uint16_t>(p[4]) << 8 | p[5]) : p[2];
          if (r == ctx.transparentRed && g == ctx.transparentGreen && b == ctx.transparentBlue) gray = 255;
        }
        grayRow[x] = gray;
      }
      break;
    case PNG_COLOR_PALETTE: {
      const int ppb = 8 / ctx.bitDepth;
      const uint8_t mask = (1 << ctx.bitDepth) - 1;
      for (uint32_t x = 0; x < w; x++) {
        const int shift = (ppb - 1 - (x % ppb)) * ctx.bitDepth;
        uint8_t idx = (src[x / ppb] >> shift) & mask;
        if (idx >= ctx.paletteSize) idx = 0;
        grayRow[x] = compositeOverWhite(rgbToGray(ctx.palette[idx * 3], ctx.palette[idx * 3 + 1],
                                                  ctx.palette[idx * 3 + 2]),
                                        ctx.paletteAlpha[idx]);
      }
      break;
    }
    case PNG_COLOR_GRAYSCALE_ALPHA:
      for (uint32_t x = 0; x < w; x++) {
        grayRow[x] = ctx.bitDepth == 16 ? compositeOverWhite(src[x * 4], src[x * 4 + 2])
                                        : compositeOverWhite(src[x * 2], src[x * 2 + 1]);
      }
      break;
    case PNG_COLOR_RGBA:
      for (uint32_t x = 0; x < w; x++) {
        const uint8_t* p = src + x * (ctx.bitDepth == 16 ? 8 : 4);
        grayRow[x] = ctx.bitDepth == 16 ? compositeOverWhite(rgbToGray(p[0], p[2], p[4]), p[6])
                                        : compositeOverWhite(rgbToGray(p[0], p[1], p[2]), p[3]);
      }
      break;
  }
}

void advanceScanline(PngContext& ctx) {
  uint8_t* temp = ctx.previousRow;
  ctx.previousRow = ctx.currentRow;
  ctx.currentRow = temp;
}

int quantizeGray(const int corrected, const ImageRenderMode mode) {
  if (mode == ImageRenderMode::TwoBit) {
    if (corrected < 43) return 0;
    if (corrected < 128) return 85;
    if (corrected < 213) return 170;
    return 255;
  }
  return corrected < 128 ? 0 : 255;
}

void drawQuantizedPixel(const RenderContext& ctx, const int x, const int y, const int q) {
  if (ctx.mode == ImageRenderMode::OneBit) {
    if (q == 0) {
      ctx.renderer->drawPixel(x, y, true);
    }
    return;
  }

  const uint8_t level = q <= 0 ? 0 : (q <= 85 ? 1 : (q <= 170 ? 2 : 3));
  const GfxRenderer::RenderMode renderMode = ctx.renderer->getRenderMode();
  if (renderMode == GfxRenderer::BW) {
    if (level < 3) {
      ctx.renderer->drawPixel(x, y, true);
    }
  } else if (renderMode == GfxRenderer::GRAYSCALE_MSB && (level == 1 || level == 2)) {
    ctx.renderer->drawPixel(x, y, false);
  } else if (renderMode == GfxRenderer::GRAYSCALE_LSB && level == 1) {
    ctx.renderer->drawPixel(x, y, false);
  }
}

bool drawGrayRow(RenderContext& ctx, const uint8_t* grayRow, int width, int rowIndex) {
  if (!grayRow || width <= 0 || width != ctx.width) return false;
  const int screenY = ctx.y + rowIndex;
  const bool reverseRow = ctx.mode == ImageRenderMode::TwoBit && ((rowIndex & 1) != 0);
  for (int step = 0; step < width; step++) {
    const int ox = reverseRow ? (width - 1 - step) : step;
    const int gray = grayRow[ox];
    int corrected = gray + static_cast<int>(ctx.errCur[ox]);
    if (corrected < 0) corrected = 0;
    if (corrected > 255) corrected = 255;

    int q;
    int err;
    const int solidBlackMax =
        ctx.mode == ImageRenderMode::TwoBit ? kPngTwoBitSolidBlackMax : kPngDitherSolidBlackMax;
    const int solidWhiteMin = ctx.mode == ImageRenderMode::TwoBit ? kPngTwoBitSolidWhiteMin : kPngDitherSolidWhiteMin;
    if (gray <= solidBlackMax) {
      q = 0;
      err = 0;
    } else if (gray >= solidWhiteMin) {
      q = 255;
      err = 0;
    } else {
      q = quantizeGray(corrected, ctx.mode);
      err = corrected - q;
      if (reverseRow) {
        if (ox > 0) ctx.errCur[ox - 1] = static_cast<int16_t>(ctx.errCur[ox - 1] + (err * 7) / 16);
        ctx.errNext[ox] = static_cast<int16_t>(ctx.errNext[ox] + (err * 5) / 16);
        if (ox + 1 < width) ctx.errNext[ox + 1] = static_cast<int16_t>(ctx.errNext[ox + 1] + (err * 3) / 16);
        if (ox > 0) ctx.errNext[ox - 1] = static_cast<int16_t>(ctx.errNext[ox - 1] + (err * 1) / 16);
      } else {
        if (ox + 1 < width) ctx.errCur[ox + 1] = static_cast<int16_t>(ctx.errCur[ox + 1] + (err * 7) / 16);
        ctx.errNext[ox] = static_cast<int16_t>(ctx.errNext[ox] + (err * 5) / 16);
        if (ox > 0) ctx.errNext[ox - 1] = static_cast<int16_t>(ctx.errNext[ox - 1] + (err * 3) / 16);
        if (ox + 1 < width) ctx.errNext[ox + 1] = static_cast<int16_t>(ctx.errNext[ox + 1] + (err * 1) / 16);
      }
    }

    drawQuantizedPixel(ctx, ctx.x + ox, screenY, q);
  }

  int16_t* t = ctx.errCur;
  ctx.errCur = ctx.errNext;
  ctx.errNext = t;
  memset(ctx.errNext, 0, static_cast<size_t>(width) * sizeof(int16_t));
  return true;
}

bool decodeAndRender(FsFile& pngFile, RenderContext& renderCtx, int outW, int outH, int srcX, int srcY, int srcW,
                     int srcH) {
  PngContext* ctxPtr = new (std::nothrow) PngContext();
  if (!ctxPtr) return false;
  PngContext& ctx = *ctxPtr;
  pngFile.seek(0);
  if (!beginPng(pngFile, ctx)) {
    delete ctxPtr;
    return false;
  }

  uint8_t* graySrc = static_cast<uint8_t*>(malloc(static_cast<size_t>(ctx.width)));
  uint8_t* grayDst = static_cast<uint8_t*>(malloc(static_cast<size_t>(outW)));
  if (!graySrc || !grayDst) {
    free(graySrc);
    free(grayDst);
    releasePng(ctx);
    delete ctxPtr;
    return false;
  }

  int currentSrcY = -1;
  for (int oy = 0; oy < outH; oy++) {
    const int sy =
        srcY + (outH <= 1 ? 0 : std::min(srcH - 1, (oy * srcH) / outH));
    while (currentSrcY < sy) {
      if (!decodeScanline(ctx)) {
        free(graySrc);
        free(grayDst);
        releasePng(ctx);
        delete ctxPtr;
        return false;
      }
      convertScanlineToGray(ctx, graySrc);
      advanceScanline(ctx);
      currentSrcY++;
    }

    for (int ox = 0; ox < outW; ox++) {
      const int sx = srcX + (outW <= 1 ? 0 : std::min(srcW - 1, (ox * srcW) / outW));
      grayDst[ox] = graySrc[sx];
    }
    if (!drawGrayRow(renderCtx, grayDst, outW, oy)) {
      free(graySrc);
      free(grayDst);
      releasePng(ctx);
      delete ctxPtr;
      return false;
    }
  }

  free(graySrc);
  free(grayDst);
  releasePng(ctx);
  delete ctxPtr;
  return true;
}
}  // namespace

bool PngRender::render(FsFile& pngFile, int x, int y, int targetWidth, int targetHeight, bool cropToFill,
                       const ImageRenderMode mode) const {
  if (!pngFile || targetWidth <= 0 || targetHeight <= 0) return false;

  int sourceW = 0;
  int sourceH = 0;
  if (!getDimensions(pngFile, &sourceW, &sourceH)) return false;

  int srcX = 0;
  int srcY = 0;
  int srcW = sourceW;
  int srcH = sourceH;
  int outW = targetWidth;
  int outH = targetHeight;
  if (cropToFill) {
    const float sx = static_cast<float>(targetWidth) / static_cast<float>(sourceW);
    const float sy = static_cast<float>(targetHeight) / static_cast<float>(sourceH);
    const float scale = std::max(sx, sy);
    srcW = std::max(1, static_cast<int>(targetWidth / scale));
    srcH = std::max(1, static_cast<int>(targetHeight / scale));
    srcX = std::max(0, (sourceW - srcW) / 2);
    srcY = std::max(0, (sourceH - srcH) / 2);
  } else {
    const float sx = static_cast<float>(targetWidth) / static_cast<float>(sourceW);
    const float sy = static_cast<float>(targetHeight) / static_cast<float>(sourceH);
    const float scale = std::min(sx, sy);
    outW = std::max(1, static_cast<int>(sourceW * scale));
    outH = std::max(1, static_cast<int>(sourceH * scale));
  }

  const int drawX = x + (targetWidth - outW) / 2;
  const int drawY = y + (targetHeight - outH) / 2;
  int16_t* errors = new (std::nothrow) int16_t[static_cast<size_t>(outW) * 2]();
  if (!errors) return false;

  RenderContext ctx = {.renderer = &renderer_,
                       .x = drawX,
                       .y = drawY,
                       .width = outW,
                       .mode = mode,
                       .errCur = errors,
                       .errNext = errors + outW};
  const bool ok = decodeAndRender(pngFile, ctx, outW, outH, srcX, srcY, srcW, srcH);
  delete[] errors;
  return ok;
}

bool PngRender::fromPath(const std::string& path, int x, int y, int targetWidth, int targetHeight,
                         bool cropToFill, const ImageRenderMode mode) const {
  FsFile file;
  if (!SdMan.openFileForRead("PNG", path, file)) return false;
  const bool ok = render(file, x, y, targetWidth, targetHeight, cropToFill, mode);
  file.close();
  return ok;
}

bool PngRender::getDimensions(FsFile& pngFile, int* outW, int* outH) {
  if (!outW || !outH || !pngFile) return false;
  *outW = 0;
  *outH = 0;
  const uint32_t pos = pngFile.position();
  pngFile.seek(0);

  uint8_t sig[8];
  uint32_t ihdrLen;
  uint32_t width;
  uint32_t height;
  uint8_t ihdrType[4];
  const bool ok = pngFile.read(sig, 8) == 8 && memcmp(sig, kPngSignature, 8) == 0 && readBE32(pngFile, ihdrLen) &&
                  pngFile.read(ihdrType, 4) == 4 && memcmp(ihdrType, "IHDR", 4) == 0 && ihdrLen >= 13 &&
                  readBE32(pngFile, width) && readBE32(pngFile, height) && width > 0 && height > 0 &&
                  width <= 2048 && height <= 3072;
  if (ok) {
    *outW = static_cast<int>(width);
    *outH = static_cast<int>(height);
  }
  pngFile.seek(pos);
  return ok;
}

bool PngRender::getDimensions(const std::string& path, int* outW, int* outH) {
  FsFile file;
  if (!SdMan.openFileForRead("PNG", path, file)) return false;
  const bool ok = getDimensions(file, outW, outH);
  file.close();
  return ok;
}
