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

GfxRenderer::GfxRenderer(HalDisplay& halDisplay)
    : display(halDisplay),
      renderMode(BW),
      orientation(Portrait),
      rectangle(*this),
      line(*this),
      icon(*this),
      polygon(*this),
      jpeg(*this),
      bitmap(*this),
      text(*this),
      ui(*this) {}

GfxRenderer::~GfxRenderer() { freeBwBufferChunks(); }

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

int GfxRenderer::textGetWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
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

void GfxRenderer::textDrawCentered(const int fontId, const int y, const char* text, const bool black,
                                   const EpdFontFamily::Style style) const {
  const int x = (getScreenWidth() - textGetWidth(fontId, text, style)) / 2;
  textDraw(fontId, x, y, text, black, style);
}

void GfxRenderer::textDraw(const int fontId, const int x, const int y, const char* text, const bool black,
                           const EpdFontFamily::Style style) const {
  const int yPos = y + textGetFontAscenderSize(fontId);
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

std::string GfxRenderer::textTruncate(const int fontId, const char* text, const int maxWidth,
                                       const EpdFontFamily::Style style) const {
  if (!text || maxWidth <= 0) return "";

  std::string item = text;
  const char* ellipsis = "...";
  int textWidth = textGetWidth(fontId, item.c_str(), style);
  if (textWidth <= maxWidth) {
    
    return item;
  }

  while (!item.empty() && textGetWidth(fontId, (item + ellipsis).c_str(), style) >= maxWidth) {
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

int GfxRenderer::textGetSpaceWidth(const int fontId) const {
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

int GfxRenderer::textGetFontAscenderSize(const int fontId) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  return fontMap.at(fontId).getData(EpdFontFamily::REGULAR)->ascender;
}

int GfxRenderer::textGetLineHeight(const int fontId) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  return fontMap.at(fontId).getData(EpdFontFamily::REGULAR)->advanceY;
}


int GfxRenderer::getTextHeight(const int fontId) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }
  return fontMap.at(fontId).getData(EpdFontFamily::REGULAR)->ascender;
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

