#include "TextRender.h"

#include <Utf8.h>

#include "GfxRenderer.h"

int TextRender::getWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  return gfx.textGetWidth(fontId, text, style);
}

int TextRender::getHeight(const int fontId) const {
  return getLineHeight(fontId);
}

int TextRender::getFontAscenderSize(const int fontId) const {
  return gfx.textGetFontAscenderSize(fontId);
}

int TextRender::getLineHeight(const int fontId) const {
  return gfx.textGetLineHeight(fontId);
}

int TextRender::getSpaceWidth(const int fontId) const {
  return gfx.textGetSpaceWidth(fontId);
}

std::string TextRender::truncate(const int fontId, const char* text, const int maxWidth,
                                 const EpdFontFamily::Style style) const {
  return gfx.textTruncate(fontId, text, maxWidth, style);
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
  gfx.textDraw(fontId, x, y, text, black, style);
}

void TextRender::centered(const int fontId, const int y, const char* text, const bool black,
                              const EpdFontFamily::Style style) const {
  gfx.textDrawCentered(fontId, y, text, black, style);
}
