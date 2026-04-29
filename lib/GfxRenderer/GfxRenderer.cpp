/**
 * @file GfxRenderer.cpp
 * @brief Definitions for GfxRenderer.
 */

#include "GfxRenderer.h"

#include <Utf8.h>
#include <memory>
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

/** Pixels outside the rounded rect (same geometry as `fillRect` with `rounded`). */
void maskBitmapCornersOutsideRounded(const GfxRenderer& gfx, const int x, const int y, const int drawnW,
                                     const int drawnH, const GfxRenderer::BitmapRoundedCornerOutside style) {
  if (style == GfxRenderer::BitmapRoundedCornerOutside::None) {
    return;
  }
  if (drawnW < 3 || drawnH < 3) {
    return;
  }
  const int r = roundedRectCornerRadius(drawnW, drawnH);
  if (r < 1) {
    return;
  }
  const int sw = gfx.getScreenWidth();
  const int sh = gfx.getScreenHeight();

  auto applyCorner = [&](const int px, const int py) {
    if (px < 0 || px >= sw || py < 0 || py >= sh) {
      return;
    }
    if (style == GfxRenderer::BitmapRoundedCornerOutside::PaperOutside) {
      gfx.drawPixel(px, py, false);
    } else {
      // Screen-fixed 1/4 tone so corner touch-up matches carousel/list dithers that use the same lattice
      // (see RecentActivity drawFlowCarouselBackdrop*), independent of bitmap (x,y).
      const bool ink = ((px & 1) == 0) && ((py & 1) == 0);
      gfx.drawPixel(px, py, ink);
    }
  };

  for (int cy = 0; cy < r; ++cy) {
    const int py = y + cy;
    const int span = cornerSpanFromRy(r, r - cy);
    for (int px = x; px < x + r - span; ++px) {
      applyCorner(px, py);
    }
  }

  for (int cy = 0; cy < r; ++cy) {
    const int py = y + cy;
    const int span = cornerSpanFromRy(r, r - cy);
    for (int px = x + drawnW - r + span; px < x + drawnW; ++px) {
      applyCorner(px, py);
    }
  }

  for (int cy = 0; cy < r; ++cy) {
    const int py = y + drawnH - 1 - cy;
    const int span = cornerSpanFromRy(r, r - cy);
    for (int px = x; px < x + r - span; ++px) {
      applyCorner(px, py);
    }
  }

  for (int cy = 0; cy < r; ++cy) {
    const int py = y + drawnH - 1 - cy;
    const int span = cornerSpanFromRy(r, r - cy);
    for (int px = x + drawnW - r + span; px < x + drawnW; ++px) {
      applyCorner(px, py);
    }
  }
}

inline bool bwShouldInk2bpp(const uint8_t stage03, const GfxRenderer::BitmapGrayRenderStyle gs) {
  const uint8_t st = stage03 & 3u;
  if (gs == GfxRenderer::BitmapGrayRenderStyle::VeryDark) {
    return st < 3u;
  }
  if (gs == GfxRenderer::BitmapGrayRenderStyle::Balanced) {
    return st <= 1u;  // skip light-gray stage in low mode
  }
  return st < 3u;
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
  
  for (int h = 0; h <= r; ++h) {
    const int span = cornerSpanFromRy(r, r - h);
    gfx.drawPixel(x + r - span, y + h, state);
  }

  
  for (int h = 0; h <= r; ++h) {
    const int span = cornerSpanFromRy(r, r - h);
    gfx.drawPixel(x + width - r - 1 + span, y + h, state);
  }

  
  for (int h = 0; h <= r; ++h) {
    const int span = cornerSpanFromRy(r, r - h);
    gfx.drawPixel(x + r - span, y + height - 1 - h, state);
  }

  
  for (int h = 0; h <= r; ++h) {
    const int span = cornerSpanFromRy(r, r - h);
    gfx.drawPixel(x + width - r - 1 + span, y + height - 1 - h, state);
  }
}
}  

void GfxRenderer::insertFont(const int fontId, EpdFontFamily font) {
  fontMap.erase(fontId);
  fontMap.emplace(fontId, std::move(font));
}

void GfxRenderer::rotateCoordinates(const int x, const int y, int* rotatedX, int* rotatedY) const {
  switch (orientation) {
    case Portrait: {
      
      
      *rotatedX = y;
      *rotatedY = HalDisplay::DISPLAY_HEIGHT - 1 - x;
      break;
    }
    case LandscapeClockwise: {
      
      *rotatedX = HalDisplay::DISPLAY_WIDTH - 1 - x;
      *rotatedY = HalDisplay::DISPLAY_HEIGHT - 1 - y;
      break;
    }
    case PortraitInverted: {
      
      
      *rotatedX = HalDisplay::DISPLAY_WIDTH - 1 - y;
      *rotatedY = x;
      break;
    }
    case LandscapeCounterClockwise: {
      
      *rotatedX = x;
      *rotatedY = y;
      break;
    }
  }
}

void GfxRenderer::drawPixel(const int x, const int y, const bool state) const {
  uint8_t* frameBuffer = display.getFrameBuffer();

  
  if (!frameBuffer) {
    Serial.printf("[%lu] [GFX] !! No framebuffer\n", millis());
    return;
  }

  int rotatedX = 0;
  int rotatedY = 0;
  rotateCoordinates(x, y, &rotatedX, &rotatedY);

  
  if (rotatedX < 0 || rotatedX >= HalDisplay::DISPLAY_WIDTH || rotatedY < 0 || rotatedY >= HalDisplay::DISPLAY_HEIGHT) {
    Serial.printf("[%lu] [GFX] !! Outside range (%d, %d) -> (%d, %d)\n", millis(), x, y, rotatedX, rotatedY);
    return;
  }

  
  const uint16_t byteIndex = rotatedY * HalDisplay::DISPLAY_WIDTH_BYTES + (rotatedX / 8);
  const uint8_t bitPosition = 7 - (rotatedX % 8);  

  if (state) {
    frameBuffer[byteIndex] &= ~(1 << bitPosition);  
  } else {
    frameBuffer[byteIndex] |= 1 << bitPosition;  
  }
}

int GfxRenderer::getTextWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }
  const auto& family = fontMap.at(fontId);
  if (streamingFonts.count(family.getData(style))) {
    return getStreamingTextWidth(family, text, style);
  }
  int w = 0, h = 0;
  family.getTextDimensions(text, &w, &h, style);
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

  
  if (text == nullptr || *text == '\0') {
    return;
  }

  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return;
  }
  const auto font = fontMap.at(fontId);

  
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
    
    Serial.printf("[%lu] [GFX] Line drawing not supported\n", millis());
  }
}

void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const bool state, const bool rounded) const {
  if (!rounded) {
    
    drawLine(x, y, x + width - 1, y, state);
    drawLine(x + width - 1, y, x + width - 1, y + height - 1, state);
    drawLine(x + width - 1, y + height - 1, x, y + height - 1, state);
    drawLine(x, y, x, y + height - 1, state);
  } else {
    
    const int radius = roundedRectCornerRadius(width, height);
    
    
    drawLine(x + radius, y, x + width - radius - 1, y, state);
    
    
    drawLine(x + radius, y + height - 1, x + width - radius - 1, y + height - 1, state);
    
    
    drawLine(x, y + radius, x, y + height - radius - 1, state);
    
    
    drawLine(x + width - 1, y + radius, x + width - 1, y + height - radius - 1, state);

    
    drawRoundedRectCornerOutlines(*this, x, y, width, height, radius, state);
  }
}

void GfxRenderer::fillRect(const int x, const int y, const int width, const int height, const FillTone tone,
                           const bool rounded) const {
  if (tone == FillTone::Gray) {
    if (rounded) {
      
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

  if (v == 3u) return;  

  if (v == 0u) {
    drawPixel(px, py, true);
    return;
  }

  
  static const uint8_t kBayer2[4] = {0, 2, 3, 1};
  const uint8_t t = kBayer2[((py & 1) << 1) | (px & 1)];
  const uint8_t tScaled = (t * 16) / 4;  
  const bool veryDark = (bitmapGrayRenderStyle == BitmapGrayRenderStyle::VeryDark);
  const bool dark = (bitmapGrayRenderStyle == BitmapGrayRenderStyle::Dark) || veryDark;

  if (v == 1u) {
    drawPixel(px, py, tScaled < (veryDark ? 14u : (dark ? 13u : 12u)));
    return;
  }

  // Keep light-gray looking solid-ish like dark gray in higher contrast modes.
  drawPixel(px, py, tScaled < (veryDark ? 14u : (dark ? 13u : 6u)));
}

void GfxRenderer::drawBitmap(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight,
                             const float cropX, const float cropY,
                             const BitmapRoundedCornerOutside roundedOutside) const {
  if (bitmap.is1Bit() && cropX == 0.0f && cropY == 0.0f) {
    drawBitmap1Bit(bitmap, x, y, maxWidth, maxHeight, roundedOutside);
    return;
  }

  float scale = 1.0f;
  bool isScaled = false;
  const int cropPixX = static_cast<int>(std::floor(bitmap.getWidth() * cropX / 2.0f));
  const int cropPixY = static_cast<int>(std::floor(bitmap.getHeight() * cropY / 2.0f));

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
  if (hasTargetBounds && std::abs(fitScale - 1.0f) > kScaleEps) {
    scale = fitScale;
    isScaled = true;
  }
  const bool replicateUpscale = isScaled && scale > 1.0f + kScaleEps;
  const int tFirstY = -cropPixY + (bitmap.isTopDown() ? cropPixY : bitmap.getHeight() - 1 - cropPixY);

  const int contentW = bitmap.getWidth() - 2 * cropPixX;
  const int contentH = bitmap.getHeight() - 2 * cropPixY;

  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));
  if (!outputRow || !rowBytes) {
    free(outputRow);
    free(rowBytes);
    return;
  }

  const BitmapGrayRenderStyle grayStyle = bitmapGrayRenderStyle;

  auto pixel2bpp = [](const uint8_t* row, const int px) -> uint8_t {
    return static_cast<uint8_t>((row[px / 4] >> (6 - ((px * 2) % 8))) & 0x3);
  };

  auto emitPixel = [&](const int screenX, const int screenY, const uint8_t val) {
    if (renderMode == BW) {
      if (bwShouldInk2bpp(val, grayStyle)) {
        drawBwFrom2bppStage(screenX, screenY, val);
      }
    } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
      drawPixel(screenX, screenY, false);
    } else if (renderMode == GRAYSCALE_LSB && val == 1) {
      drawPixel(screenX, screenY, false);
    }
  };

  for (int bmpY = 0; bmpY < (bitmap.getHeight() - cropPixY); bmpY++) {
    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      free(outputRow);
      free(rowBytes);
      return;
    }

    if (bmpY < cropPixY) {
      continue;
    }

    const int t = -cropPixY + (bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY);
    const int uY = bitmap.isTopDown() ? (t - tFirstY) : (tFirstY - t);

    if (replicateUpscale) {
      const int y0 = y + static_cast<int>(std::floor(static_cast<float>(uY) * scale));
      const int y1 = y + static_cast<int>(std::floor(static_cast<float>(uY + 1) * scale));
      if (y0 >= getScreenHeight()) {
        break;
      }
      for (int sy = y0; sy < y1 && sy < getScreenHeight(); ++sy) {
        if (sy < 0) {
          continue;
        }
        for (int bmpX = cropPixX; bmpX < bitmap.getWidth() - cropPixX; bmpX++) {
          const int srcCol = bmpX - cropPixX;
          const int x0 = x + static_cast<int>(std::floor(static_cast<float>(srcCol) * scale));
          const int x1 = x + static_cast<int>(std::floor(static_cast<float>(srcCol + 1) * scale));
          const uint8_t val = pixel2bpp(outputRow, bmpX);
          for (int sx = x0; sx < x1 && sx < getScreenWidth(); ++sx) {
            if (sx < 0) {
              continue;
            }
            emitPixel(sx, sy, val);
          }
        }
      }
    } else {
      int screenY = t;
      if (isScaled) {
        screenY = static_cast<int>(std::floor(static_cast<float>(t) * scale));
      }
      screenY += y;
      if (screenY >= getScreenHeight()) {
        break;
      }
      if (screenY < 0) {
        continue;
      }

      for (int bmpX = cropPixX; bmpX < bitmap.getWidth() - cropPixX; bmpX++) {
        int screenX = bmpX - cropPixX;
        if (isScaled) {
          screenX = static_cast<int>(std::floor(static_cast<float>(screenX) * scale));
        }
        screenX += x;
        if (screenX >= getScreenWidth()) {
          break;
        }
        if (screenX < 0) {
          continue;
        }

        const uint8_t val = pixel2bpp(outputRow, bmpX);
        emitPixel(screenX, screenY, val);
      }
    }
  }

  if (roundedOutside != BitmapRoundedCornerOutside::None && contentW > 0 && contentH > 0) {
    const int drawnW = static_cast<int>(std::floor(static_cast<float>(contentW) * scale));
    const int drawnH = static_cast<int>(std::floor(static_cast<float>(contentH) * scale));
    const int maskW = maxWidth > 0 ? maxWidth : drawnW;
    const int maskH = maxHeight > 0 ? maxHeight : drawnH;
    if (maskW > 0 && maskH > 0) {
      maskBitmapCornersOutsideRounded(*this, x, y, maskW, maskH, roundedOutside);
    }
  }

  free(outputRow);
  free(rowBytes);
}

void GfxRenderer::drawBitmap1Bit(const Bitmap& bitmap, const int x, const int y, const int maxWidth,
                                 const int maxHeight, const BitmapRoundedCornerOutside roundedOutside) const {
  constexpr float kScaleEps = 1e-5f;
  constexpr float kHuge = 1e9f;
  float scale = 1.0f;
  bool isScaled = false;
  const int bw = bitmap.getWidth();
  const bool hasW = maxWidth > 0;
  const bool hasH = maxHeight > 0;
  if (hasW || hasH) {
    const float fitW = hasW ? static_cast<float>(maxWidth) / static_cast<float>(bw) : kHuge;
    const float fitH = hasH ? static_cast<float>(maxHeight) / static_cast<float>(bitmap.getHeight()) : kHuge;
    const float fitScale = (hasW && hasH) ? std::min(fitW, fitH) : (hasW ? fitW : fitH);
    if (std::abs(fitScale - 1.0f) > kScaleEps) {
      scale = fitScale;
      isScaled = true;
    }
  }
  const bool replicateUpscale = isScaled && scale > 1.0f + kScaleEps;

  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));
  if (!outputRow || !rowBytes) {
    free(outputRow);
    free(rowBytes);
    return;
  }

  const BitmapGrayRenderStyle grayStyle = bitmapGrayRenderStyle;

  auto pixel2bpp = [](const uint8_t* row, const int px) -> uint8_t {
    return static_cast<uint8_t>((row[px / 4] >> (6 - ((px * 2) % 8))) & 0x3);
  };

  auto emitPixel1 = [&](const int screenX, const int screenY, const uint8_t val) {
    if (renderMode == BW) {
      if (bwShouldInk2bpp(val, grayStyle)) {
        drawBwFrom2bppStage(screenX, screenY, val);
      }
    } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
      drawPixel(screenX, screenY, false);
    } else if (renderMode == GRAYSCALE_LSB && val == 1) {
      drawPixel(screenX, screenY, false);
    }
  };

  for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      free(outputRow);
      free(rowBytes);
      return;
    }

    const int vr = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;

    if (replicateUpscale) {
      const int y0 = y + static_cast<int>(std::floor(static_cast<float>(vr) * scale));
      const int y1 = y + static_cast<int>(std::floor(static_cast<float>(vr + 1) * scale));
      if (y0 >= getScreenHeight()) {
        continue;
      }
      for (int sy = y0; sy < y1 && sy < getScreenHeight(); ++sy) {
        if (sy < 0) {
          continue;
        }
        for (int bmpX = 0; bmpX < bw; bmpX++) {
          const int x0 = x + static_cast<int>(std::floor(static_cast<float>(bmpX) * scale));
          const int x1 = x + static_cast<int>(std::floor(static_cast<float>(bmpX + 1) * scale));
          const uint8_t val = pixel2bpp(outputRow, bmpX);
          for (int sx = x0; sx < x1 && sx < getScreenWidth(); ++sx) {
            if (sx < 0) {
              continue;
            }
            emitPixel1(sx, sy, val);
          }
        }
      }
    } else {
      const int screenY = y + (isScaled ? static_cast<int>(std::floor(static_cast<float>(vr) * scale)) : vr);
      if (screenY >= getScreenHeight()) {
        continue;
      }
      if (screenY < 0) {
        continue;
      }

      for (int bmpX = 0; bmpX < bw; bmpX++) {
        const int screenX = x + (isScaled ? static_cast<int>(std::floor(static_cast<float>(bmpX) * scale)) : bmpX);
        if (screenX >= getScreenWidth()) {
          break;
        }
        if (screenX < 0) {
          continue;
        }

        const uint8_t val = pixel2bpp(outputRow, bmpX);
        emitPixel1(screenX, screenY, val);
      }
    }
  }

  if (roundedOutside != BitmapRoundedCornerOutside::None) {
    const int drawnW = static_cast<int>(std::floor(static_cast<float>(bw) * scale));
    const int drawnH = static_cast<int>(std::floor(static_cast<float>(bitmap.getHeight()) * scale));
    const int maskW = maxWidth > 0 ? maxWidth : drawnW;
    const int maskH = maxHeight > 0 ? maxHeight : drawnH;
    if (maskW > 0 && maskH > 0) {
      maskBitmapCornersOutsideRounded(*this, x, y, maskW, maskH, roundedOutside);
    }
  }

  free(outputRow);
  free(rowBytes);
}

void GfxRenderer::fillPolygon(const int* xPoints, const int* yPoints, int numPoints, bool state) const {
  if (numPoints < 3) return;

  
  int minY = yPoints[0], maxY = yPoints[0];
  for (int i = 1; i < numPoints; i++) {
    if (yPoints[i] < minY) minY = yPoints[i];
    if (yPoints[i] > maxY) maxY = yPoints[i];
  }

  
  if (minY < 0) minY = 0;
  if (maxY >= getScreenHeight()) maxY = getScreenHeight() - 1;

  
  auto* nodeX = static_cast<int*>(malloc(numPoints * sizeof(int)));
  if (!nodeX) {
    Serial.printf("[%lu] [GFX] !! Failed to allocate polygon node buffer\n", millis());
    return;
  }

  
  for (int scanY = minY; scanY <= maxY; scanY++) {
    int nodes = 0;

    
    int j = numPoints - 1;
    for (int i = 0; i < numPoints; i++) {
      if ((yPoints[i] < scanY && yPoints[j] >= scanY) || (yPoints[j] < scanY && yPoints[i] >= scanY)) {
        
        int dy = yPoints[j] - yPoints[i];
        if (dy != 0) {
          nodeX[nodes++] = xPoints[i] + (scanY - yPoints[i]) * (xPoints[j] - xPoints[i]) / dy;
        }
      }
      j = i;
    }

    
    for (int i = 0; i < nodes - 1; i++) {
      for (int k = i + 1; k < nodes; k++) {
        if (nodeX[i] > nodeX[k]) {
          int temp = nodeX[i];
          nodeX[i] = nodeX[k];
          nodeX[k] = temp;
        }
      }
    }

    
    for (int i = 0; i < nodes - 1; i += 2) {
      int startX = nodeX[i];
      int endX = nodeX[i + 1];

      
      if (startX < 0) startX = 0;
      if (endX >= getScreenWidth()) endX = getScreenWidth() - 1;

      
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
    
    return item;
  }

  while (!item.empty() && getTextWidth(fontId, (item + ellipsis).c_str(), style) >= maxWidth) {
    utf8RemoveLastChar(item);
  }

  return item.empty() ? ellipsis : item + ellipsis;
}


int GfxRenderer::getScreenWidth() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      
      return HalDisplay::DISPLAY_HEIGHT;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      
      return HalDisplay::DISPLAY_WIDTH;
  }
  return HalDisplay::DISPLAY_HEIGHT;
}

int GfxRenderer::getScreenHeight() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      
      return HalDisplay::DISPLAY_WIDTH;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      
      return HalDisplay::DISPLAY_HEIGHT;
  }
  return HalDisplay::DISPLAY_WIDTH;
}

int GfxRenderer::getSpaceWidth(const int fontId) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  const EpdFontFamily& font = fontMap.at(fontId);
  const EpdFontData* fontData = font.getData(EpdFontFamily::REGULAR);
  if (!fontData) {
    return 0;
  }

  const auto streamIt = streamingFonts.find(fontData);
  if (streamIt != streamingFonts.end()) {
    EpdGlyph g{};
    constexpr uint32_t kSpace = 0x20;
    if (!streamIt->second->getGlyphMetadata(kSpace, g)) {
      if (!streamIt->second->getGlyphMetadata(REPLACEMENT_GLYPH, g)) {
        return 0;
      }
    }
    return g.advanceX;
  }

  const EpdGlyph* glyph = font.getGlyph(' ', EpdFontFamily::REGULAR);
  if (!glyph) {
    glyph = font.getGlyph(REPLACEMENT_GLYPH, EpdFontFamily::REGULAR);
  }
  return glyph ? glyph->advanceX : 0;
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
  constexpr int buttonY = 40;     
  constexpr int textYOffset = 7;  
  constexpr int buttonPositions[] = {25, 130, 245, 350};
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    
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
  constexpr int buttonWidth = 40;   
  constexpr int buttonHeight = 80;  
  constexpr int buttonX = 5;        
  
  constexpr int topButtonY = 345;  

  const char* labels[] = {topBtn, bottomBtn};

  
  const int x = screenWidth - buttonX - buttonWidth;

  
  if (topBtn != nullptr && topBtn[0] != '\0') {
    drawLine(x, topButtonY, x + buttonWidth - 1, topButtonY);                                       
    drawLine(x, topButtonY, x, topButtonY + buttonHeight - 1);                                      
    drawLine(x + buttonWidth - 1, topButtonY, x + buttonWidth - 1, topButtonY + buttonHeight - 1);  
  }

  
  if ((topBtn != nullptr && topBtn[0] != '\0') || (bottomBtn != nullptr && bottomBtn[0] != '\0')) {
    drawLine(x, topButtonY + buttonHeight, x + buttonWidth - 1, topButtonY + buttonHeight);  
  }

  
  if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
    drawLine(x, topButtonY + buttonHeight, x, topButtonY + 2 * buttonHeight - 1);  
    drawLine(x + buttonWidth - 1, topButtonY + buttonHeight, x + buttonWidth - 1,
             topButtonY + 2 * buttonHeight - 1);                                                             
    drawLine(x, topButtonY + 2 * buttonHeight - 1, x + buttonWidth - 1, topButtonY + 2 * buttonHeight - 1);  
  }

  
  for (int i = 0; i < 2; i++) {
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int y = topButtonY + i * buttonHeight;

      
      const int textWidth = getTextWidth(fontId, labels[i]);
      const int textHeight = getTextHeight(fontId);

      
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
  if (text == nullptr || *text == '\0' || fontMap.count(fontId) == 0) {
    return;
  }
  const auto font = fontMap.at(fontId);
  if (!font.hasPrintableChars(text, style)) {
    return;
  }

  const EpdFontData* fontData = font.getData(style);
  if (!fontData) {
    return;
  }
  auto it = streamingFonts.find(fontData);
  int yPos = y;

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    EpdGlyph glyphStorage;
    const EpdGlyph* glyph = nullptr;

    if (it != streamingFonts.end()) {
      if (!it->second->getGlyphMetadata(cp, glyphStorage)) {
        it->second->getGlyphMetadata(REPLACEMENT_GLYPH, glyphStorage);
      }
      glyph = &glyphStorage;
    } else {
      glyph = font.getGlyph(cp, style);
      if (!glyph) {
        glyph = font.getGlyph(REPLACEMENT_GLYPH, style);
      }
    }

    if (!glyph) {
      continue;
    }

    uint8_t localStackBuffer[1024];
    const uint8_t* bitmap = nullptr;

    if (fontData->bitmap != nullptr) {
      bitmap = &fontData->bitmap[glyph->dataOffset];
    } else if (it != streamingFonts.end()) {
      if (it->second->getGlyphBitmap(glyph->dataOffset, glyph->dataLength, localStackBuffer)) {
        bitmap = localStackBuffer;
      }
    }

    if (bitmap != nullptr) {
      const int is2Bit = fontData->is2Bit;
      for (int glyphY = 0; glyphY < glyph->height; glyphY++) {
        for (int glyphX = 0; glyphX < glyph->width; glyphX++) {
          const int pixelPosition = glyphY * glyph->width + glyphX;
          const int screenX = x + (fontData->ascender - glyph->top + glyphY);
          const int screenY = yPos - glyph->left - glyphX;

          if (is2Bit) {
            const uint8_t byte = bitmap[pixelPosition / 4];
            const uint8_t bit_index = (3 - (pixelPosition % 4)) * 2;
            const uint8_t bmpVal = 3 - ((byte >> bit_index) & 0x3);

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
    yPos -= glyph->advanceX;
  }
}

uint8_t* GfxRenderer::getFrameBuffer() const { return display.getFrameBuffer(); }

size_t GfxRenderer::getBufferSize() { return HalDisplay::BUFFER_SIZE; }




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

  
  for (size_t i = 0; i < BW_BUFFER_NUM_CHUNKS; i++) {
    
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

void GfxRenderer::resetTransientReaderState() {
  freeBwBufferChunks();
  renderMode = BW;
  cleanupGrayscaleWithFrameBuffer();
}

void GfxRenderer::renderChar(const EpdFontFamily& fontFamily, const uint32_t cp, int* x, const int* y,
                             const bool pixelState, const EpdFontFamily::Style style) const {
  EpdGlyph glyphStorage;
  const EpdGlyph* glyph = nullptr;
  const EpdFontData* fontData = fontFamily.getData(style);
  if (!fontData) {
    return;
  }
  auto it = streamingFonts.find(fontData);
  if (it != streamingFonts.end()) {
    if (!it->second->getGlyphMetadata(cp, glyphStorage)) {
      it->second->getGlyphMetadata(REPLACEMENT_GLYPH, glyphStorage);
    }
    glyph = &glyphStorage;
  } else {
    glyph = fontFamily.getGlyph(cp, style);
  }

  if (!glyph) {
    glyph = fontFamily.getGlyph(REPLACEMENT_GLYPH, style);
  }

  if (!glyph) {
    return;
  }

  const int is2Bit = fontData->is2Bit;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int left = glyph->left;

  const uint8_t* bitmap = nullptr;
  uint8_t localStackBuffer[2048];

  if (fontData->bitmap != nullptr) {
    bitmap = &fontData->bitmap[glyph->dataOffset];
  } else if (it != streamingFonts.end()) {
    const size_t dataLen = glyph->dataLength;
    if (dataLen <= sizeof(localStackBuffer)) {
      if (it->second->getGlyphBitmap(glyph->dataOffset, dataLen, localStackBuffer)) {
        bitmap = localStackBuffer;
      } else {
        *x += glyph->advanceX;
        return;
      }
    } else {
      /** Avoid heap `new` (throws bad_alloc when heap is low after ZIP/fonts). Draw row stripes from SD. */
      constexpr size_t kMaxRowBytes = 512;
      const size_t rowBytes =
          is2Bit ? (static_cast<size_t>(width) + 3u) / 4u : (static_cast<size_t>(width) + 7u) / 8u;
      if (rowBytes == 0 || rowBytes > kMaxRowBytes) {
        *x += glyph->advanceX;
        return;
      }
      uint8_t rowBuf[kMaxRowBytes];
      for (int glyphY = 0; glyphY < height; glyphY++) {
        const uint32_t rowOff =
            glyph->dataOffset + static_cast<uint32_t>(glyphY) * static_cast<uint32_t>(rowBytes);
        if (!it->second->getGlyphBitmap(rowOff, rowBytes, rowBuf)) {
          *x += glyph->advanceX;
          return;
        }
        const int screenY = *y - glyph->top + glyphY;
        for (int glyphX = 0; glyphX < width; glyphX++) {
          const int screenX = *x + left + glyphX;
          if (is2Bit) {
            const uint8_t byte = rowBuf[glyphX / 4];
            const uint8_t bit_index = (3 - (glyphX % 4)) * 2;
            const uint8_t bmpVal = 3 - ((byte >> bit_index) & 0x3);

            if (renderMode == BW && bmpVal < 3) {
              drawPixel(screenX, screenY, pixelState);
            } else if (renderMode == GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
              drawPixel(screenX, screenY, false);
            } else if (renderMode == GRAYSCALE_LSB && bmpVal == 1) {
              drawPixel(screenX, screenY, false);
            }
          } else {
            const uint8_t byte = rowBuf[glyphX / 8];
            const uint8_t bit_index = 7 - (glyphX % 8);
            if ((byte >> bit_index) & 1) {
              drawPixel(screenX, screenY, pixelState);
            }
          }
        }
      }
      *x += glyph->advanceX;
      return;
    }
  }

  if (bitmap != nullptr) {
    for (int glyphY = 0; glyphY < height; glyphY++) {
      const int screenY = *y - glyph->top + glyphY;
      for (int glyphX = 0; glyphX < width; glyphX++) {
        const int pixelPosition = glyphY * width + glyphX;
        const int screenX = *x + left + glyphX;

        if (is2Bit) {
          const uint8_t byte = bitmap[pixelPosition / 4];
          const uint8_t bit_index = (3 - (pixelPosition % 4)) * 2;
          const uint8_t bmpVal = 3 - ((byte >> bit_index) & 0x3);

          if (renderMode == BW && bmpVal < 3) {
            drawPixel(screenX, screenY, pixelState);
          } else if (renderMode == GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
            drawPixel(screenX, screenY, false);
          } else if (renderMode == GRAYSCALE_LSB && bmpVal == 1) {
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
        Serial.printf("[%lu] [GFX] !! Failed to allocate bitmap row buffers\n", millis());
        free(outputRow);
        free(rowBytes_buf);
        return;
    }
    
    
    Bitmap* nonConstBitmap = const_cast<Bitmap*>(&bitmap);
    if (nonConstBitmap->rewindToData() != BmpReaderError::Ok) {
        Serial.printf("[%lu] [GFX] Failed to rewind bitmap\n", millis());
        free(outputRow);
        free(rowBytes_buf);
        return;
    }
    
    
    bool isPure1Bit = (bitmap.getBpp() == 1);
    
    
    bool isGrayscaleMode = (renderMode == GRAYSCALE_LSB || renderMode == GRAYSCALE_MSB);
    const BitmapGrayRenderStyle grayStyle = bitmapGrayRenderStyle;

    for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
        
        if (nonConstBitmap->readNextRow(outputRow, rowBytes_buf) != BmpReaderError::Ok) {
            Serial.printf("[%lu] [GFX] Failed to read row %d from bitmap\n", millis(), bmpY);
            free(outputRow);
            free(rowBytes_buf);
            return;
        }
        
        
        int srcY = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
        
        
        int destY = y + static_cast<int>(srcY * scaleY);
        
        
        if (destY >= getScreenHeight() || destY < 0) continue;
        
        
        for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
            
            uint8_t val = (outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x03;
            
            
            int destX = x + static_cast<int>(bmpX * scaleX);
            
            
            if (destX < 0 || destX >= getScreenWidth()) continue;
            
            if (isGrayscaleMode) {
                
                
                
                if (val > 0) { 
                    drawPixel(destX, destY, val);
                }
            } else {
                
                bool pixelSet;
                
                if (isPure1Bit) {
                    
                    pixelSet = (val == 0);
                } else {
                    pixelSet = bwShouldInk2bpp(val, grayStyle);
                }

                if (pixelSet) {
                    if (isPure1Bit) {
                        drawPixel(destX, destY, true);
                    } else {
                        drawBwFrom2bppStage(destX, destY, val);
                    }
                }
                
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
    
    
    std::vector<uint8_t> sampleValues;
    sampleValues.reserve(100); 
    
    
    for (int bmpY = 0; bmpY < std::min(10, bitmap.getHeight()); bmpY++) {
        nonConstBitmap->readNextRow(outputRow, rowBytes_buf);
        for (int bmpX = 0; bmpX < std::min(10, bitmap.getWidth()); bmpX++) {
            uint8_t val = (outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x03;
            sampleValues.push_back(val);
        }
    }
    
    
    int threshold = 1; 
    bool hasWhite = false;
    bool hasBlack = false;
    
    for (uint8_t val : sampleValues) {
        if (val >= 2) hasWhite = true;
        if (val <= 1) hasBlack = true;
    }
    
    if (hasWhite && hasBlack) {
        
        threshold = 1;
    } else if (!hasWhite) {
        
        threshold = 3;
    } else {
        
        threshold = -1;
    }
    
    
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
    
    
    
    
    for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
        if (nonConstBitmap->readNextRow(outputRow, rowBytes_buf) != BmpReaderError::Ok) {
            free(outputRow);
            free(rowBytes_buf);
            return;
        }
        
        int srcY = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
        int destY = y + static_cast<int>(srcY * scaleY);
        
        if (destY >= getScreenHeight() || destY < 0) continue;
        
        
        for (int outX = 0; outX < static_cast<int>(bitmap.getWidth() * scaleX); outX++) {
            int srcX = static_cast<int>(outX / scaleX);
            if (srcX >= bitmap.getWidth()) continue;
            
            uint8_t val = (outputRow[srcX / 4] >> (6 - ((srcX * 2) % 8))) & 0x03;
            
            
            
            
            
            
            bool isBlack = (val < 2);  
            
            
            if (!isBlack && scaleX > 0.5f) {
                
                int neighborCount = 0;
                int whiteNeighbors = 0;
                
                
                for (int ny = -1; ny <= 1; ny++) {
                    for (int nx = -1; nx <= 1; nx++) {
                        if (ny == 0 && nx == 0) continue;
                        
                        int checkY = bmpY + ny;
                        int checkX = srcX + nx;
                        
                        if (checkY >= 0 && checkY < bitmap.getHeight() && 
                            checkX >= 0 && checkX < bitmap.getWidth()) {
                            neighborCount++;
                            
                            
                            
                        }
                    }
                }
            }
            
            int destX = x + outX;
            if (destX >= 0 && destX < getScreenWidth()) {
                if (isBlack) {
                    drawPixel(destX, destY, true);  
                }
                
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
      
      const bool ink = !readIconBitMsbFirst(bitmap, width, height, sx, sy);
      const bool black = ink ^ invert;
      drawPixel(x + dx, y + dy, black);
    }
  }
}

void GfxRenderer::drawTransparentImage(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight,
                                      uint8_t transparentColor, ImageOrientation imgOrientation) const {
    
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
    
    
    float scale = std::min(scaleX, scaleY);
    
    
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
    
    
    Bitmap* nonConstBitmap = const_cast<Bitmap*>(&bitmap);
    if (nonConstBitmap->rewindToData() != BmpReaderError::Ok) {
        Serial.printf("[%lu] [GFX] Failed to rewind bitmap\n", millis());
        free(outputRow);
        free(rowBytes_buf);
        return;
    }
    
    
    for (int bmpY = 0; bmpY < targetHeight; bmpY++) {
        if (nonConstBitmap->readNextRow(outputRow, rowBytes_buf) != BmpReaderError::Ok) {
            Serial.printf("[%lu] [GFX] Failed to read row %d\n", millis(), bmpY);
            break;
        }
        
        int srcY = bitmap.isTopDown() ? bmpY : targetHeight - 1 - bmpY;
        int destY = y + static_cast<int>(srcY * scale);
        
        if (destY < 0 || destY >= getScreenHeight()) continue;
        
        for (int bmpX = 0; bmpX < targetWidth; bmpX++) {
            
            uint8_t val = (outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x03;
            
            int destX = x + static_cast<int>(bmpX * scale);
            if (destX < 0 || destX >= getScreenWidth()) continue;
            
            
            
            bool shouldDraw;
            if (transparentColor == 1) {
                
                shouldDraw = (val < 3);
            } else {
                
                shouldDraw = (val == 3);
            }
            
            if (shouldDraw) {
                if (renderMode == BW) {
                    if (bwShouldInk2bpp(val, bitmapGrayRenderStyle)) {
                      drawBwFrom2bppStage(destX, destY, val);
                    }
                } else {
                    drawPixel(destX, destY, true);
                }
            }
        }
    }
    
    free(outputRow);
    free(rowBytes_buf);
}


void GfxRenderer::drawTransparentImage2Bit(const uint8_t bitmap[], int x, int y, int width, int height,
                                          uint8_t alphaThreshold, ImageOrientation imgOrientation) const {
  int targetX = x;
  int targetY = y;
  int targetW = width;
  int targetH = height;

  
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
  const int bytesPerRow = (width + 3) / 4; 
  
  for (int row = 0; row < height; row++) {
    int screenY = rotatedY + row;
    if (screenY < 0 || screenY >= HalDisplay::DISPLAY_HEIGHT) continue;
    
    const uint8_t* srcRow = &bitmap[row * bytesPerRow];
    uint8_t* destRow = &frameBuffer[screenY * stride];
    
    for (int pixelX = 0; pixelX < width; pixelX++) {
      int screenX = rotatedX + pixelX;
      if (screenX < 0 || screenX >= HalDisplay::DISPLAY_WIDTH) continue;
      
      
      const int byteIdx = pixelX / 4;
      const int bitShift = 6 - ((pixelX & 3) << 1);
      const uint8_t pixelValue = (srcRow[byteIdx] >> bitShift) & 0x03;
      
      
      
      if (pixelValue >= alphaThreshold) {
        const uint16_t byteIndex = screenY * stride + (screenX / 8);
        const uint8_t bitPosition = 7 - (screenX % 8);
        
        
        
        bool drawBlack = (pixelValue < 2); 
        
        if (drawBlack) {
          frameBuffer[byteIndex] &= ~(1 << bitPosition);  
        } else {
          frameBuffer[byteIndex] |= 1 << bitPosition;   
        }
      }
    }
  }
}

void GfxRenderer::insertStreamingFont(int fontId, std::unique_ptr<ExternalFont> streamingFont,
                                      const EpdFontFamily& font) {
  const EpdFontData* dataPtr = streamingFont->getData();
  EpdFontFamily streamingFamily = font;
  streamingFamily.setData(EpdFontFamily::REGULAR, dataPtr);
  streamingFonts.erase(dataPtr);
  streamingFonts.emplace(dataPtr, std::move(streamingFont));
  fontMap.erase(fontId);
  fontMap.emplace(fontId, streamingFamily);
}

bool GfxRenderer::getGlyphBitmap(const EpdFontFamily& fontFamily, uint32_t offset, uint32_t length,
                                 uint8_t* outputBuffer, EpdFontFamily::Style style) const {
  const EpdFontData* targetData = fontFamily.getData(style);
  auto it = streamingFonts.find(targetData);
  if (it != streamingFonts.end()) {
    return it->second->getGlyphBitmap(offset, length, outputBuffer);
  }
  return false;
}

void GfxRenderer::removeFont(int fontId) {
  auto it = fontMap.find(fontId);
  if (it == fontMap.end()) {
    return;
  }
  for (const auto style :
       {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC}) {
    const EpdFontData* d = it->second.getData(style);
    if (d != nullptr) {
      streamingFonts.erase(d);
    }
  }
  fontMap.erase(it);
}

void GfxRenderer::removeAllStreamingFonts() { streamingFonts.clear(); }

int GfxRenderer::getStreamingTextWidth(const EpdFontFamily& family, const char* text,
                                       EpdFontFamily::Style style) const {
  const EpdFontData* data = family.getData(style);
  auto it = streamingFonts.find(data);
  if (it == streamingFonts.end()) {
    return 0;
  }

  int totalWidth = 0;
  uint32_t cp;
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text);

  while ((cp = utf8NextCodepoint(&ptr))) {
    EpdGlyph glyph;
    if (!it->second->getGlyphMetadata(cp, glyph)) {
      if (!it->second->getGlyphMetadata(REPLACEMENT_GLYPH, glyph)) {
        continue;
      }
    }
    totalWidth += glyph.advanceX;
  }
  return totalWidth;
}

void GfxRenderer::addStreamingFontStyle(int fontId, EpdFontFamily::Style style,
                                        std::unique_ptr<ExternalFont> streamingFont) {
  auto it = fontMap.find(fontId);
  if (it == fontMap.end()) {
    Serial.printf("[GFX] Can't add style to unknown font ID %d\n", fontId);
    return;
  }

  const EpdFontData* dataPtr = streamingFont->getData();
  streamingFonts.erase(dataPtr);
  streamingFonts.emplace(dataPtr, std::move(streamingFont));

  EpdFontFamily updatedFamily = it->second;
  updatedFamily.setData(style, dataPtr);
  fontMap.erase(fontId);
  fontMap.emplace(fontId, updatedFamily);
}