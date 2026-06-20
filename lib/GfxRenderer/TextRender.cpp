#include "TextRender.h"

#include <Utf8.h>

#include <algorithm>
#include <string>

#include "GfxRenderer.h"

namespace {

constexpr uint8_t kSmallCapsScalePct = 77;
constexpr int kScaleRoundBias = 50;

bool isAsciiLower(const uint32_t cp) { return cp >= 'a' && cp <= 'z'; }

uint32_t toAsciiUpper(const uint32_t cp) { return isAsciiLower(cp) ? (cp - ('a' - 'A')) : cp; }

void appendUtf8Codepoint(std::string& out, const uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string toUpperUtf8(const char* text) {
  std::string upper;
  if (!text) {
    return upper;
  }
  upper.reserve(std::strlen(text));
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text);
  while (const uint32_t cp = utf8NextCodepoint(&ptr)) {
    appendUtf8Codepoint(upper, toAsciiUpper(cp));
  }
  return upper;
}

int scaleMetricRound(const int value, const uint8_t scalePct) {
  return std::max(1, (value * static_cast<int>(scalePct) + kScaleRoundBias) / 100);
}

}  // namespace

int TextRender::getWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if (gfx.fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }
  const auto& family = gfx.fontMap.at(fontId);
  if (gfx.streamingFonts.count(family.getData(style))) {
    return getStreamingTextWidth(family, text, style);
  }
  int w = 0;
  int h = 0;
  family.getTextDimensions(text, &w, &h, style);
  return w;
}

int TextRender::getHeight(const int fontId) const {
  return getLineHeight(fontId);
}

int TextRender::getFontAscenderSize(const int fontId) const {
  if (gfx.fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }
  return gfx.fontMap.at(fontId).getData(EpdFontFamily::REGULAR)->ascender;
}

int TextRender::getLineHeight(const int fontId) const {
  if (gfx.fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }
  return gfx.fontMap.at(fontId).getData(EpdFontFamily::REGULAR)->advanceY;
}

int TextRender::getSpaceWidth(const int fontId) const {
  if (gfx.fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  const EpdFontFamily& font = gfx.fontMap.at(fontId);
  const EpdFontData* fontData = font.getData(EpdFontFamily::REGULAR);
  if (!fontData) {
    return 0;
  }

  const auto streamIt = gfx.streamingFonts.find(fontData);
  if (streamIt != gfx.streamingFonts.end()) {
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


int TextRender::getSmallCapsWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if (!text || *text == '\0' || gfx.fontMap.count(fontId) == 0) {
    return 0;
  }

  const auto& family = gfx.fontMap.at(fontId);
  const EpdFontData* fontData = family.getData(style);
  if (!fontData) {
    return 0;
  }

  const auto streamIt = gfx.streamingFonts.find(fontData);
  const std::string upper = toUpperUtf8(text);
  const char* ptr = upper.c_str();
  int totalWidth = 0;
  while (const uint32_t cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&ptr))) {
    EpdGlyph glyphStorage;
    const EpdGlyph* glyph = nullptr;
    if (streamIt != gfx.streamingFonts.end()) {
      if (!streamIt->second->getGlyphMetadata(cp, glyphStorage)) {
        streamIt->second->getGlyphMetadata(REPLACEMENT_GLYPH, glyphStorage);
      }
      glyph = &glyphStorage;
    } else {
      glyph = family.getGlyph(cp, style);
      if (!glyph) {
        glyph = family.getGlyph(REPLACEMENT_GLYPH, style);
      }
    }
    if (!glyph) {
      continue;
    }
    totalWidth += scaleMetricRound(glyph->advanceX, kSmallCapsScalePct);
  }
  return totalWidth;
}


std::string TextRender::truncate(const int fontId, const char* text, const int maxWidth,
                                 const EpdFontFamily::Style style) const {
  if (!text || maxWidth <= 0) return "";

  std::string item = text;
  const char* ellipsis = "...";
  int textWidth = getWidth(fontId, item.c_str(), style);
  if (textWidth <= maxWidth) {
    return item;
  }

  while (!item.empty() && getWidth(fontId, (item + ellipsis).c_str(), style) >= maxWidth) {
    utf8RemoveLastChar(item);
  }

  return item.empty() ? ellipsis : item + ellipsis;
}

void TextRender::rotated90CW(const int fontId, const int x, const int y, const char* text, const bool black,
                                 const EpdFontFamily::Style style) const {
  if (text == nullptr || *text == '\0' || gfx.fontMap.count(fontId) == 0) {
    return;
  }
  const auto font = gfx.fontMap.at(fontId);
  if (!font.hasPrintableChars(text, style)) {
    return;
  }

  const EpdFontData* fontData = font.getData(style);
  if (!fontData) {
    return;
  }
  auto it = gfx.streamingFonts.find(fontData);
  int yPos = y;

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    EpdGlyph glyphStorage;
    const EpdGlyph* glyph = nullptr;

    if (it != gfx.streamingFonts.end()) {
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
    } else if (it != gfx.streamingFonts.end()) {
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
            const uint8_t bitIndex = (3 - (pixelPosition % 4)) * 2;
            const uint8_t bmpVal = 3 - ((byte >> bitIndex) & 0x3);

            if (gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
              gfx.drawPixel(screenX, screenY, black);
            } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
              gfx.drawPixel(screenX, screenY, false);
            } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_LSB && bmpVal == 1) {
              gfx.drawPixel(screenX, screenY, false);
            }
          } else {
            const uint8_t byte = bitmap[pixelPosition / 8];
            const uint8_t bitIndex = 7 - (pixelPosition % 8);
            if ((byte >> bitIndex) & 1) {
              gfx.drawPixel(screenX, screenY, black);
            }
          }
        }
      }
    }
    yPos -= glyph->advanceX;
  }
}

void TextRender::render(const int fontId, const int x, const int y, const char* text, const bool black,
                      const EpdFontFamily::Style style) const {
  const int yPos = y + getFontAscenderSize(fontId);
  int xpos = x;

  if (text == nullptr || *text == '\0') {
    return;
  }

  if (gfx.fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return;
  }
  const auto font = gfx.fontMap.at(fontId);
  if (!font.hasPrintableChars(text, style)) {
    return;
  }

  uint32_t cp;
  int yCursor = yPos;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    renderChar(font, cp, &xpos, &yCursor, black, style);
  }
}

int TextRender::renderSmallCaps(const int fontId, const int x, const int y, const char* text, const bool black,
                                const EpdFontFamily::Style style) const {
  if (text == nullptr || *text == '\0') {
    return x;
  }
  if (gfx.fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return x;
  }

  const auto font = gfx.fontMap.at(fontId);
  const std::string upper = toUpperUtf8(text);
  const char* ptr = upper.c_str();
  // Sit the small caps on the same baseline as the surrounding full-size text.
  const int yPos = y + getFontAscenderSize(fontId);
  int xpos = x;
  int yCursor = yPos;

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&ptr)))) {
    renderScaledChar(font, cp, &xpos, &yCursor, black, style, kSmallCapsScalePct);
  }
  return xpos;
}

void TextRender::centered(const int fontId, const int y, const char* text, const bool black,
                              const EpdFontFamily::Style style) const {
  const int x = (gfx.getScreenWidth() - getWidth(fontId, text, style)) / 2;
  render(fontId, x, y, text, black, style);
}

void TextRender::renderChar(const EpdFontFamily& fontFamily, const uint32_t cp, int* x, const int* y,
                            const bool pixelState, const EpdFontFamily::Style style) const {
  EpdGlyph glyphStorage;
  const EpdGlyph* glyph = nullptr;
  const EpdFontData* fontData = fontFamily.getData(style);
  if (!fontData) {
    return;
  }
  auto it = gfx.streamingFonts.find(fontData);
  if (it != gfx.streamingFonts.end()) {
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
  } else if (it != gfx.streamingFonts.end()) {
    const size_t dataLen = glyph->dataLength;
    if (dataLen <= sizeof(localStackBuffer)) {
      if (it->second->getGlyphBitmap(glyph->dataOffset, dataLen, localStackBuffer)) {
        bitmap = localStackBuffer;
      } else {
        *x += glyph->advanceX;
        return;
      }
    } else {
      constexpr size_t kMaxRowBytes = 512;
      const size_t rowBytes =
          is2Bit ? (static_cast<size_t>(width) + 3u) / 4u : (static_cast<size_t>(width) + 7u) / 8u;
      if (rowBytes == 0 || rowBytes > kMaxRowBytes) {
        *x += glyph->advanceX;
        return;
      }
      uint8_t rowBuf[kMaxRowBytes];
      for (int glyphY = 0; glyphY < height; glyphY++) {
        const uint32_t rowOff = glyph->dataOffset + static_cast<uint32_t>(glyphY) * static_cast<uint32_t>(rowBytes);
        if (!it->second->getGlyphBitmap(rowOff, rowBytes, rowBuf)) {
          *x += glyph->advanceX;
          return;
        }
        const int screenY = *y - glyph->top + glyphY;
        for (int glyphX = 0; glyphX < width; glyphX++) {
          const int screenX = *x + left + glyphX;
          if (is2Bit) {
            const uint8_t byte = rowBuf[glyphX / 4];
            const uint8_t bitIndex = (3 - (glyphX % 4)) * 2;
            const uint8_t bmpVal = 3 - ((byte >> bitIndex) & 0x3);

            if (gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
              gfx.drawPixel(screenX, screenY, pixelState);
            } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
              gfx.drawPixel(screenX, screenY, false);
            } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_LSB && bmpVal == 1) {
              gfx.drawPixel(screenX, screenY, false);
            }
          } else {
            const uint8_t byte = rowBuf[glyphX / 8];
            const uint8_t bitIndex = 7 - (glyphX % 8);
            if ((byte >> bitIndex) & 1) {
              gfx.drawPixel(screenX, screenY, pixelState);
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
          const uint8_t bitIndex = (3 - (pixelPosition % 4)) * 2;
          const uint8_t bmpVal = 3 - ((byte >> bitIndex) & 0x3);

          if (gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
            gfx.drawPixel(screenX, screenY, pixelState);
          } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
            gfx.drawPixel(screenX, screenY, false);
          } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_LSB && bmpVal == 1) {
            gfx.drawPixel(screenX, screenY, false);
          }
        } else {
          const uint8_t byte = bitmap[pixelPosition / 8];
          const uint8_t bitIndex = 7 - (pixelPosition % 8);
          if ((byte >> bitIndex) & 1) {
            gfx.drawPixel(screenX, screenY, pixelState);
          }
        }
      }
    }
  }

  *x += glyph->advanceX;
}

void TextRender::renderScaledChar(const EpdFontFamily& fontFamily, const uint32_t cp, int* x, const int* y,
                                  const bool pixelState, const EpdFontFamily::Style style,
                                  const uint8_t scalePct) const {
  EpdGlyph glyphStorage;
  const EpdGlyph* glyph = nullptr;
  const EpdFontData* fontData = fontFamily.getData(style);
  if (!fontData) {
    return;
  }
  auto it = gfx.streamingFonts.find(fontData);
  if (it != gfx.streamingFonts.end()) {
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
  const int scaledAdvanceX = scaleMetricRound(glyph->advanceX, scalePct);
  const int scaledLeft = (glyph->left * static_cast<int>(scalePct) + kScaleRoundBias) / 100;
  const int scaledTop = std::max(1, (glyph->top * static_cast<int>(scalePct) + kScaleRoundBias) / 100);
  const int scaledW = std::max(1, (static_cast<int>(width) * static_cast<int>(scalePct) + kScaleRoundBias) / 100);
  const int scaledH = std::max(1, (static_cast<int>(height) * static_cast<int>(scalePct) + kScaleRoundBias) / 100);

  const uint8_t* bitmap = nullptr;
  uint8_t localStackBuffer[2048];
  if (fontData->bitmap != nullptr) {
    bitmap = &fontData->bitmap[glyph->dataOffset];
  } else if (it != gfx.streamingFonts.end()) {
    const size_t dataLen = glyph->dataLength;
    if (dataLen <= sizeof(localStackBuffer)) {
      if (it->second->getGlyphBitmap(glyph->dataOffset, dataLen, localStackBuffer)) {
        bitmap = localStackBuffer;
      } else {
        *x += scaledAdvanceX;
        return;
      }
    } else {
      constexpr size_t kMaxRowBytes = 512;
      const size_t rowBytes =
          is2Bit ? (static_cast<size_t>(width) + 3u) / 4u : (static_cast<size_t>(width) + 7u) / 8u;
      if (rowBytes == 0 || rowBytes > kMaxRowBytes) {
        *x += scaledAdvanceX;
        return;
      }
      uint8_t rowBuf[kMaxRowBytes];
      for (int outY = 0; outY < scaledH; ++outY) {
        const int srcY = std::min<int>(height - 1, (outY * static_cast<int>(height)) / scaledH);
        const uint32_t rowOff = glyph->dataOffset + static_cast<uint32_t>(srcY) * static_cast<uint32_t>(rowBytes);
        if (!it->second->getGlyphBitmap(rowOff, rowBytes, rowBuf)) {
          *x += scaledAdvanceX;
          return;
        }
        const int screenY = *y - scaledTop + outY;
        for (int outX = 0; outX < scaledW; ++outX) {
          int sx0 = (outX * static_cast<int>(width)) / scaledW;
          int sx1 = ((outX + 1) * static_cast<int>(width)) / scaledW;
          if (sx1 <= sx0) sx1 = sx0 + 1;
          sx1 = std::min<int>(sx1, width);
          const int screenX = *x + scaledLeft + outX;
          // Keep the darkest source pixel in the horizontal footprint so vertical strokes survive.
          if (is2Bit) {
            uint8_t rawMax = 0;
            for (int sx = sx0; sx < sx1; ++sx) {
              const uint8_t raw = (rowBuf[sx / 4] >> ((3 - (sx % 4)) * 2)) & 0x3;
              if (raw > rawMax) rawMax = raw;
            }
            const uint8_t bmpVal = 3 - rawMax;
            if (gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
              gfx.drawPixel(screenX, screenY, pixelState);
            } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
              gfx.drawPixel(screenX, screenY, false);
            } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_LSB && bmpVal == 1) {
              gfx.drawPixel(screenX, screenY, false);
            }
          } else {
            bool ink = false;
            for (int sx = sx0; sx < sx1; ++sx) {
              if ((rowBuf[sx / 8] >> (7 - (sx % 8))) & 1) {
                ink = true;
                break;
              }
            }
            if (ink) {
              gfx.drawPixel(screenX, screenY, pixelState);
            }
          }
        }
      }
      *x += scaledAdvanceX;
      return;
    }
  }

  if (bitmap != nullptr) {
    for (int outY = 0; outY < scaledH; ++outY) {
      int sy0 = (outY * static_cast<int>(height)) / scaledH;
      int sy1 = ((outY + 1) * static_cast<int>(height)) / scaledH;
      if (sy1 <= sy0) sy1 = sy0 + 1;
      sy1 = std::min<int>(sy1, height);
      const int screenY = *y - scaledTop + outY;
      for (int outX = 0; outX < scaledW; ++outX) {
        int sx0 = (outX * static_cast<int>(width)) / scaledW;
        int sx1 = ((outX + 1) * static_cast<int>(width)) / scaledW;
        if (sx1 <= sx0) sx1 = sx0 + 1;
        sx1 = std::min<int>(sx1, width);
        const int screenX = *x + scaledLeft + outX;
        // Keep the darkest source pixel in the footprint so thin strokes survive downscaling.
        if (is2Bit) {
          uint8_t rawMax = 0;
          for (int sy = sy0; sy < sy1; ++sy) {
            for (int sx = sx0; sx < sx1; ++sx) {
              const int pp = sy * width + sx;
              const uint8_t raw = (bitmap[pp / 4] >> ((3 - (pp % 4)) * 2)) & 0x3;
              if (raw > rawMax) rawMax = raw;
            }
          }
          const uint8_t bmpVal = 3 - rawMax;
          if (gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
            gfx.drawPixel(screenX, screenY, pixelState);
          } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
            gfx.drawPixel(screenX, screenY, false);
          } else if (gfx.renderMode == GfxRenderer::GRAYSCALE_LSB && bmpVal == 1) {
            gfx.drawPixel(screenX, screenY, false);
          }
        } else {
          bool ink = false;
          for (int sy = sy0; sy < sy1 && !ink; ++sy) {
            for (int sx = sx0; sx < sx1; ++sx) {
              const int pp = sy * width + sx;
              if ((bitmap[pp / 8] >> (7 - (pp % 8))) & 1) {
                ink = true;
                break;
              }
            }
          }
          if (ink) {
            gfx.drawPixel(screenX, screenY, pixelState);
          }
        }
      }
    }
  }
  *x += scaledAdvanceX;
}

int TextRender::getStreamingTextWidth(const EpdFontFamily& family, const char* text,
                                      const EpdFontFamily::Style style) const {
  const EpdFontData* data = family.getData(style);
  auto it = gfx.streamingFonts.find(data);
  if (it == gfx.streamingFonts.end()) {
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
