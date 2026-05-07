/**
 * @file GfxRendererBitmap.cpp
 */
#include "BitmapRender.h"
#include "GfxRenderer.h"

#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
/** Corner radius for rounded fillRect/drawRect: subtle, not pill-shaped (was min/4). */
int RoundedRectCornerRadius(const int width, const int height) {
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

int CornerSpanFromRy(const int r, const int ry) {
  const int inner = r * r - ry * ry;
  if (inner <= 0) {
    return 0;
  }
  return static_cast<int>(std::sqrt(static_cast<double>(inner)));
}

/** Same interior as `fillRect(..., rounded=true)` — O(1) per pixel (must match fillRect scanlines). */
bool PixelInRoundedRectFillShape(const int px, const int py, const int x, const int y, const int w, const int h) {
  if (px < x || px >= x + w || py < y || py >= y + h) {
    return false;
  }
  if (w < 3 || h < 3) {
    return true;
  }
  const int radius = RoundedRectCornerRadius(w, h);
  if (py >= y + radius && py < y + h - radius) {
    return true;
  }
  const int fromTop = py - y;
  if (fromTop < radius) {
    const int span = CornerSpanFromRy(radius, radius - fromTop);
    return px >= x + radius - span && px < x + w - radius + span;
  }
  const int fromBottom = (y + h - 1) - py;
  if (fromBottom < radius) {
    const int span = CornerSpanFromRy(radius, radius - fromBottom);
    return px >= x + radius - span && px < x + w - radius + span;
  }
  return true;
}

/** Pixels outside the rounded rect (same geometry as `fillRect` with `rounded`). */
void MaskBitmapCornersOutsideRounded(const GfxRenderer& gfx, const int x, const int y, const int drawnW,
                                     const int drawnH, const BitmapRender::RoundedOutside style) {
  if (style == BitmapRender::RoundedOutside::None) {
    return;
  }
  if (drawnW < 3 || drawnH < 3) {
    return;
  }
  const int r = RoundedRectCornerRadius(drawnW, drawnH);
  if (r < 1) {
    return;
  }
  const int sw = gfx.getScreenWidth();
  const int sh = gfx.getScreenHeight();

  auto applyCorner = [&](const int px, const int py) {
    if (px < 0 || px >= sw || py < 0 || py >= sh) {
      return;
    }
    if (style == BitmapRender::RoundedOutside::PaperOutside) {
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
    const int span = CornerSpanFromRy(r, r - cy);
    for (int px = x; px < x + r - span; ++px) {
      applyCorner(px, py);
    }
  }

  for (int cy = 0; cy < r; ++cy) {
    const int py = y + cy;
    const int span = CornerSpanFromRy(r, r - cy);
    for (int px = x + drawnW - r + span; px < x + drawnW; ++px) {
      applyCorner(px, py);
    }
  }

  for (int cy = 0; cy < r; ++cy) {
    const int py = y + drawnH - 1 - cy;
    const int span = CornerSpanFromRy(r, r - cy);
    for (int px = x; px < x + r - span; ++px) {
      applyCorner(px, py);
    }
  }

  for (int cy = 0; cy < r; ++cy) {
    const int py = y + drawnH - 1 - cy;
    const int span = CornerSpanFromRy(r, r - cy);
    for (int px = x + drawnW - r + span; px < x + drawnW; ++px) {
      applyCorner(px, py);
    }
  }
}

inline bool BwShouldInk2bpp(const uint8_t stage03, const GfxRenderer::BitmapGrayRenderStyle gs) {
  const uint8_t st = stage03 & 3u;
  if (gs == GfxRenderer::BitmapGrayRenderStyle::VeryDark) {
    return st < 3u;
  }
  if (gs == GfxRenderer::BitmapGrayRenderStyle::Balanced) {
    return st <= 1u;  // skip light-gray stage in low mode
  }
  return st < 3u;
}

void DrawBwFrom2bppStage(const GfxRenderer& gfx, const int px, const int py, const uint8_t stage03) {
  const uint8_t v = static_cast<uint8_t>(stage03 & 3u);
  if (v == 3u) return;
  if (v == 0u) {
    gfx.drawPixel(px, py, true);
    return;
  }
  static const uint8_t kBayer2[4] = {0, 2, 3, 1};
  const uint8_t t = kBayer2[((py & 1) << 1) | (px & 1)];
  const uint8_t tScaled = (t * 16) / 4;
  const auto style = gfx.getBitmapGrayRenderStyle();
  const bool veryDark = (style == GfxRenderer::BitmapGrayRenderStyle::VeryDark);
  const bool dark = (style == GfxRenderer::BitmapGrayRenderStyle::Dark) || veryDark;
  if (v == 1u) {
    gfx.drawPixel(px, py, tScaled < (veryDark ? 14u : (dark ? 13u : 12u)));
    return;
  }
  gfx.drawPixel(px, py, tScaled < (veryDark ? 14u : (dark ? 13u : 6u)));
}

/** 1-bpp packed row-major, MSB = left; dimensions are the source bitmap's (width x height). */
bool ReadIconBitMsbFirst(const uint8_t* bitmap, const int width, const int height, const int sx, const int sy) {
  if (sx < 0 || sy < 0 || sx >= width || sy >= height) {
    return false;
  }
  const int stride = (width + 7) / 8;
  const uint8_t byte = bitmap[sy * stride + sx / 8];
  return (byte & (0x80 >> (sx % 8))) != 0;
}
}

void BitmapRender::render(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight,
                             const float cropX, const float cropY,
                             const RoundedOutside roundedOutside) const {
  if (bitmap.is1Bit() && cropX == 0.0f && cropY == 0.0f) {
    oneBit(bitmap, x, y, maxWidth, maxHeight, roundedOutside);
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

  const int drawnW =
      contentW > 0 ? static_cast<int>(std::floor(static_cast<float>(contentW) * scale)) : 0;
  const int drawnH =
      contentH > 0 ? static_cast<int>(std::floor(static_cast<float>(contentH) * scale)) : 0;
  const int maskW = maxWidth > 0 ? maxWidth : drawnW;
  const int maskH = maxHeight > 0 ? maxHeight : drawnH;

  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));
  if (!outputRow || !rowBytes) {
    free(outputRow);
    free(rowBytes);
    return;
  }

  const GfxRenderer::BitmapGrayRenderStyle grayStyle = gfx.bitmapGrayRenderStyle;

  auto pixel2bpp = [](const uint8_t* row, const int px) -> uint8_t {
    return static_cast<uint8_t>((row[px / 4] >> (6 - ((px * 2) % 8))) & 0x3);
  };

  auto emitPixel = [&](const int screenX, const int screenY, const uint8_t val) {
    if (roundedOutside == BitmapRender::RoundedOutside::PaperOutside && maskW > 0 && maskH > 0) {
      if (!PixelInRoundedRectFillShape(screenX, screenY, x, y, maskW, maskH)) {
        return;
      }
    }
    if (gfx.renderMode == GfxRenderer::BW) {
      if (BwShouldInk2bpp(val, grayStyle)) {
        DrawBwFrom2bppStage(gfx, screenX, screenY, val);
      }
    } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_MSB && (val == 1 || val == 2)) {
      gfx.drawPixel(screenX, screenY, false);
    } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_LSB && val == 1) {
      gfx.drawPixel(screenX, screenY, false);
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
      if (y0 >= gfx.getScreenHeight()) {
        break;
      }
      for (int sy = y0; sy < y1 && sy < gfx.getScreenHeight(); ++sy) {
        if (sy < 0) {
          continue;
        }
        for (int bmpX = cropPixX; bmpX < bitmap.getWidth() - cropPixX; bmpX++) {
          const int srcCol = bmpX - cropPixX;
          const int x0 = x + static_cast<int>(std::floor(static_cast<float>(srcCol) * scale));
          const int x1 = x + static_cast<int>(std::floor(static_cast<float>(srcCol + 1) * scale));
          const uint8_t val = pixel2bpp(outputRow, bmpX);
          for (int sx = x0; sx < x1 && sx < gfx.getScreenWidth(); ++sx) {
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
      if (screenY >= gfx.getScreenHeight()) {
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
        if (screenX >= gfx.getScreenWidth()) {
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

  if (roundedOutside != BitmapRender::RoundedOutside::None && contentW > 0 && contentH > 0 && maskW > 0 &&
      maskH > 0) {
    MaskBitmapCornersOutsideRounded(gfx, x, y, maskW, maskH, roundedOutside);
  }

  free(outputRow);
  free(rowBytes);
}

void BitmapRender::oneBit(const Bitmap& bitmap, const int x, const int y, const int maxWidth,
                                 const int maxHeight, const RoundedOutside roundedOutside) const {
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

  const int drawnW = static_cast<int>(std::floor(static_cast<float>(bw) * scale));
  const int drawnH = static_cast<int>(std::floor(static_cast<float>(bitmap.getHeight()) * scale));
  const int maskW = maxWidth > 0 ? maxWidth : drawnW;
  const int maskH = maxHeight > 0 ? maxHeight : drawnH;

  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));
  if (!outputRow || !rowBytes) {
    free(outputRow);
    free(rowBytes);
    return;
  }

  const GfxRenderer::BitmapGrayRenderStyle grayStyle = gfx.bitmapGrayRenderStyle;

  auto pixel2bpp = [](const uint8_t* row, const int px) -> uint8_t {
    return static_cast<uint8_t>((row[px / 4] >> (6 - ((px * 2) % 8))) & 0x3);
  };

  auto emitPixel1 = [&](const int screenX, const int screenY, const uint8_t val) {
    if (roundedOutside == BitmapRender::RoundedOutside::PaperOutside && maskW > 0 && maskH > 0) {
      if (!PixelInRoundedRectFillShape(screenX, screenY, x, y, maskW, maskH)) {
        return;
      }
    }
    if (gfx.renderMode == GfxRenderer::BW) {
      if (BwShouldInk2bpp(val, grayStyle)) {
        DrawBwFrom2bppStage(gfx, screenX, screenY, val);
      }
    } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_MSB && (val == 1 || val == 2)) {
      gfx.drawPixel(screenX, screenY, false);
    } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_LSB && val == 1) {
      gfx.drawPixel(screenX, screenY, false);
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
      if (y0 >= gfx.getScreenHeight()) {
        continue;
      }
      for (int sy = y0; sy < y1 && sy < gfx.getScreenHeight(); ++sy) {
        if (sy < 0) {
          continue;
        }
        for (int bmpX = 0; bmpX < bw; bmpX++) {
          const int x0 = x + static_cast<int>(std::floor(static_cast<float>(bmpX) * scale));
          const int x1 = x + static_cast<int>(std::floor(static_cast<float>(bmpX + 1) * scale));
          const uint8_t val = pixel2bpp(outputRow, bmpX);
          for (int sx = x0; sx < x1 && sx < gfx.getScreenWidth(); ++sx) {
            if (sx < 0) {
              continue;
            }
            emitPixel1(sx, sy, val);
          }
        }
      }
    } else {
      const int screenY = y + (isScaled ? static_cast<int>(std::floor(static_cast<float>(vr) * scale)) : vr);
      if (screenY >= gfx.getScreenHeight()) {
        continue;
      }
      if (screenY < 0) {
        continue;
      }

      for (int bmpX = 0; bmpX < bw; bmpX++) {
        const int screenX = x + (isScaled ? static_cast<int>(std::floor(static_cast<float>(bmpX) * scale)) : bmpX);
        if (screenX >= gfx.getScreenWidth()) {
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

  if (roundedOutside != BitmapRender::RoundedOutside::None && maskW > 0 && maskH > 0) {
    MaskBitmapCornersOutsideRounded(gfx, x, y, maskW, maskH, roundedOutside);
  }

  free(outputRow);
  free(rowBytes);
}

void BitmapRender::smallClean(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight) const {
    
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
    auto* rowBytesBuffer = static_cast<uint8_t*>(malloc(rowBytes));
    
    if (!outputRow || !rowBytesBuffer) {
        Serial.printf("[%lu] [GFX] !! Failed to allocate bitmap row buffers\n", millis());
        free(outputRow);
        free(rowBytesBuffer);
        return;
    }
    
    
    Bitmap* nonConstBitmap = const_cast<Bitmap*>(&bitmap);
    if (nonConstBitmap->rewindToData() != BmpReaderError::Ok) {
        Serial.printf("[%lu] [GFX] Failed to rewind bitmap\n", millis());
        free(outputRow);
        free(rowBytesBuffer);
        return;
    }
    
    
    bool isPure1Bit = (bitmap.getBpp() == 1);
    
    
    bool isGrayscaleMode =
        (gfx.renderMode == GfxRenderer::GRAYSCALE_LSB || gfx.renderMode == GfxRenderer::GRAYSCALE_MSB);
    const GfxRenderer::BitmapGrayRenderStyle grayStyle = gfx.bitmapGrayRenderStyle;

    for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
        
        if (nonConstBitmap->readNextRow(outputRow, rowBytesBuffer) != BmpReaderError::Ok) {
            Serial.printf("[%lu] [GFX] Failed to read row %d from bitmap\n", millis(), bmpY);
            free(outputRow);
            free(rowBytesBuffer);
            return;
        }
        
        
        int srcY = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
        
        
        int destY = y + static_cast<int>(srcY * scaleY);
        
        
        if (destY >= gfx.getScreenHeight() || destY < 0) continue;
        
        
        for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
            
            uint8_t val = (outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x03;
            
            
            int destX = x + static_cast<int>(bmpX * scaleX);
            
            
            if (destX < 0 || destX >= gfx.getScreenWidth()) continue;
            
            if (isGrayscaleMode) {
                
                
                
                if (val > 0) { 
                    gfx.drawPixel(destX, destY, val);
                }
            } else {
                
                bool pixelSet;
                
                if (isPure1Bit) {
                    
                    pixelSet = (val == 0);
                } else {
                    pixelSet = BwShouldInk2bpp(val, grayStyle);
                }

                if (pixelSet) {
                    if (isPure1Bit) {
                        gfx.drawPixel(destX, destY, true);
                    } else {
                        DrawBwFrom2bppStage(gfx, destX, destY, val);
                    }
                }
                
            }
        }
    }
    
    free(outputRow);
    free(rowBytesBuffer);
}

void BitmapRender::smallAdaptive(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight) const {
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
    auto* rowBytesBuffer = static_cast<uint8_t*>(malloc(rowBytes));
    
    if (!outputRow || !rowBytesBuffer) {
        free(outputRow);
        free(rowBytesBuffer);
        return;
    }
    
    Bitmap* nonConstBitmap = const_cast<Bitmap*>(&bitmap);
    nonConstBitmap->rewindToData();
    
    
    std::vector<uint8_t> sampleValues;
    sampleValues.reserve(100); 
    
    
    for (int bmpY = 0; bmpY < std::min(10, bitmap.getHeight()); bmpY++) {
        nonConstBitmap->readNextRow(outputRow, rowBytesBuffer);
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
        if (nonConstBitmap->readNextRow(outputRow, rowBytesBuffer) != BmpReaderError::Ok) {
            free(outputRow);
            free(rowBytesBuffer);
            return;
        }
        
        int srcY = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
        int destY = y + static_cast<int>(srcY * scaleY);
        
        if (destY >= gfx.getScreenHeight() || destY < 0) continue;
        
        for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
            uint8_t val = (outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x03;
            
            
            bool pixelSet = (static_cast<int>(val) <= threshold);
            
            if (pixelSet) {
                int destX = x + static_cast<int>(bmpX * scaleX);
                if (destX >= 0 && destX < gfx.getScreenWidth()) {
                    gfx.drawPixel(destX, destY, true);
                }
            }
        }
    }
    
    free(outputRow);
    free(rowBytesBuffer);
}

void BitmapRender::small(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight) const {
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
    auto* rowBytesBuffer = static_cast<uint8_t*>(malloc(rowBytes));
    
    if (!outputRow || !rowBytesBuffer) {
        free(outputRow);
        free(rowBytesBuffer);
        return;
    }
    
    Bitmap* nonConstBitmap = const_cast<Bitmap*>(&bitmap);
    nonConstBitmap->rewindToData();
    
    
    
    
    for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
        if (nonConstBitmap->readNextRow(outputRow, rowBytesBuffer) != BmpReaderError::Ok) {
            free(outputRow);
            free(rowBytesBuffer);
            return;
        }
        
        int srcY = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
        int destY = y + static_cast<int>(srcY * scaleY);
        
        if (destY >= gfx.getScreenHeight() || destY < 0) continue;
        
        
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
            if (destX >= 0 && destX < gfx.getScreenWidth()) {
                if (isBlack) {
                    gfx.drawPixel(destX, destY, true);  
                }
                
            }
        }
    }
    
    free(outputRow);
    free(rowBytesBuffer);
}

void BitmapRender::icon(const uint8_t bitmap[], int x, int y, int width, int height,
                           Orientation orientation, bool invert) const {
  int outW = width;
  int outH = height;
  switch (orientation) {
    case BitmapRender::Orientation::None:
    case BitmapRender::Orientation::Rotate180:
      outW = width;
      outH = height;
      break;
    case BitmapRender::Orientation::Rotate90CW:
    case BitmapRender::Orientation::Rotate270CW:
      outW = height;
      outH = width;
      break;
  }

  for (int dy = 0; dy < outH; ++dy) {
    for (int dx = 0; dx < outW; ++dx) {
      int sx = 0;
      int sy = 0;
      switch (orientation) {
        case BitmapRender::Orientation::None:
          sx = dx;
          sy = dy;
          break;
        case BitmapRender::Orientation::Rotate180:
          sx = width - 1 - dx;
          sy = height - 1 - dy;
          break;
        case BitmapRender::Orientation::Rotate90CW:
          sx = height - 1 - dx;
          sy = dy;
          break;
        case BitmapRender::Orientation::Rotate270CW:
          sx = dx;
          sy = width - 1 - dy;
          break;
      }
      
      const bool ink = !ReadIconBitMsbFirst(bitmap, width, height, sx, sy);
      const bool black = ink ^ invert;
      gfx.drawPixel(x + dx, y + dy, black);
    }
  }
}

void BitmapRender::transparent(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight,
                                        uint8_t transparentColor,
                                        Orientation orientation) const {
    
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
    auto* rowBytesBuffer = static_cast<uint8_t*>(malloc(rowBytes));
    
    if (!outputRow || !rowBytesBuffer) {
        Serial.printf("[%lu] [GFX] !! Failed to allocate bitmap row buffers\n", millis());
        free(outputRow);
        free(rowBytesBuffer);
        return;
    }
    
    
    Bitmap* nonConstBitmap = const_cast<Bitmap*>(&bitmap);
    if (nonConstBitmap->rewindToData() != BmpReaderError::Ok) {
        Serial.printf("[%lu] [GFX] Failed to rewind bitmap\n", millis());
        free(outputRow);
        free(rowBytesBuffer);
        return;
    }
    
    
    for (int bmpY = 0; bmpY < targetHeight; bmpY++) {
        if (nonConstBitmap->readNextRow(outputRow, rowBytesBuffer) != BmpReaderError::Ok) {
            Serial.printf("[%lu] [GFX] Failed to read row %d\n", millis(), bmpY);
            break;
        }
        
        int srcY = bitmap.isTopDown() ? bmpY : targetHeight - 1 - bmpY;
        int destY = y + static_cast<int>(srcY * scale);
        
        if (destY < 0 || destY >= gfx.getScreenHeight()) continue;
        
        for (int bmpX = 0; bmpX < targetWidth; bmpX++) {
            
            uint8_t val = (outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x03;
            
            int destX = x + static_cast<int>(bmpX * scale);
            if (destX < 0 || destX >= gfx.getScreenWidth()) continue;
            
            
            
            bool shouldDraw;
            if (transparentColor == 1) {
                
                shouldDraw = (val < 3);
            } else {
                
                shouldDraw = (val == 3);
            }
            
            if (shouldDraw) {
                if (gfx.renderMode == GfxRenderer::BW) {
                    if (BwShouldInk2bpp(val, gfx.bitmapGrayRenderStyle)) {
                      DrawBwFrom2bppStage(gfx, destX, destY, val);
                    }
                } else {
                    gfx.drawPixel(destX, destY, true);
                }
            }
        }
    }
    
    free(outputRow);
    free(rowBytesBuffer);
}

void BitmapRender::blendTransparent2Bit(const uint8_t bitmap[], int x, int y, int width, int height,
                                              uint8_t alphaThreshold,
                                              Orientation orientation) const {
  int targetX = x;
  int targetY = y;
  int targetW = width;
  int targetH = height;

  
  if (orientation != BitmapRender::Orientation::None) {
    Serial.printf("[%lu] [GFX] 2-bit transparent image rotation not yet implemented\n", millis());
    return;
  }

  int rotatedX = 0, rotatedY = 0;
  gfx.rotateCoordinates(targetX, targetY, &rotatedX, &rotatedY);

  switch (gfx.orientation) {
    case GfxRenderer::Portrait:
      rotatedY -= targetH;
      break;
    case GfxRenderer::PortraitInverted:
      rotatedX -= targetW;
      break;
    case GfxRenderer::LandscapeClockwise:
      rotatedY -= targetH;
      rotatedX -= targetW;
      break;
    case GfxRenderer::LandscapeCounterClockwise:
      break;
  }

  uint8_t* frameBuffer = gfx.display.getFrameBuffer();
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

namespace SleepScreenBitmap {

static void renderBitmap1Bit(const GfxRenderer& gfx, const Bitmap& bitmap, const int x, const int y,
                           const int maxWidth, const int maxHeight) {
  float scale = 1.0f;
  bool isScaled = false;
  if (maxWidth > 0 && bitmap.getWidth() > maxWidth) {
    scale = static_cast<float>(maxWidth) / static_cast<float>(bitmap.getWidth());
    isScaled = true;
  }
  if (maxHeight > 0 && bitmap.getHeight() > maxHeight) {
    scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>(bitmap.getHeight()));
    isScaled = true;
  }

  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(static_cast<size_t>(outputRowSize)));
  auto* rowBytes = static_cast<uint8_t*>(malloc(static_cast<size_t>(bitmap.getRowBytes())));
  if (!outputRow || !rowBytes) {
    free(outputRow);
    free(rowBytes);
    return;
  }

  for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      free(outputRow);
      free(rowBytes);
      return;
    }

    const int bmpYOffset = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
    int screenY = y + (isScaled ? static_cast<int>(std::floor(static_cast<float>(bmpYOffset) * scale)) : bmpYOffset);
    if (screenY >= gfx.getScreenHeight()) {
      continue;
    }
    if (screenY < 0) {
      continue;
    }

    for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
      int screenX = x + (isScaled ? static_cast<int>(std::floor(static_cast<float>(bmpX) * scale)) : bmpX);
      if (screenX >= gfx.getScreenWidth()) {
        break;
      }
      if (screenX < 0) {
        continue;
      }

      const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      if (val < 3) {
        gfx.drawPixel(screenX, screenY, true);
      }
    }
  }

  free(outputRow);
  free(rowBytes);
}

}  

void BitmapRender::sleepScreen(const Bitmap& bitmap, const int x, const int y, const int maxWidth,
                                  const int maxHeight, const float cropX, const float cropY,
                                  const bool coverFill) const {
  if (bitmap.is1Bit() && cropX == 0.0f && cropY == 0.0f) {
    SleepScreenBitmap::renderBitmap1Bit(gfx, bitmap, x, y, maxWidth, maxHeight);
    return;
  }

  float scale = 1.0f;
  bool isScaled = false;
  const int cropPixX = static_cast<int>(std::floor(static_cast<float>(bitmap.getWidth()) * cropX / 2.0f));
  const int cropPixY = static_cast<int>(std::floor(static_cast<float>(bitmap.getHeight()) * cropY / 2.0f));

  const float croppedWidth = (1.0f - cropX) * static_cast<float>(bitmap.getWidth());
  const float croppedHeight = (1.0f - cropY) * static_cast<float>(bitmap.getHeight());

  const float widthScale =
      (maxWidth > 0 && croppedWidth > 0.0f) ? static_cast<float>(maxWidth) / croppedWidth : 0.0f;
  const float heightScale =
      (maxHeight > 0 && croppedHeight > 0.0f) ? static_cast<float>(maxHeight) / croppedHeight : 0.0f;

  const bool hasW = maxWidth > 0 && croppedWidth > 0.0f;
  const bool hasH = maxHeight > 0 && croppedHeight > 0.0f;
  float fitScale = 1.0f;
  if (hasW && hasH) {
    fitScale = coverFill ? std::max(widthScale, heightScale) : std::min(widthScale, heightScale);
  } else if (hasW) {
    fitScale = widthScale;
  } else if (hasH) {
    fitScale = heightScale;
  }

  constexpr float kEps = 1e-5f;
  const bool hasTargetBounds = hasW || hasH;
  if (hasTargetBounds) {
    if (coverFill) {
      if (std::fabs(fitScale - 1.0f) > kEps) {
        scale = fitScale;
        isScaled = true;
      }
    } else if (fitScale < 1.0f - kEps) {
      scale = fitScale;
      isScaled = true;
    }
  }

  const bool upscale = isScaled && scale > 1.0f + kEps;

  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(static_cast<size_t>(outputRowSize)));
  auto* rowBytes = static_cast<uint8_t*>(malloc(static_cast<size_t>(bitmap.getRowBytes())));
  if (!outputRow || !rowBytes) {
    free(outputRow);
    free(rowBytes);
    return;
  }

  const int screenW = gfx.getScreenWidth();
  const int screenH = gfx.getScreenHeight();

  auto plotSleepPixel = [this](const int sx, const int sy, const uint8_t val) {
    if (gfx.renderMode == GfxRenderer::BW && val < 3) {
      gfx.drawPixel(sx, sy);
    } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_MSB && (val == 1 || val == 2)) {
      gfx.drawPixel(sx, sy, false);
    } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_LSB && val == 1) {
      gfx.drawPixel(sx, sy, false);
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

    const float ly =
        static_cast<float>(-cropPixY + (bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY));
    float lyEnd = ly;
    if ((bmpY + 1) < (bitmap.getHeight() - cropPixY)) {
      lyEnd = static_cast<float>(-cropPixY +
                                 (bitmap.isTopDown() ? (bmpY + 1) : bitmap.getHeight() - 1 - (bmpY + 1)));
    } else {
      lyEnd = ly + (bitmap.isTopDown() ? 1.f : -1.f);
    }

    int syLo = static_cast<int>(std::floor(ly * scale));
    int syHi = static_cast<int>(std::floor(lyEnd * scale));
    if (syLo > syHi) {
      std::swap(syLo, syHi);
    }
    syLo += y;
    syHi += y;
    const int yEnd = std::min(screenH, std::max(syLo + 1, syHi));

    if (syLo >= screenH) {
      break;
    }
    if (yEnd <= 0) {
      continue;
    }

    for (int bmpX = cropPixX; bmpX < bitmap.getWidth() - cropPixX; bmpX++) {
      const int relX = bmpX - cropPixX;
      const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      if (!isScaled) {
        const int sx = relX + x;
        if (sx >= screenW) {
          break;
        }
        if (sx < 0) {
          continue;
        }
        for (int sy = std::max(0, syLo); sy < yEnd; sy++) {
          plotSleepPixel(sx, sy, val);
        }
      } else if (!upscale) {
        const int screenX =
            static_cast<int>(std::floor(static_cast<float>(relX) * scale)) + x;
        if (screenX >= screenW) {
          break;
        }
        if (screenX < 0) {
          continue;
        }
        for (int sy = std::max(0, syLo); sy < yEnd; sy++) {
          plotSleepPixel(screenX, sy, val);
        }
      } else {
        const int sx0 = static_cast<int>(std::floor(static_cast<float>(relX) * scale)) + x;
        const int sx1 = static_cast<int>(std::floor(static_cast<float>(relX + 1) * scale)) + x;
        const int xa = std::max(0, std::min(sx0, sx1));
        const int xb = std::min(screenW, std::max(sx0, sx1));
        for (int sx = xa; sx < xb; sx++) {
          for (int sy = std::max(0, syLo); sy < yEnd; sy++) {
            plotSleepPixel(sx, sy, val);
          }
        }
      }
    }
  }

  free(outputRow);
  free(rowBytes);
}
