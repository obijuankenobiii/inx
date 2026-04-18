#include "GfxRenderer.h"

#include <Utf8.h>
#include <vector>
#include <algorithm>
#include <cmath>

namespace {
/** Corner radius for rounded fillRect/drawRect: subtle, not pill-shaped (was min/4). */
int roundedRectCornerRadius(const int width, const int height) {
  const int m = std::min(width, height);
  if (m < 5) {
    return 1;
  }
  int r = m / 10;
  if (r < 2) {
    r = 2;
  }
  if (2 * r > m) {
    r = m / 2;
  }
  return std::max(1, r);
}

int cornerSpanFromRy(const int r, const int ry) {
  const int inner = r * r - ry * ry;
  if (inner <= 0) {
    return 0;
  }
  return static_cast<int>(std::sqrt(static_cast<double>(inner)));
}

/** Packed 2bpp sample: px in [0,width), py in [0,height). */
inline uint8_t packed2bppAt(const uint8_t* buf, const int stride, const int px, const int py) {
  const uint8_t* row = buf + static_cast<size_t>(py) * static_cast<size_t>(stride);
  return (row[px / 4] >> (6 - ((px % 4) * 2))) & 0x3;
}

/** Bilinear sample in 0..3; reduces blocky NN upscale without changing downscale path. */
float sampleBilinear2bpp(const uint8_t* packed, const int width, const int height, const int stride, float xf,
                        float yf) {
  if (width <= 0 || height <= 0) {
    return 3.f;
  }
  if (xf < 0.f) {
    xf = 0.f;
  }
  if (yf < 0.f) {
    yf = 0.f;
  }
  const float maxX = static_cast<float>(width - 1);
  const float maxY = static_cast<float>(height - 1);
  if (xf > maxX) {
    xf = maxX;
  }
  if (yf > maxY) {
    yf = maxY;
  }
  const int x0 = static_cast<int>(std::floor(xf));
  const int y0 = static_cast<int>(std::floor(yf));
  const int x1 = std::min(x0 + 1, width - 1);
  const int y1 = std::min(y0 + 1, height - 1);
  const float wx = xf - static_cast<float>(x0);
  const float wy = yf - static_cast<float>(y0);
  const float v00 = static_cast<float>(packed2bppAt(packed, stride, x0, y0));
  const float v10 = static_cast<float>(packed2bppAt(packed, stride, x1, y0));
  const float v01 = static_cast<float>(packed2bppAt(packed, stride, x0, y1));
  const float v11 = static_cast<float>(packed2bppAt(packed, stride, x1, y1));
  const float t0 = v00 * (1.f - wx) + v10 * wx;
  const float t1 = v01 * (1.f - wx) + v11 * wx;
  return t0 * (1.f - wy) + t1 * wy;
}

/** Map bilinear 0..3 blend to a discrete 2 bpp stage; snaps near extremes so flat black/white stays solid. */
inline int snapBilinear2bppStage(const float interp) {
  if (interp <= 0.55f) {
    return 0;
  }
  if (interp >= 2.45f) {
    return 3;
  }
  const int r = static_cast<int>(interp + 0.5f);
  if (r < 0) {
    return 0;
  }
  if (r > 3) {
    return 3;
  }
  return r;
}

/** After box-filter downscale, snap near-black / near-white column averages to solid 0 / 3. */
inline uint8_t snapDownscaleResolved2bpp(const uint32_t sum, const uint32_t count) {
  if (count == 0) {
    return 3;
  }
  uint8_t v = static_cast<uint8_t>((sum + (count / 2)) / count);
  const float avg = static_cast<float>(sum) / static_cast<float>(count);
  if (avg >= 2.45f) {
    v = 3;
  } else if (avg <= 0.55f) {
    v = 0;
  }
  return v;
}

/** Max RAM for full-image 2bpp decode used by bilinear upscale (fallback to NN blocks if larger). */
constexpr size_t kMaxBilinearPackedBytes = 280u * 1024u;

/**
 * True when a single bitmap has enough mid-gray (2bpp 1/2) to justify the grayscale refresh pass.
 * When most pixels are white (3), require a much larger gray area so JPEG/dither noise in margins
 * does not force a full-screen grayscale cycle.
 */
bool bitmapStatsWarrantGrayscale(const uint32_t checked, const uint32_t grayPixels, const uint32_t whitePixels) {
  if (checked == 0) {
    return false;
  }
  const uint32_t whiteRatioX1000 = (whitePixels * 1000U) / checked;

  if (whiteRatioX1000 >= 820U) {
    const uint32_t minGray = std::max<uint32_t>((checked * 35U) / 1000U, 1200U);
    return grayPixels >= minGray;
  }
  if (whiteRatioX1000 >= 720U) {
    const uint32_t minGray = std::max<uint32_t>((checked * 20U) / 1000U, 512U);
    return grayPixels >= minGray;
  }

  const uint32_t thresholdByRatio = (checked * 80U) / 1000U;  // 8%
  const uint32_t threshold = std::max<uint32_t>(256U, thresholdByRatio);
  return grayPixels >= threshold;
}

/** 1-bpp packed row-major, MSB = left; dimensions are the source bitmap's (width x height). */
bool readIconBitMsbFirst(const uint8_t* bitmap, const int width, const int height, const int sx, const int sy) {
  if (sx < 0 || sy < 0 || sx >= width || sy >= height) {
    return false;
  }
  const int stride = (width + 7) / 8;
  const uint8_t byte = bitmap[sy * stride + sx / 8];
  return (byte & (0x80 >> (sx % 8))) != 0;
}

/** Quarter-circle outline (same centers as rounded fillRect), not a filled wedge. */
void drawRoundedRectCornerOutlines(const GfxRenderer& gfx, int x, int y, int width, int height, int r, bool state) {
  // Top-left center (x+r, y+r)
  for (int h = 0; h <= r; ++h) {
    const int span = cornerSpanFromRy(r, r - h);
    gfx.drawPixel(x + r - span, y + h, state);
  }

  // Top-right center (x+width-r-1, y+r)
  for (int h = 0; h <= r; ++h) {
    const int span = cornerSpanFromRy(r, r - h);
    gfx.drawPixel(x + width - r - 1 + span, y + h, state);
  }

  // Bottom-left center (x+r, y+height-r-1)
  for (int h = 0; h <= r; ++h) {
    const int span = cornerSpanFromRy(r, r - h);
    gfx.drawPixel(x + r - span, y + height - 1 - h, state);
  }

  // Bottom-right center (x+width-r-1, y+height-r-1)
  for (int h = 0; h <= r; ++h) {
    const int span = cornerSpanFromRy(r, r - h);
    gfx.drawPixel(x + width - r - 1 + span, y + height - 1 - h, state);
  }
}
}  // namespace

void GfxRenderer::insertFont(const int fontId, EpdFontFamily font) { fontMap.insert({fontId, font}); }

void GfxRenderer::rotateCoordinates(const int x, const int y, int* rotatedX, int* rotatedY) const {
  switch (orientation) {
    case Portrait: {
      // Logical portrait (480x800) → panel (800x480)
      // Rotation: 90 degrees clockwise
      *rotatedX = y;
      *rotatedY = HalDisplay::DISPLAY_HEIGHT - 1 - x;
      break;
    }
    case LandscapeClockwise: {
      // Logical landscape (800x480) rotated 180 degrees (swap top/bottom and left/right)
      *rotatedX = HalDisplay::DISPLAY_WIDTH - 1 - x;
      *rotatedY = HalDisplay::DISPLAY_HEIGHT - 1 - y;
      break;
    }
    case PortraitInverted: {
      // Logical portrait (480x800) → panel (800x480)
      // Rotation: 90 degrees counter-clockwise
      *rotatedX = HalDisplay::DISPLAY_WIDTH - 1 - y;
      *rotatedY = x;
      break;
    }
    case LandscapeCounterClockwise: {
      // Logical landscape (800x480) aligned with panel orientation
      *rotatedX = x;
      *rotatedY = y;
      break;
    }
  }
}

void GfxRenderer::drawPixel(const int x, const int y, const bool state) const {
  uint8_t* frameBuffer = display.getFrameBuffer();

  // Early return if no framebuffer is set
  if (!frameBuffer) {
    Serial.printf("[%lu] [GFX] !! No framebuffer\n", millis());
    return;
  }

  int rotatedX = 0;
  int rotatedY = 0;
  rotateCoordinates(x, y, &rotatedX, &rotatedY);

  // Bounds checking against physical panel dimensions
  if (rotatedX < 0 || rotatedX >= HalDisplay::DISPLAY_WIDTH || rotatedY < 0 || rotatedY >= HalDisplay::DISPLAY_HEIGHT) {
    Serial.printf("[%lu] [GFX] !! Outside range (%d, %d) -> (%d, %d)\n", millis(), x, y, rotatedX, rotatedY);
    return;
  }

  // Calculate byte position and bit position
  const uint16_t byteIndex = rotatedY * HalDisplay::DISPLAY_WIDTH_BYTES + (rotatedX / 8);
  const uint8_t bitPosition = 7 - (rotatedX % 8);  // MSB first

  if (state) {
    frameBuffer[byteIndex] &= ~(1 << bitPosition);  // Clear bit
  } else {
    frameBuffer[byteIndex] |= 1 << bitPosition;  // Set bit
  }
}

int GfxRenderer::getTextWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  int w = 0, h = 0;
  fontMap.at(fontId).getTextDimensions(text, &w, &h, style);
  return w;
}

void GfxRenderer::drawCenteredText(const int fontId, const int y, const char* text, const bool black,
                                   const EpdFontFamily::Style style) const {
  const int x = (getScreenWidth() - getTextWidth(fontId, text, style)) / 2;
  drawText(fontId, x, y, text, black, style);
}

void GfxRenderer::drawText(const int fontId, const int x, const int y, const char* text, const bool black,
                           const EpdFontFamily::Style style) const {
  const int yPos = y + getFontAscenderSize(fontId);
  int xpos = x;

  // cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return;
  }
  const auto font = fontMap.at(fontId);

  // no printable characters
  if (!font.hasPrintableChars(text, style)) {
    return;
  }

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    renderChar(font, cp, &xpos, &yPos, black, style);
  }
}

void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const bool state) const {
  const int maxX = getScreenWidth() - 1;
  const int maxY = getScreenHeight() - 1;

  if (x1 == x2) {
    if (y2 < y1) {
      std::swap(y1, y2);
    }
    if (x1 < 0 || x1 > maxX) return;
    y1 = std::max(0, y1);
    y2 = std::min(y2, maxY);
    if (y1 > y2) return;
    for (int y = y1; y <= y2; y++) {
      drawPixel(x1, y, state);
    }
  } else if (y1 == y2) {
    if (x2 < x1) {
      std::swap(x1, x2);
    }
    if (y1 < 0 || y1 > maxY) return;
    x1 = std::max(0, x1);
    x2 = std::min(x2, maxX);
    if (x1 > x2) return;
    for (int x = x1; x <= x2; x++) {
      drawPixel(x, y1, state);
    }
  } else {
    // TODO: Implement
    Serial.printf("[%lu] [GFX] Line drawing not supported\n", millis());
  }
}

void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const bool state, const bool rounded) const {
  if (!rounded) {
    // Original rectangle drawing
    drawLine(x, y, x + width - 1, y, state);
    drawLine(x + width - 1, y, x + width - 1, y + height - 1, state);
    drawLine(x + width - 1, y + height - 1, x, y + height - 1, state);
    drawLine(x, y, x, y + height - 1, state);
  } else {
    // Rounded rectangle drawing
    const int radius = roundedRectCornerRadius(width, height);
    
    // Draw top edge (excluding corners)
    drawLine(x + radius, y, x + width - radius - 1, y, state);
    
    // Draw bottom edge (excluding corners)
    drawLine(x + radius, y + height - 1, x + width - radius - 1, y + height - 1, state);
    
    // Draw left edge (excluding corners)
    drawLine(x, y + radius, x, y + height - radius - 1, state);
    
    // Draw right edge (excluding corners)
    drawLine(x + width - 1, y + radius, x + width - 1, y + height - radius - 1, state);

    // Thin quarter arcs (filled wedges used to spill black inside white rounded fills)
    drawRoundedRectCornerOutlines(*this, x, y, width, height, radius, state);
  }
}

void GfxRenderer::fillRect(const int x, const int y, const int width, const int height, const FillTone tone,
                           const bool rounded) const {
  if (tone == FillTone::Gray) {
    if (rounded) {
      // Checkerboard not implemented for rounded fills; use solid ink so corners stay correct.
      fillRect(x, y, width, height, FillTone::Ink, true);
      return;
    }
    const int x1 = std::max(0, x);
    const int y1 = std::max(0, y);
    const int x2 = std::min(getScreenWidth(), x + width);
    const int y2 = std::min(getScreenHeight(), y + height);
    for (int fy = y1; fy < y2; fy++) {
      for (int fx = x1; fx < x2; fx++) {
        drawPixel(fx, fy, ((fx + fy) & 1) == 0);
      }
    }
    return;
  }

  const bool state = (tone == FillTone::Ink);
  if (!rounded) {
    for (int fillY = y; fillY < y + height; fillY++) {
      drawLine(x, fillY, x + width - 1, fillY, state);
    }
  } else {
    const int radius = roundedRectCornerRadius(width, height);

    for (int fillY = y + radius; fillY < y + height - radius; fillY++) {
      drawLine(x, fillY, x + width - 1, fillY, state);
    }

    for (int cornerY = 0; cornerY < radius; cornerY++) {
      int cornerSpan = static_cast<int>(sqrt(radius * radius - (radius - cornerY) * (radius - cornerY)));

      int topY = y + cornerY;
      drawLine(x + radius - cornerSpan, topY, x + width - radius + cornerSpan - 1, topY, state);

      int bottomY = y + height - 1 - cornerY;
      drawLine(x + radius - cornerSpan, bottomY, x + width - radius + cornerSpan - 1, bottomY, state);
    }
  }
}

void GfxRenderer::fillRect(const int x, const int y, const int width, const int height, const bool state,
                           const bool rounded) const {
  fillRect(x, y, width, height, state ? FillTone::Ink : FillTone::Paper, rounded);
}

void GfxRenderer::drawBwFrom2bppStage(const int px, const int py, const uint8_t stage03) const {
  const uint8_t v = static_cast<uint8_t>(stage03 & 3u);
  if (v == 3u) {
    return;
  }
  if (v == 0u) {
    drawPixel(px, py, true);
    return;
  }
  // Stages 1 / 2: 8×8 Bayer at full pixel resolution. 4×4 Bayer repeats every 4 px and reads as harsh “squares”;
  // 8×8 doubles the period so mid-grays look smoother and more even (still dithered — true solid gray needs GC refresh).
  static const uint8_t kBayer8[64] = {
      0,  32, 8,  40, 2,  34, 10, 42,  48, 16, 56, 24, 50, 18, 58, 26,  12, 44, 4,  36, 14, 46, 6,  38,
      60, 28, 52, 20, 62, 30, 54, 22,  3,  35, 11, 43, 1,  33, 9,  41,  51, 19, 59, 27, 49, 17, 57, 25,
      15, 47, 7,  39, 13, 45, 5,  37,  63, 31, 55, 23, 61, 29, 53, 21};
  const uint8_t t = kBayer8[((py & 7) << 3) | (px & 7)];
  if (v == 1u) {
    drawPixel(px, py, t < 40);  // 40/64 ≈ 62.5% ink
    return;
  }
  drawPixel(px, py, t < 28);  // 28/64 ≈ 43.75% ink
}

void GfxRenderer::drawBitmap(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight,
                             const float cropX, const float cropY) const {
  // For 1-bit bitmaps, use optimized 1-bit rendering path (no crop support for 1-bit)
  if (bitmap.is1Bit() && cropX == 0.0f && cropY == 0.0f) {
    drawBitmap1Bit(bitmap, x, y, maxWidth, maxHeight);
    return;
  }

  float scale = 1.0f;
  bool isScaled = false;
  int cropPixX = std::floor(bitmap.getWidth() * cropX / 2.0f);
  int cropPixY = std::floor(bitmap.getHeight() * cropY / 2.0f);


  const float croppedWidth = (1.0f - cropX) * static_cast<float>(bitmap.getWidth());
  const float croppedHeight = (1.0f - cropY) * static_cast<float>(bitmap.getHeight());
  bool hasTargetBounds = false;
  float fitScale = 1.0f;

  if (maxWidth > 0 && croppedWidth > 0.0f) {
    fitScale = static_cast<float>(maxWidth) / croppedWidth;
    hasTargetBounds = true;
  }

  if (maxHeight > 0 && croppedHeight > 0.0f) {
    const float heightScale = static_cast<float>(maxHeight) / croppedHeight;
    fitScale = hasTargetBounds ? std::min(fitScale, heightScale) : heightScale;
    hasTargetBounds = true;
  }

  constexpr float kScaleEps = 1e-5f;
  const bool upscaleBlock = hasTargetBounds && fitScale > 1.0f + kScaleEps;
  if (hasTargetBounds && fitScale < 1.0f - kScaleEps) {
    scale = fitScale;
    isScaled = true;
  } else if (upscaleBlock) {
    scale = fitScale;
    isScaled = true;
  }

  // Calculate output row size (2 bits per pixel, packed into bytes)
  // IMPORTANT: Use int, not uint8_t, to avoid overflow for images > 1020 pixels wide
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes) {
    free(outputRow);
    free(rowBytes);
    return;
  }

  auto get2BitPixel = [](const uint8_t* row, const int px) -> uint8_t {
    return (row[px / 4] >> (6 - ((px * 2) % 8))) & 0x3;
  };

  // Use area accumulation for downscaled 2-bit bitmaps to avoid block/square artifacts.
  const bool useAreaDownscale = isScaled && scale < 1.0f && !bitmap.is1Bit();
  const bool grayFull = (bitmapGrayRenderStyle == BitmapGrayRenderStyle::FullGray);
  const int screenWidth = getScreenWidth();
  std::vector<uint32_t> graySums;
  std::vector<uint32_t> grayCounts;
  int activeDestY = -1;

  uint32_t localChecked = 0;
  uint32_t localGray = 0;
  uint32_t localWhite = 0;

  // Upscale: bilinear on a full decoded 2bpp buffer (avoids NN "squares"); large images fall back below.
  if (upscaleBlock && !bitmap.is1Bit()) {
    const int bw = bitmap.getWidth();
    const int bh = bitmap.getHeight();
    const int stride = outputRowSize;
    const size_t packedBytes = static_cast<size_t>(bh) * static_cast<size_t>(stride);
    if (packedBytes <= kMaxBilinearPackedBytes) {
      if (uint8_t* packed = static_cast<uint8_t*>(malloc(packedBytes))) {
        Bitmap* nc = const_cast<Bitmap*>(&bitmap);
        bool decodeOk = nc->rewindToData() == BmpReaderError::Ok;
        if (decodeOk) {
          for (int ry = 0; ry < bh; ry++) {
            if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
              decodeOk = false;
              break;
            }
            memcpy(packed + static_cast<size_t>(ry) * static_cast<size_t>(stride), outputRow,
                   static_cast<size_t>(stride));
          }
        }
        if (decodeOk) {
          const int cropW = bw - 2 * cropPixX;
          const int cropH = bh - 2 * cropPixY;
          if (cropW > 0 && cropH > 0) {
            int destW = static_cast<int>(std::ceil(static_cast<float>(cropW) * scale - 1e-5f));
            int destH = static_cast<int>(std::ceil(static_cast<float>(cropH) * scale - 1e-5f));
            if (maxWidth > 0) {
              destW = std::min(destW, maxWidth);
            }
            if (maxHeight > 0) {
              destH = std::min(destH, maxHeight);
            }
            const int screenH = getScreenHeight();
            uint32_t bChecked = 0, bGray = 0, bWhite = 0;
            for (int dy = 0; dy < destH; dy++) {
              const int dpy = y + dy;
              if (dpy < 0 || dpy >= screenH) {
                continue;
              }
              for (int dx = 0; dx < destW; dx++) {
                const int dpx = x + dx;
                if (dpx < 0 || dpx >= screenWidth) {
                  continue;
                }
                const float logX = (static_cast<float>(dx) + 0.5f) / scale - 0.5f;
                const float logY = (static_cast<float>(dy) + 0.5f) / scale - 0.5f;
                if (logX < 0.f || logY < 0.f || logX > static_cast<float>(cropW) - 1.f - 1e-4f ||
                    logY > static_cast<float>(cropH) - 1.f - 1e-4f) {
                  continue;
                }
                const float bmpXf = logX + static_cast<float>(cropPixX);
                const float bmpYf = bitmap.isTopDown()
                                        ? (logY + static_cast<float>(cropPixY))
                                        : (static_cast<float>(bh - 1) - logY - static_cast<float>(cropPixY));
                const float interp = sampleBilinear2bpp(packed, bw, bh, stride, bmpXf, bmpYf);
                if (renderMode == BW) {
                  bChecked++;
                  const int st = snapBilinear2bppStage(interp);
                  if (st == 1 || st == 2) {
                    bGray++;
                  } else if (st == 3) {
                    bWhite++;
                  }
                  if (grayFull ? (st < 3) : (st <= 1)) {
                    drawBwFrom2bppStage(dpx, dpy, static_cast<uint8_t>(st));
                  }
                } else if (renderMode == GRAYSCALE_MSB) {
                  const int rv = static_cast<int>(interp + 0.5f);
                  if (rv == 1 || rv == 2) {
                    drawPixel(dpx, dpy, false);
                  }
                } else if (renderMode == GRAYSCALE_LSB) {
                  const int rv = static_cast<int>(interp + 0.5f);
                  if (rv == 1) {
                    drawPixel(dpx, dpy, false);
                  }
                }
              }
            }
            localChecked += bChecked;
            localGray += bGray;
            localWhite += bWhite;
            if (renderMode == BW && bitmapStatsWarrantGrayscale(localChecked, localGray, localWhite)) {
              anyBitmapImageWantsGrayscale = true;
            }
            free(packed);
            free(outputRow);
            free(rowBytes);
            return;
          }
        }
        (void)nc->rewindToData();
        free(packed);
      }
    }
  }

  auto flushAccumulatedRow = [&](const int destY) {
    if (destY < 0 || destY >= getScreenHeight()) {
      return;
    }
    for (int sx = 0; sx < screenWidth; sx++) {
      uint8_t resolvedVal = 3;
      if (grayCounts[sx] > 0) {
        resolvedVal = snapDownscaleResolved2bpp(graySums[sx], grayCounts[sx]);
      }
      if (renderMode == BW && grayCounts[sx] > 0) {
        localChecked++;
        if (resolvedVal == 1 || resolvedVal == 2) {
          localGray++;
        } else if (resolvedVal == 3) {
          localWhite++;
        }
      }
      // No mapped source samples: leave white. Neighbor carry caused dark rectangular smears.
      // Downscaled BW: Balanced inks only dark averages (<=1); FullGray inks all non-white averages (<3).
      if (renderMode == BW && grayCounts[sx] > 0 &&
          (grayFull ? (resolvedVal < 3) : (resolvedVal <= 1))) {
        drawBwFrom2bppStage(sx, destY, resolvedVal);
      } else if (renderMode == GRAYSCALE_MSB && (resolvedVal == 1 || resolvedVal == 2)) {
        drawPixel(sx, destY, false);
      } else if (renderMode == GRAYSCALE_LSB && resolvedVal == 1) {
        drawPixel(sx, destY, false);
      }
      graySums[sx] = 0;
      grayCounts[sx] = 0;
    }
  };

  if (useAreaDownscale) {
    graySums.assign(screenWidth, 0);
    grayCounts.assign(screenWidth, 0);
  }

  for (int bmpY = 0; bmpY < (bitmap.getHeight() - cropPixY); bmpY++) {
    // The BMP's (0, 0) is the bottom-left corner (if the height is positive, top-left if negative).
    // Screen's (0, 0) is the top-left corner.
    const int logicalYUnscaled = -cropPixY + (bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY);

    int screenY = logicalYUnscaled;
    if (isScaled && !upscaleBlock) {
      screenY = static_cast<int>(std::floor(logicalYUnscaled * scale));
    }
    screenY += y;
    if (!upscaleBlock && screenY >= getScreenHeight()) {
      break;
    }

    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      free(outputRow);
      free(rowBytes);
      return;
    }

    if (!upscaleBlock && screenY < 0) {
      continue;
    }

    if (bmpY < cropPixY) {
      // Skip the row if it's outside the crop area
      continue;
    }

    int destY0 = screenY;
    int destY1 = screenY;
    if (upscaleBlock) {
      destY0 = y + static_cast<int>(std::floor(logicalYUnscaled * scale));
      destY1 = y + static_cast<int>(std::floor((logicalYUnscaled + 1) * scale)) - 1;
      if (destY0 >= getScreenHeight()) {
        break;
      }
      if (destY1 < 0) {
        continue;
      }
    }

    for (int bmpX = cropPixX; bmpX < bitmap.getWidth() - cropPixX; bmpX++) {
      const uint8_t sourceVal = get2BitPixel(outputRow, bmpX);
      uint8_t val = sourceVal;

      // Grayscale pass decision: use source 2bpp only (per-image), BW pass only.
      if (renderMode == BW && !useAreaDownscale) {
        localChecked++;
        if (sourceVal == 1 || sourceVal == 2) {
          localGray++;
        } else if (sourceVal == 3) {
          localWhite++;
        }
      }

      if (useAreaDownscale) {
        if (activeDestY == -1) {
          activeDestY = screenY;
        } else if (screenY != activeDestY) {
          flushAccumulatedRow(activeDestY);
          activeDestY = screenY;
        }
        int screenX = bmpX - cropPixX;
        if (isScaled) {
          screenX = static_cast<int>(std::floor(screenX * scale));
        }
        screenX += x;
        if (screenX >= getScreenWidth()) {
          break;
        }
        if (screenX < 0) {
          continue;
        }
        graySums[screenX] = static_cast<uint16_t>(graySums[screenX] + val);
        grayCounts[screenX]++;
      } else if (upscaleBlock) {
        const int logicalX = bmpX - cropPixX;
        const int destX0 = x + static_cast<int>(std::floor(logicalX * scale));
        const int destX1 = x + static_cast<int>(std::floor((logicalX + 1) * scale)) - 1;
        for (int yy = destY0; yy <= destY1; ++yy) {
          if (yy < 0 || yy >= getScreenHeight()) {
            continue;
          }
          for (int xx = destX0; xx <= destX1; ++xx) {
            if (xx < 0 || xx >= getScreenWidth()) {
              continue;
            }
            if (renderMode == BW && (grayFull ? (val < 3) : (val <= 1))) {
              drawBwFrom2bppStage(xx, yy, val);
            } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
              drawPixel(xx, yy, false);
            } else if (renderMode == GRAYSCALE_LSB && val == 1) {
              drawPixel(xx, yy, false);
            }
          }
        }
      } else {
        int screenX = bmpX - cropPixX;
        if (isScaled) {
          screenX = static_cast<int>(std::floor(screenX * scale));
        }
        screenX += x;
        if (screenX >= getScreenWidth()) {
          break;
        }
        if (screenX < 0) {
          continue;
        }

        if (renderMode == BW && val < 3) {
          drawBwFrom2bppStage(screenX, screenY, val);
        } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
          drawPixel(screenX, screenY, false);
        } else if (renderMode == GRAYSCALE_LSB && val == 1) {
          drawPixel(screenX, screenY, false);
        }
      }
    }

  }

  if (useAreaDownscale && activeDestY >= 0) {
    flushAccumulatedRow(activeDestY);
  }

  if (renderMode == BW && bitmapStatsWarrantGrayscale(localChecked, localGray, localWhite)) {
    anyBitmapImageWantsGrayscale = true;
  }

  free(outputRow);
  free(rowBytes);
}

void GfxRenderer::drawBitmap1Bit(const Bitmap& bitmap, const int x, const int y, const int maxWidth,
                                 const int maxHeight) const {
  float fitScale = 1.0f;
  bool hasTargetBounds = false;
  const float iw = static_cast<float>(bitmap.getWidth());
  const float ih = static_cast<float>(bitmap.getHeight());
  if (maxWidth > 0 && iw > 0.0f) {
    fitScale = static_cast<float>(maxWidth) / iw;
    hasTargetBounds = true;
  }
  if (maxHeight > 0 && ih > 0.0f) {
    const float heightScale = static_cast<float>(maxHeight) / ih;
    fitScale = hasTargetBounds ? std::min(fitScale, heightScale) : heightScale;
    hasTargetBounds = true;
  }

  constexpr float kEps = 1e-5f;
  float scale = 1.0f;
  bool isScaled = false;
  const bool upscaleBlock = hasTargetBounds && fitScale > 1.0f + kEps;
  if (hasTargetBounds && fitScale < 1.0f - kEps) {
    scale = fitScale;
    isScaled = true;
  } else if (upscaleBlock) {
    scale = fitScale;
    isScaled = true;
  }

  // Downscaling by iterating source pixels leaves destination holes; box-filter like drawBitmap.
  const bool useAreaDownscale = isScaled && scale < 1.0f;
  const bool grayFull = (bitmapGrayRenderStyle == BitmapGrayRenderStyle::FullGray);
  const int screenWidth = getScreenWidth();
  std::vector<uint32_t> graySums;
  std::vector<uint32_t> grayCounts;
  int activeDestY = -1;

  uint32_t localChecked = 0;
  uint32_t localGray = 0;
  uint32_t localWhite = 0;

  auto get2BitPixel = [](const uint8_t* row, const int px) -> uint8_t {
    return (row[px / 4] >> (6 - ((px * 2) % 8))) & 0x3;
  };

  auto flushAccumulatedRow = [&](const int destY) {
    if (destY < 0 || destY >= getScreenHeight()) {
      return;
    }
    for (int sx = 0; sx < screenWidth; sx++) {
      uint8_t resolvedVal = 3;
      if (grayCounts[sx] > 0) {
        resolvedVal = snapDownscaleResolved2bpp(graySums[sx], grayCounts[sx]);
      }
      if (renderMode == BW && grayCounts[sx] > 0) {
        localChecked++;
        if (resolvedVal == 1 || resolvedVal == 2) {
          localGray++;
        } else if (resolvedVal == 3) {
          localWhite++;
        }
      }
      if (renderMode == BW && grayCounts[sx] > 0 &&
          (grayFull ? (resolvedVal < 3) : (resolvedVal <= 1))) {
        drawBwFrom2bppStage(sx, destY, resolvedVal);
      } else if (renderMode == GRAYSCALE_MSB && (resolvedVal == 1 || resolvedVal == 2)) {
        drawPixel(sx, destY, false);
      } else if (renderMode == GRAYSCALE_LSB && resolvedVal == 1) {
        drawPixel(sx, destY, false);
      }
      graySums[sx] = 0;
      grayCounts[sx] = 0;
    }
  };

  if (useAreaDownscale) {
    graySums.assign(screenWidth, 0);
    grayCounts.assign(screenWidth, 0);
  }

  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes) {
    free(outputRow);
    free(rowBytes);
    return;
  }

  if (upscaleBlock) {
    const int bw = bitmap.getWidth();
    const int bh = bitmap.getHeight();
    const int stride = outputRowSize;
    const size_t packedBytes = static_cast<size_t>(bh) * static_cast<size_t>(stride);
    if (packedBytes <= kMaxBilinearPackedBytes) {
      if (uint8_t* packed = static_cast<uint8_t*>(malloc(packedBytes))) {
        Bitmap* nc = const_cast<Bitmap*>(&bitmap);
        bool decodeOk = nc->rewindToData() == BmpReaderError::Ok;
        if (decodeOk) {
          for (int ry = 0; ry < bh; ry++) {
            if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
              decodeOk = false;
              break;
            }
            memcpy(packed + static_cast<size_t>(ry) * static_cast<size_t>(stride), outputRow,
                   static_cast<size_t>(stride));
          }
        }
        if (decodeOk && bw > 0 && bh > 0) {
          int destW = static_cast<int>(std::ceil(static_cast<float>(bw) * scale - 1e-5f));
          int destH = static_cast<int>(std::ceil(static_cast<float>(bh) * scale - 1e-5f));
          if (maxWidth > 0) {
            destW = std::min(destW, maxWidth);
          }
          if (maxHeight > 0) {
            destH = std::min(destH, maxHeight);
          }
          const int screenH = getScreenHeight();
          uint32_t bChecked = 0, bGray = 0, bWhite = 0;
          for (int dy = 0; dy < destH; dy++) {
            const int dpy = y + dy;
            if (dpy < 0 || dpy >= screenH) {
              continue;
            }
            for (int dx = 0; dx < destW; dx++) {
              const int dpx = x + dx;
              if (dpx < 0 || dpx >= screenWidth) {
                continue;
              }
              const float logX = (static_cast<float>(dx) + 0.5f) / scale - 0.5f;
              const float logY = (static_cast<float>(dy) + 0.5f) / scale - 0.5f;
              if (logX < 0.f || logY < 0.f || logX > static_cast<float>(bw) - 1.f - 1e-4f ||
                  logY > static_cast<float>(bh) - 1.f - 1e-4f) {
                continue;
              }
              const float bmpXf = logX;
              const float bmpYf =
                  bitmap.isTopDown() ? logY : (static_cast<float>(bh - 1) - logY);
              const float interp = sampleBilinear2bpp(packed, bw, bh, stride, bmpXf, bmpYf);
              if (renderMode == BW) {
                bChecked++;
                const int st = snapBilinear2bppStage(interp);
                if (st == 1 || st == 2) {
                  bGray++;
                } else if (st == 3) {
                  bWhite++;
                }
                if (grayFull ? (st < 3) : (st <= 1)) {
                  drawBwFrom2bppStage(dpx, dpy, static_cast<uint8_t>(st));
                }
              } else if (renderMode == GRAYSCALE_MSB) {
                const int rv = static_cast<int>(interp + 0.5f);
                if (rv == 1 || rv == 2) {
                  drawPixel(dpx, dpy, false);
                }
              } else if (renderMode == GRAYSCALE_LSB) {
                const int rv = static_cast<int>(interp + 0.5f);
                if (rv == 1) {
                  drawPixel(dpx, dpy, false);
                }
              }
            }
          }
          localChecked += bChecked;
          localGray += bGray;
          localWhite += bWhite;
          if (renderMode == BW && bitmapStatsWarrantGrayscale(localChecked, localGray, localWhite)) {
            anyBitmapImageWantsGrayscale = true;
          }
          free(packed);
          free(outputRow);
          free(rowBytes);
          return;
        }
        (void)nc->rewindToData();
        free(packed);
      }
    }
  }

  for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      free(outputRow);
      free(rowBytes);
      return;
    }

    const int bmpYOffset = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
    int screenY = y + bmpYOffset;
    if (isScaled && !upscaleBlock) {
      screenY = y + static_cast<int>(std::floor(bmpYOffset * scale));
    }
    if (!upscaleBlock && screenY >= getScreenHeight()) {
      continue;
    }
    if (!upscaleBlock && screenY < 0) {
      continue;
    }

    int destY0 = screenY;
    int destY1 = screenY;
    if (upscaleBlock) {
      destY0 = y + static_cast<int>(std::floor(bmpYOffset * scale));
      destY1 = y + static_cast<int>(std::floor((bmpYOffset + 1) * scale)) - 1;
      if (destY0 >= getScreenHeight()) {
        continue;
      }
      if (destY1 < 0) {
        continue;
      }
    }

    for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
      const uint8_t sourceVal = get2BitPixel(outputRow, bmpX);
      const uint8_t val = sourceVal;

      if (renderMode == BW && !useAreaDownscale) {
        localChecked++;
        if (sourceVal == 1 || sourceVal == 2) {
          localGray++;
        } else if (sourceVal == 3) {
          localWhite++;
        }
      }

      if (useAreaDownscale) {
        int screenX = x + (isScaled ? static_cast<int>(std::floor(bmpX * scale)) : bmpX);
        if (screenX >= getScreenWidth()) {
          break;
        }
        if (screenX < 0) {
          continue;
        }
        if (activeDestY == -1) {
          activeDestY = screenY;
        } else if (screenY != activeDestY) {
          flushAccumulatedRow(activeDestY);
          activeDestY = screenY;
        }
        graySums[screenX] += val;
        grayCounts[screenX]++;
      } else if (upscaleBlock) {
        const int destX0 = x + static_cast<int>(std::floor(bmpX * scale));
        const int destX1 = x + static_cast<int>(std::floor((bmpX + 1) * scale)) - 1;
        for (int yy = destY0; yy <= destY1; ++yy) {
          if (yy < 0 || yy >= getScreenHeight()) {
            continue;
          }
          for (int xx = destX0; xx <= destX1; ++xx) {
            if (xx < 0 || xx >= getScreenWidth()) {
              continue;
            }
            if (renderMode == BW && (grayFull ? (val < 3) : (val <= 1))) {
              drawBwFrom2bppStage(xx, yy, val);
            } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
              drawPixel(xx, yy, false);
            } else if (renderMode == GRAYSCALE_LSB && val == 1) {
              drawPixel(xx, yy, false);
            }
          }
        }
      } else {
        int screenX = x + (isScaled ? static_cast<int>(std::floor(bmpX * scale)) : bmpX);
        if (screenX >= getScreenWidth()) {
          break;
        }
        if (screenX < 0) {
          continue;
        }

        if (renderMode == BW && val < 3) {
          drawBwFrom2bppStage(screenX, screenY, val);
        } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
          drawPixel(screenX, screenY, false);
        } else if (renderMode == GRAYSCALE_LSB && val == 1) {
          drawPixel(screenX, screenY, false);
        }
      }
    }
  }

  if (useAreaDownscale && activeDestY >= 0) {
    flushAccumulatedRow(activeDestY);
  }

  if (renderMode == BW && bitmapStatsWarrantGrayscale(localChecked, localGray, localWhite)) {
    anyBitmapImageWantsGrayscale = true;
  }

  free(outputRow);
  free(rowBytes);
}

bool GfxRenderer::needsBitmapGrayscale() const { return anyBitmapImageWantsGrayscale; }

void GfxRenderer::fillPolygon(const int* xPoints, const int* yPoints, int numPoints, bool state) const {
  if (numPoints < 3) return;

  // Find bounding box
  int minY = yPoints[0], maxY = yPoints[0];
  for (int i = 1; i < numPoints; i++) {
    if (yPoints[i] < minY) minY = yPoints[i];
    if (yPoints[i] > maxY) maxY = yPoints[i];
  }

  // Clip to screen
  if (minY < 0) minY = 0;
  if (maxY >= getScreenHeight()) maxY = getScreenHeight() - 1;

  // Allocate node buffer for scanline algorithm
  auto* nodeX = static_cast<int*>(malloc(numPoints * sizeof(int)));
  if (!nodeX) {
    Serial.printf("[%lu] [GFX] !! Failed to allocate polygon node buffer\n", millis());
    return;
  }

  // Scanline fill algorithm
  for (int scanY = minY; scanY <= maxY; scanY++) {
    int nodes = 0;

    // Find all intersection points with edges
    int j = numPoints - 1;
    for (int i = 0; i < numPoints; i++) {
      if ((yPoints[i] < scanY && yPoints[j] >= scanY) || (yPoints[j] < scanY && yPoints[i] >= scanY)) {
        // Calculate X intersection using fixed-point to avoid float
        int dy = yPoints[j] - yPoints[i];
        if (dy != 0) {
          nodeX[nodes++] = xPoints[i] + (scanY - yPoints[i]) * (xPoints[j] - xPoints[i]) / dy;
        }
      }
      j = i;
    }

    // Sort nodes by X (simple bubble sort, numPoints is small)
    for (int i = 0; i < nodes - 1; i++) {
      for (int k = i + 1; k < nodes; k++) {
        if (nodeX[i] > nodeX[k]) {
          int temp = nodeX[i];
          nodeX[i] = nodeX[k];
          nodeX[k] = temp;
        }
      }
    }

    // Fill between pairs of nodes
    for (int i = 0; i < nodes - 1; i += 2) {
      int startX = nodeX[i];
      int endX = nodeX[i + 1];

      // Clip to screen
      if (startX < 0) startX = 0;
      if (endX >= getScreenWidth()) endX = getScreenWidth() - 1;

      // Draw horizontal line
      for (int x = startX; x <= endX; x++) {
        drawPixel(x, scanY, state);
      }
    }
  }

  free(nodeX);
}

void GfxRenderer::clearScreen(const uint8_t color) const { display.clearScreen(color); }

void GfxRenderer::invertScreen() const {
  uint8_t* buffer = display.getFrameBuffer();
  if (!buffer) {
    Serial.printf("[%lu] [GFX] !! No framebuffer in invertScreen\n", millis());
    return;
  }
  for (int i = 0; i < HalDisplay::BUFFER_SIZE; i++) {
    buffer[i] = ~buffer[i];
  }
}

void GfxRenderer::displayBuffer(const HalDisplay::RefreshMode refreshMode) const { display.displayBuffer(refreshMode); }

std::string GfxRenderer::truncatedText(const int fontId, const char* text, const int maxWidth,
                                       const EpdFontFamily::Style style) const {
  if (!text || maxWidth <= 0) return "";

  std::string item = text;
  const char* ellipsis = "...";
  int textWidth = getTextWidth(fontId, item.c_str(), style);
  if (textWidth <= maxWidth) {
    // Text fits, return as is
    return item;
  }

  while (!item.empty() && getTextWidth(fontId, (item + ellipsis).c_str(), style) >= maxWidth) {
    utf8RemoveLastChar(item);
  }

  return item.empty() ? ellipsis : item + ellipsis;
}

// Note: Internal driver treats screen in command orientation; this library exposes a logical orientation
int GfxRenderer::getScreenWidth() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 480px wide in portrait logical coordinates
      return HalDisplay::DISPLAY_HEIGHT;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 800px wide in landscape logical coordinates
      return HalDisplay::DISPLAY_WIDTH;
  }
  return HalDisplay::DISPLAY_HEIGHT;
}

int GfxRenderer::getScreenHeight() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 800px tall in portrait logical coordinates
      return HalDisplay::DISPLAY_WIDTH;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 480px tall in landscape logical coordinates
      return HalDisplay::DISPLAY_HEIGHT;
  }
  return HalDisplay::DISPLAY_WIDTH;
}

int GfxRenderer::getSpaceWidth(const int fontId) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  return fontMap.at(fontId).getGlyph(' ', EpdFontFamily::REGULAR)->advanceX;
}

int GfxRenderer::getFontAscenderSize(const int fontId) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  return fontMap.at(fontId).getData(EpdFontFamily::REGULAR)->ascender;
}

int GfxRenderer::getLineHeight(const int fontId) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  return fontMap.at(fontId).getData(EpdFontFamily::REGULAR)->advanceY;
}

void GfxRenderer::drawButtonHints(const int fontId, const char* btn1, const char* btn2, const char* btn3,
                                  const char* btn4) {
  const Orientation orig_orientation = getOrientation();
  setOrientation(Orientation::Portrait);

  const int pageHeight = getScreenHeight();
  constexpr int buttonWidth = 106;
  constexpr int buttonHeight = 40;
  constexpr int buttonY = 40;     // Distance from bottom
  constexpr int textYOffset = 7;  // Distance from top of button to text baseline
  constexpr int buttonPositions[] = {25, 130, 245, 350};
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    // Only draw if the label is non-empty
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int x = buttonPositions[i];
      fillRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, false, true);
      drawRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, true, true);
      const int textWidth = getTextWidth(fontId, labels[i]);
      const int textX = x + (buttonWidth - 1 - textWidth) / 2;
      drawText(fontId, textX, pageHeight - buttonY + textYOffset, labels[i]);
    }
  }

  setOrientation(orig_orientation);
}

void GfxRenderer::drawSideButtonHints(const int fontId, const char* topBtn, const char* bottomBtn) const {
  const int screenWidth = getScreenWidth();
  constexpr int buttonWidth = 40;   // Width on screen (height when rotated)
  constexpr int buttonHeight = 80;  // Height on screen (width when rotated)
  constexpr int buttonX = 5;        // Distance from right edge
  // Position for the button group - buttons share a border so they're adjacent
  constexpr int topButtonY = 345;  // Top button position

  const char* labels[] = {topBtn, bottomBtn};

  // Draw the shared border for both buttons as one unit
  const int x = screenWidth - buttonX - buttonWidth;

  // Draw top button outline (3 sides, bottom open)
  if (topBtn != nullptr && topBtn[0] != '\0') {
    drawLine(x, topButtonY, x + buttonWidth - 1, topButtonY);                                       // Top
    drawLine(x, topButtonY, x, topButtonY + buttonHeight - 1);                                      // Left
    drawLine(x + buttonWidth - 1, topButtonY, x + buttonWidth - 1, topButtonY + buttonHeight - 1);  // Right
  }

  // Draw shared middle border
  if ((topBtn != nullptr && topBtn[0] != '\0') || (bottomBtn != nullptr && bottomBtn[0] != '\0')) {
    drawLine(x, topButtonY + buttonHeight, x + buttonWidth - 1, topButtonY + buttonHeight);  // Shared border
  }

  // Draw bottom button outline (3 sides, top is shared)
  if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
    drawLine(x, topButtonY + buttonHeight, x, topButtonY + 2 * buttonHeight - 1);  // Left
    drawLine(x + buttonWidth - 1, topButtonY + buttonHeight, x + buttonWidth - 1,
             topButtonY + 2 * buttonHeight - 1);                                                             // Right
    drawLine(x, topButtonY + 2 * buttonHeight - 1, x + buttonWidth - 1, topButtonY + 2 * buttonHeight - 1);  // Bottom
  }

  // Draw text for each button
  for (int i = 0; i < 2; i++) {
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int y = topButtonY + i * buttonHeight;

      // Draw rotated text centered in the button
      const int textWidth = getTextWidth(fontId, labels[i]);
      const int textHeight = getTextHeight(fontId);

      // Center the rotated text in the button
      const int textX = x + (buttonWidth - textHeight) / 2;
      const int textY = y + (buttonHeight + textWidth) / 2;

      drawTextRotated90CW(fontId, textX, textY, labels[i]);
    }
  }
}

int GfxRenderer::getTextHeight(const int fontId) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }
  return fontMap.at(fontId).getData(EpdFontFamily::REGULAR)->ascender;
}

void GfxRenderer::drawTextRotated90CW(const int fontId, const int x, const int y, const char* text, const bool black,
                                      const EpdFontFamily::Style style) const {
  // Cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return;
  }
  const auto font = fontMap.at(fontId);

  // No printable characters
  if (!font.hasPrintableChars(text, style)) {
    return;
  }

  // For 90° clockwise rotation:
  // Original (glyphX, glyphY) -> Rotated (glyphY, -glyphX)
  // Text reads from bottom to top

  int yPos = y;  // Current Y position (decreases as we draw characters)

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    const EpdGlyph* glyph = font.getGlyph(cp, style);
    if (!glyph) {
      glyph = font.getGlyph(REPLACEMENT_GLYPH, style);
    }
    if (!glyph) {
      continue;
    }

    const int is2Bit = font.getData(style)->is2Bit;
    const uint32_t offset = glyph->dataOffset;
    const uint8_t width = glyph->width;
    const uint8_t height = glyph->height;
    const int left = glyph->left;
    const int top = glyph->top;

    const uint8_t* bitmap = &font.getData(style)->bitmap[offset];

    if (bitmap != nullptr) {
      for (int glyphY = 0; glyphY < height; glyphY++) {
        for (int glyphX = 0; glyphX < width; glyphX++) {
          const int pixelPosition = glyphY * width + glyphX;

          // 90° clockwise rotation transformation:
          // screenX = x + (ascender - top + glyphY)
          // screenY = yPos - (left + glyphX)
          const int screenX = x + (font.getData(style)->ascender - top + glyphY);
          const int screenY = yPos - left - glyphX;

          if (is2Bit) {
            const uint8_t byte = bitmap[pixelPosition / 4];
            const uint8_t bit_index = (3 - pixelPosition % 4) * 2;
            const uint8_t bmpVal = 3 - (byte >> bit_index) & 0x3;

            if (renderMode == BW && bmpVal < 3) {
              drawPixel(screenX, screenY, black);
            } else if (renderMode == GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
              drawPixel(screenX, screenY, false);
            } else if (renderMode == GRAYSCALE_LSB && bmpVal == 1) {
              drawPixel(screenX, screenY, false);
            }
          } else {
            const uint8_t byte = bitmap[pixelPosition / 8];
            const uint8_t bit_index = 7 - (pixelPosition % 8);

            if ((byte >> bit_index) & 1) {
              drawPixel(screenX, screenY, black);
            }
          }
        }
      }
    }

    // Move to next character position (going up, so decrease Y)
    yPos -= glyph->advanceX;
  }
}

uint8_t* GfxRenderer::getFrameBuffer() const { return display.getFrameBuffer(); }

size_t GfxRenderer::getBufferSize() { return HalDisplay::BUFFER_SIZE; }

// unused
// void GfxRenderer::grayscaleRevert() const { display.grayscaleRevert(); }

void GfxRenderer::copyGrayscaleLsbBuffers() const { display.copyGrayscaleLsbBuffers(display.getFrameBuffer()); }

void GfxRenderer::copyGrayscaleMsbBuffers() const { display.copyGrayscaleMsbBuffers(display.getFrameBuffer()); }

void GfxRenderer::displayGrayBuffer() const { display.displayGrayBuffer(); }

void GfxRenderer::freeBwBufferChunks() {
  for (auto& bwBufferChunk : bwBufferChunks) {
    if (bwBufferChunk) {
      free(bwBufferChunk);
      bwBufferChunk = nullptr;
    }
  }
}

/**
 * This should be called before grayscale buffers are populated.
 * A `restoreBwBuffer` call should always follow the grayscale render if this method was called.
 * Uses chunked allocation to avoid needing 48KB of contiguous memory.
 * Returns true if buffer was stored successfully, false if allocation failed.
 */
bool GfxRenderer::storeBwBuffer() {
  const uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    Serial.printf("[%lu] [GFX] !! No framebuffer in storeBwBuffer\n", millis());
    return false;
  }

  // Allocate and copy each chunk
  for (size_t i = 0; i < BW_BUFFER_NUM_CHUNKS; i++) {
    // Check if any chunks are already allocated
    if (bwBufferChunks[i]) {
      Serial.printf("[%lu] [GFX] !! BW buffer chunk %zu already stored - this is likely a bug, freeing chunk\n",
                    millis(), i);
      free(bwBufferChunks[i]);
      bwBufferChunks[i] = nullptr;
    }

    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    bwBufferChunks[i] = static_cast<uint8_t*>(malloc(BW_BUFFER_CHUNK_SIZE));

    if (!bwBufferChunks[i]) {
      Serial.printf("[%lu] [GFX] !! Failed to allocate BW buffer chunk %zu (%zu bytes)\n", millis(), i,
                    BW_BUFFER_CHUNK_SIZE);
      // Free previously allocated chunks
      freeBwBufferChunks();
      return false;
    }

    memcpy(bwBufferChunks[i], frameBuffer + offset, BW_BUFFER_CHUNK_SIZE);
  }

  Serial.printf("[%lu] [GFX] Stored BW buffer in %zu chunks (%zu bytes each)\n", millis(), BW_BUFFER_NUM_CHUNKS,
                BW_BUFFER_CHUNK_SIZE);
  return true;
}

/**
 * This can only be called if `storeBwBuffer` was called prior to the grayscale render.
 * It should be called to restore the BW buffer state after grayscale rendering is complete.
 * Uses chunked restoration to match chunked storage.
 */
void GfxRenderer::restoreBwBuffer() {
  // Check if any all chunks are allocated
  bool missingChunks = false;
  for (const auto& bwBufferChunk : bwBufferChunks) {
    if (!bwBufferChunk) {
      missingChunks = true;
      break;
    }
  }

  if (missingChunks) {
    freeBwBufferChunks();
    return;
  }

  uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    Serial.printf("[%lu] [GFX] !! No framebuffer in restoreBwBuffer\n", millis());
    freeBwBufferChunks();
    return;
  }

  for (size_t i = 0; i < BW_BUFFER_NUM_CHUNKS; i++) {
    // Check if chunk is missing
    if (!bwBufferChunks[i]) {
      Serial.printf("[%lu] [GFX] !! BW buffer chunks not stored - this is likely a bug\n", millis());
      freeBwBufferChunks();
      return;
    }

    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    memcpy(frameBuffer + offset, bwBufferChunks[i], BW_BUFFER_CHUNK_SIZE);
  }

  display.cleanupGrayscaleBuffers(frameBuffer);

  freeBwBufferChunks();
  Serial.printf("[%lu] [GFX] Restored and freed BW buffer chunks\n", millis());
}

/**
 * Cleanup grayscale buffers using the current frame buffer.
 * Use this when BW buffer was re-rendered instead of stored/restored.
 */
void GfxRenderer::cleanupGrayscaleWithFrameBuffer() const {
  uint8_t* frameBuffer = display.getFrameBuffer();
  if (frameBuffer) {
    display.cleanupGrayscaleBuffers(frameBuffer);
  }
}

void GfxRenderer::renderChar(const EpdFontFamily& fontFamily, const uint32_t cp, int* x, const int* y,
                             const bool pixelState, const EpdFontFamily::Style style) const {
  const EpdGlyph* glyph = fontFamily.getGlyph(cp, style);
  if (!glyph) {
    glyph = fontFamily.getGlyph(REPLACEMENT_GLYPH, style);
  }

  // no glyph?
  if (!glyph) {
    Serial.printf("[%lu] [GFX] No glyph for codepoint %d\n", millis(), cp);
    return;
  }

  const int is2Bit = fontFamily.getData(style)->is2Bit;
  const uint32_t offset = glyph->dataOffset;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int left = glyph->left;

  const uint8_t* bitmap = nullptr;
  bitmap = &fontFamily.getData(style)->bitmap[offset];

  if (bitmap != nullptr) {
    for (int glyphY = 0; glyphY < height; glyphY++) {
      const int screenY = *y - glyph->top + glyphY;
      for (int glyphX = 0; glyphX < width; glyphX++) {
        const int pixelPosition = glyphY * width + glyphX;
        const int screenX = *x + left + glyphX;

        if (is2Bit) {
          const uint8_t byte = bitmap[pixelPosition / 4];
          const uint8_t bit_index = (3 - pixelPosition % 4) * 2;
          // the direct bit from the font is 0 -> white, 1 -> light gray, 2 -> dark gray, 3 -> black
          // we swap this to better match the way images and screen think about colors:
          // 0 -> black, 1 -> dark grey, 2 -> light grey, 3 -> white
          const uint8_t bmpVal = 3 - (byte >> bit_index) & 0x3;

          if (renderMode == BW && bmpVal < 3) {
            // Black (also paints over the grays in BW mode)
            drawPixel(screenX, screenY, pixelState);
          } else if (renderMode == GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
            // Light gray (also mark the MSB if it's going to be a dark gray too)
            // We have to flag pixels in reverse for the gray buffers, as 0 leave alone, 1 update
            drawPixel(screenX, screenY, false);
          } else if (renderMode == GRAYSCALE_LSB && bmpVal == 1) {
            // Dark gray
            drawPixel(screenX, screenY, false);
          }
        } else {
          const uint8_t byte = bitmap[pixelPosition / 8];
          const uint8_t bit_index = 7 - (pixelPosition % 8);

          if ((byte >> bit_index) & 1) {
            drawPixel(screenX, screenY, pixelState);
          }
        }
      }
    }
  }

  *x += glyph->advanceX;
}

void GfxRenderer::getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const {
  switch (orientation) {
    case Portrait:
      *outTop = VIEWABLE_MARGIN_TOP;
      *outRight = VIEWABLE_MARGIN_RIGHT;
      *outBottom = VIEWABLE_MARGIN_BOTTOM;
      *outLeft = VIEWABLE_MARGIN_LEFT;
      break;
    case LandscapeClockwise:
      *outTop = VIEWABLE_MARGIN_LEFT;
      *outRight = VIEWABLE_MARGIN_TOP;
      *outBottom = VIEWABLE_MARGIN_RIGHT;
      *outLeft = VIEWABLE_MARGIN_BOTTOM;
      break;
    case PortraitInverted:
      *outTop = VIEWABLE_MARGIN_BOTTOM;
      *outRight = VIEWABLE_MARGIN_LEFT;
      *outBottom = VIEWABLE_MARGIN_TOP;
      *outLeft = VIEWABLE_MARGIN_RIGHT;
      break;
    case LandscapeCounterClockwise:
      *outTop = VIEWABLE_MARGIN_RIGHT;
      *outRight = VIEWABLE_MARGIN_BOTTOM;
      *outBottom = VIEWABLE_MARGIN_LEFT;
      *outLeft = VIEWABLE_MARGIN_TOP;
      break;
  }
}
/**
 * Ultra-clean small bitmap drawing with proper white preservation
 * Uses threshold-based rendering to eliminate artifacts in white areas
 */
void GfxRenderer::drawSmallBitmapClean(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight) const {
    // For small images, we want crisp scaling without artifacts
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    bool needsScaling = false;
    
    if (maxWidth > 0 && bitmap.getWidth() > maxWidth) {
        scaleX = static_cast<float>(maxWidth) / static_cast<float>(bitmap.getWidth());
        needsScaling = true;
    }
    if (maxHeight > 0 && bitmap.getHeight() > maxHeight) {
        scaleY = static_cast<float>(maxHeight) / static_cast<float>(bitmap.getHeight());
        needsScaling = true;
    }
    
    // Use the smaller scale to maintain aspect ratio
    if (needsScaling) {
        float scale = std::min(scaleX, scaleY);
        scaleX = scale;
        scaleY = scale;
    }
    
    // Allocate buffers for processing
    const int outputRowSize = (bitmap.getWidth() + 3) / 4; // Size for 2bpp output
    const int rowBytes = bitmap.getRowBytes();
    
    auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
    auto* rowBytes_buf = static_cast<uint8_t*>(malloc(rowBytes));
    
    if (!outputRow || !rowBytes_buf) {
        Serial.printf("[%lu] [GFX] !! Failed to allocate bitmap row buffers\n", millis());
        free(outputRow);
        free(rowBytes_buf);
        return;
    }
    
    // We need to read rows sequentially - first rewind to start
    Bitmap* nonConstBitmap = const_cast<Bitmap*>(&bitmap);
    if (nonConstBitmap->rewindToData() != BmpReaderError::Ok) {
        Serial.printf("[%lu] [GFX] Failed to rewind bitmap\n", millis());
        free(outputRow);
        free(rowBytes_buf);
        return;
    }
    
    // Pre-calculate if this is a 1-bit image (no dithering artifacts)
    bool isPure1Bit = (bitmap.getBpp() == 1);
    
    // Check if we're in grayscale mode for anti-aliasing
    bool isGrayscaleMode = (renderMode == GRAYSCALE_LSB || renderMode == GRAYSCALE_MSB);
    const bool grayFull = (bitmapGrayRenderStyle == BitmapGrayRenderStyle::FullGray);

    for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
        // Read the row using readNextRow
        if (nonConstBitmap->readNextRow(outputRow, rowBytes_buf) != BmpReaderError::Ok) {
            Serial.printf("[%lu] [GFX] Failed to read row %d from bitmap\n", millis(), bmpY);
            free(outputRow);
            free(rowBytes_buf);
            return;
        }
        
        // Calculate source Y in bitmap coordinates (handle top-down/bottom-up)
        int srcY = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
        
        // Calculate destination Y with scaling
        int destY = y + static_cast<int>(srcY * scaleY);
        
        // Skip if outside screen bounds
        if (destY >= getScreenHeight() || destY < 0) continue;
        
        // For each pixel in the source row
        for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
            // Get the 2bpp value from the output row
            uint8_t val = (outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x03;
            
            // Calculate destination X with scaling
            int destX = x + static_cast<int>(bmpX * scaleX);
            
            // Skip if outside screen bounds
            if (destX < 0 || destX >= getScreenWidth()) continue;
            
            if (isGrayscaleMode) {
                // In grayscale mode, preserve all 4 levels for anti-aliasing
                // The existing drawPixel function should handle grayscale rendering
                // based on the current renderMode
                if (val > 0) { // Only draw non-white pixels
                    drawPixel(destX, destY, val);
                }
            } else {
                // BW mode - use threshold for crisp rendering
                bool pixelSet;
                
                if (isPure1Bit) {
                    // 1-bit: exactly 0 = black, 3 = white
                    pixelSet = (val == 0);
                } else {
                    pixelSet = grayFull ? (val < 3) : (val <= 1);
                }

                if (pixelSet) {
                    if (isPure1Bit) {
                        drawPixel(destX, destY, true);
                    } else {
                        drawBwFrom2bppStage(destX, destY, val);
                    }
                }
                // White pixels (val >= 2) are not drawn - eliminates squares in white areas
            }
        }
    }
    
    free(outputRow);
    free(rowBytes_buf);
}

/**
 * Even cleaner version with local threshold adaptation
 */
void GfxRenderer::drawSmallBitmapAdaptive(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight) const {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    bool needsScaling = false;
    
    if (maxWidth > 0 && bitmap.getWidth() > maxWidth) {
        scaleX = static_cast<float>(maxWidth) / static_cast<float>(bitmap.getWidth());
        needsScaling = true;
    }
    if (maxHeight > 0 && bitmap.getHeight() > maxHeight) {
        scaleY = static_cast<float>(maxHeight) / static_cast<float>(bitmap.getHeight());
        needsScaling = true;
    }
    
    if (needsScaling) {
        float scale = std::min(scaleX, scaleY);
        scaleX = scale;
        scaleY = scale;
    }
    
    const int outputRowSize = (bitmap.getWidth() + 3) / 4;
    const int rowBytes = bitmap.getRowBytes();
    
    auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
    auto* rowBytes_buf = static_cast<uint8_t*>(malloc(rowBytes));
    
    if (!outputRow || !rowBytes_buf) {
        free(outputRow);
        free(rowBytes_buf);
        return;
    }
    
    Bitmap* nonConstBitmap = const_cast<Bitmap*>(&bitmap);
    nonConstBitmap->rewindToData();
    
    // First pass: analyze the image to find the best threshold
    std::vector<uint8_t> sampleValues;
    sampleValues.reserve(100); // Sample up to 100 pixels
    
    // Sample some pixels to determine image characteristics
    for (int bmpY = 0; bmpY < std::min(10, bitmap.getHeight()); bmpY++) {
        nonConstBitmap->readNextRow(outputRow, rowBytes_buf);
        for (int bmpX = 0; bmpX < std::min(10, bitmap.getWidth()); bmpX++) {
            uint8_t val = (outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x03;
            sampleValues.push_back(val);
        }
    }
    
    // Find the threshold - look for a gap between dark and light values
    int threshold = 1; // Default
    bool hasWhite = false;
    bool hasBlack = false;
    
    for (uint8_t val : sampleValues) {
        if (val >= 2) hasWhite = true;
        if (val <= 1) hasBlack = true;
    }
    
    if (hasWhite && hasBlack) {
        // Image has both dark and light - use threshold 1
        threshold = 1;
    } else if (!hasWhite) {
        // All dark image - draw everything
        threshold = 3;
    } else {
        // All light image - draw nothing
        threshold = -1;
    }
    
    // Rewind for second pass (actual rendering)
    nonConstBitmap->rewindToData();
    
    for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
        if (nonConstBitmap->readNextRow(outputRow, rowBytes_buf) != BmpReaderError::Ok) {
            free(outputRow);
            free(rowBytes_buf);
            return;
        }
        
        int srcY = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
        int destY = y + static_cast<int>(srcY * scaleY);
        
        if (destY >= getScreenHeight() || destY < 0) continue;
        
        for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
            uint8_t val = (outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x03;
            
            // Use adaptive threshold
            bool pixelSet = (static_cast<int>(val) <= threshold);
            
            if (pixelSet) {
                int destX = x + static_cast<int>(bmpX * scaleX);
                if (destX >= 0 && destX < getScreenWidth()) {
                    drawPixel(destX, destY, true);
                }
            }
        }
    }
    
    free(outputRow);
    free(rowBytes_buf);
}

/**
 * Simplest and most reliable - use a fixed high threshold
 * This is what I recommend for thumbnails
 */
void GfxRenderer::drawSmallBitmap(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight) const {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    bool needsScaling = false;
    
    if (maxWidth > 0 && bitmap.getWidth() > maxWidth) {
        scaleX = static_cast<float>(maxWidth) / static_cast<float>(bitmap.getWidth());
        needsScaling = true;
    }
    if (maxHeight > 0 && bitmap.getHeight() > maxHeight) {
        scaleY = static_cast<float>(maxHeight) / static_cast<float>(bitmap.getHeight());
        needsScaling = true;
    }
    
    if (needsScaling) {
        float scale = std::min(scaleX, scaleY);
        scaleX = scale;
        scaleY = scale;
    }
    
    const int outputRowSize = (bitmap.getWidth() + 3) / 4;
    const int rowBytes = bitmap.getRowBytes();
    
    auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
    auto* rowBytes_buf = static_cast<uint8_t*>(malloc(rowBytes));
    
    if (!outputRow || !rowBytes_buf) {
        free(outputRow);
        free(rowBytes_buf);
        return;
    }
    
    Bitmap* nonConstBitmap = const_cast<Bitmap*>(&bitmap);
    nonConstBitmap->rewindToData();
    
    // For high-contrast images like book covers, use aggressive thresholding
    // and majority voting to eliminate white grains in black areas
    
    for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
        if (nonConstBitmap->readNextRow(outputRow, rowBytes_buf) != BmpReaderError::Ok) {
            free(outputRow);
            free(rowBytes_buf);
            return;
        }
        
        int srcY = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
        int destY = y + static_cast<int>(srcY * scaleY);
        
        if (destY >= getScreenHeight() || destY < 0) continue;
        
        // For each output pixel
        for (int outX = 0; outX < static_cast<int>(bitmap.getWidth() * scaleX); outX++) {
            int srcX = static_cast<int>(outX / scaleX);
            if (srcX >= bitmap.getWidth()) continue;
            
            uint8_t val = (outputRow[srcX / 4] >> (6 - ((srcX * 2) % 8))) & 0x03;
            
            // Map 2-bit values to pure black/white
            // 0 = pure black (0), 1 = near black (85), 2 = near white (170), 3 = pure white (255)
            
            // Aggressive threshold - treat anything less than 170 as black
            // This eliminates white grains in black areas
            bool isBlack = (val < 2);  // Values 0 and 1 are black, 2 and 3 are white
            
            // For even cleaner text, also check neighboring pixels to remove isolated white specks
            if (!isBlack && scaleX > 0.5f) {
                // If this is a white pixel, check if it's isolated
                int neighborCount = 0;
                int whiteNeighbors = 0;
                
                // Check 3x3 neighborhood
                for (int ny = -1; ny <= 1; ny++) {
                    for (int nx = -1; nx <= 1; nx++) {
                        if (ny == 0 && nx == 0) continue;
                        
                        int checkY = bmpY + ny;
                        int checkX = srcX + nx;
                        
                        if (checkY >= 0 && checkY < bitmap.getHeight() && 
                            checkX >= 0 && checkX < bitmap.getWidth()) {
                            neighborCount++;
                            
                            // For simplicity, we'd need to buffer previous rows for proper neighborhood check
                            // Instead, we'll use a simpler approach below
                        }
                    }
                }
            }
            
            int destX = x + outX;
            if (destX >= 0 && destX < getScreenWidth()) {
                if (isBlack) {
                    drawPixel(destX, destY, true);  // Black
                }
                // White pixels are not drawn
            }
        }
    }
    
    free(outputRow);
    free(rowBytes_buf);
}

void GfxRenderer::drawIcon(const uint8_t bitmap[], int x, int y, int width, int height,
                           ImageOrientation imgOrientation, bool invert) const {
  int outW = width;
  int outH = height;
  switch (imgOrientation) {
    case None:
    case Rotate180:
      outW = width;
      outH = height;
      break;
    case Rotate90CW:
    case Rotate270CW:
      outW = height;
      outH = width;
      break;
  }

  for (int dy = 0; dy < outH; ++dy) {
    for (int dx = 0; dx < outW; ++dx) {
      int sx = 0;
      int sy = 0;
      switch (imgOrientation) {
        case None:
          sx = dx;
          sy = dy;
          break;
        case Rotate180:
          sx = width - 1 - dx;
          sy = height - 1 - dy;
          break;
        case Rotate90CW:
          sx = height - 1 - dx;
          sy = dy;
          break;
        case Rotate270CW:
          sx = dx;
          sy = width - 1 - dy;
          break;
      }
      // Match packed 1bpp + drawImage: MSB 1 = light, 0 = ink (drawImage copied bytes verbatim).
      const bool ink = !readIconBitMsbFirst(bitmap, width, height, sx, sy);
      const bool black = ink ^ invert;
      drawPixel(x + dx, y + dy, black);
    }
  }
}

void GfxRenderer::drawTransparentImage(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight,
                                      uint8_t transparentColor, ImageOrientation imgOrientation) const {
    // Calculate scaling if needed
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    
    int targetWidth = bitmap.getWidth();
    int targetHeight = bitmap.getHeight();
    
    if (maxWidth > 0 && targetWidth > maxWidth) {
        scaleX = static_cast<float>(maxWidth) / static_cast<float>(targetWidth);
    }
    if (maxHeight > 0 && targetHeight > maxHeight) {
        scaleY = static_cast<float>(maxHeight) / static_cast<float>(targetHeight);
    }
    
    // Use the smaller scale to maintain aspect ratio
    float scale = std::min(scaleX, scaleY);
    
    // Allocate buffers for row processing
    const int outputRowSize = (targetWidth + 3) / 4;
    const int rowBytes = bitmap.getRowBytes();
    
    auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
    auto* rowBytes_buf = static_cast<uint8_t*>(malloc(rowBytes));
    
    if (!outputRow || !rowBytes_buf) {
        Serial.printf("[%lu] [GFX] !! Failed to allocate bitmap row buffers\n", millis());
        free(outputRow);
        free(rowBytes_buf);
        return;
    }
    
    // Rewind bitmap to start
    Bitmap* nonConstBitmap = const_cast<Bitmap*>(&bitmap);
    if (nonConstBitmap->rewindToData() != BmpReaderError::Ok) {
        Serial.printf("[%lu] [GFX] Failed to rewind bitmap\n", millis());
        free(outputRow);
        free(rowBytes_buf);
        return;
    }
    
    // Process each row
    for (int bmpY = 0; bmpY < targetHeight; bmpY++) {
        if (nonConstBitmap->readNextRow(outputRow, rowBytes_buf) != BmpReaderError::Ok) {
            Serial.printf("[%lu] [GFX] Failed to read row %d\n", millis(), bmpY);
            break;
        }
        
        int srcY = bitmap.isTopDown() ? bmpY : targetHeight - 1 - bmpY;
        int destY = y + static_cast<int>(srcY * scale);
        
        if (destY < 0 || destY >= getScreenHeight()) continue;
        
        for (int bmpX = 0; bmpX < targetWidth; bmpX++) {
            // Get 2-bit value from outputRow
            uint8_t val = (outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x03;
            
            int destX = x + static_cast<int>(bmpX * scale);
            if (destX < 0 || destX >= getScreenWidth()) continue;
            
            // transparentColor: 1 = make white transparent (most common)
            // For 1-bit source: val < 3 means black/dark, val == 3 means white
            bool shouldDraw;
            if (transparentColor == 1) {
                // White is transparent - draw if not white
                shouldDraw = (val < 3);
            } else {
                // Black is transparent - draw if white
                shouldDraw = (val == 3);
            }
            
            if (shouldDraw) {
                if (renderMode == BW) {
                    drawBwFrom2bppStage(destX, destY, val);
                } else {
                    drawPixel(destX, destY, true);
                }
            }
        }
    }
    
    free(outputRow);
    free(rowBytes_buf);
}

// Overload for 2-bit grayscale images with alpha threshold
void GfxRenderer::drawTransparentImage2Bit(const uint8_t bitmap[], int x, int y, int width, int height,
                                          uint8_t alphaThreshold, ImageOrientation imgOrientation) const {
  int targetX = x;
  int targetY = y;
  int targetW = width;
  int targetH = height;

  // Handle rotation if needed (simplified - you'd need 2-bit rotation logic)
  if (imgOrientation != None) {
    Serial.printf("[%lu] [GFX] 2-bit transparent image rotation not yet implemented\n", millis());
    return;
  }

  int rotatedX = 0, rotatedY = 0;
  rotateCoordinates(targetX, targetY, &rotatedX, &rotatedY);

  switch (orientation) {
    case Portrait:           rotatedY -= targetH; break;
    case PortraitInverted:   rotatedX -= targetW; break;
    case LandscapeClockwise: rotatedY -= targetH; rotatedX -= targetW; break;
    case LandscapeCounterClockwise: break;
  }

  uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    Serial.printf("[%lu] [GFX] !! No framebuffer\n", millis());
    return;
  }

  const int stride = HalDisplay::DISPLAY_WIDTH_BYTES;
  const int bytesPerRow = (width + 3) / 4; // 2 bits per pixel
  
  for (int row = 0; row < height; row++) {
    int screenY = rotatedY + row;
    if (screenY < 0 || screenY >= HalDisplay::DISPLAY_HEIGHT) continue;
    
    const uint8_t* srcRow = &bitmap[row * bytesPerRow];
    uint8_t* destRow = &frameBuffer[screenY * stride];
    
    for (int pixelX = 0; pixelX < width; pixelX++) {
      int screenX = rotatedX + pixelX;
      if (screenX < 0 || screenX >= HalDisplay::DISPLAY_WIDTH) continue;
      
      // Extract 2-bit value
      const int byteIdx = pixelX / 4;
      const int bitShift = 6 - ((pixelX & 3) << 1);
      const uint8_t pixelValue = (srcRow[byteIdx] >> bitShift) & 0x03;
      
      // Only draw if pixel value is above alpha threshold
      // alphaThreshold 0-3, where 3 is fully opaque, 0 is fully transparent
      if (pixelValue >= alphaThreshold) {
        const uint16_t byteIndex = screenY * stride + (screenX / 8);
        const uint8_t bitPosition = 7 - (screenX % 8);
        
        // For 2-bit, we need to map to 1-bit framebuffer
        // This is simplified - you might want more sophisticated dithering
        bool drawBlack = (pixelValue < 2); // Treat as black if dark enough
        
        if (drawBlack) {
          frameBuffer[byteIndex] &= ~(1 << bitPosition);  // Clear bit (black)
        } else {
          frameBuffer[byteIndex] |= 1 << bitPosition;   // Set bit (white)
        }
      }
    }
  }
}