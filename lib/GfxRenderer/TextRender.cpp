#include "TextRender.h"

#include <Utf8.h>

#include "GfxRenderer.h"

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

void TextRender::prewarm(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if (!text || *text == '\0' || gfx.fontMap.count(fontId) == 0) {
    return;
  }
  const EpdFontData* data = gfx.fontMap.at(fontId).getData(style);
  const auto it = gfx.streamingFonts.find(data);
  if (it != gfx.streamingFonts.end()) {
    it->second->prewarmText(text);
  }
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
