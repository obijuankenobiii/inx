/**
 * @file GfxRenderer.cpp
 * @brief Definitions for GfxRenderer.
 */

#include "GfxRenderer.h"

#include <Utf8.h>
#include <memory>
#include <set>
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
      bitmap(*this),
      text(*this),
      ui(*this) {}

GfxRenderer::~GfxRenderer() { freeBwBufferChunks(); }

void GfxRenderer::begin() {
  panelWidth = display.getDisplayWidth();
  panelHeight = display.getDisplayHeight();
  panelWidthBytes = display.getDisplayWidthBytes();
  frameBufferSize = display.getBufferSize();
  bwBufferChunks.assign((frameBufferSize + BW_BUFFER_CHUNK_SIZE - 1) / BW_BUFFER_CHUNK_SIZE, nullptr);
}

void GfxRenderer::insertFont(const int fontId, EpdFontFamily font) {
  fontMap.erase(fontId);
  fontMap.emplace(fontId, std::move(font));
}

void GfxRenderer::rotateCoordinates(const int x, const int y, int* rotatedX, int* rotatedY) const {
  switch (orientation) {
    case Portrait: {
      
      
      *rotatedX = y;
      *rotatedY = panelHeight - 1 - x;
      break;
    }
    case LandscapeClockwise: {
      
      *rotatedX = panelWidth - 1 - x;
      *rotatedY = panelHeight - 1 - y;
      break;
    }
    case PortraitInverted: {
      
      
      *rotatedX = panelWidth - 1 - y;
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

  
  if (rotatedX < 0 || rotatedX >= panelWidth || rotatedY < 0 || rotatedY >= panelHeight) {
    Serial.printf("[%lu] [GFX] !! Outside range (%d, %d) -> (%d, %d)\n", millis(), x, y, rotatedX, rotatedY);
    return;
  }

  
  const uint32_t byteIndex = rotatedY * panelWidthBytes + (rotatedX / 8);
  const uint8_t bitPosition = 7 - (rotatedX % 8);  

  if (state) {
    frameBuffer[byteIndex] &= ~(1 << bitPosition);  
  } else {
    frameBuffer[byteIndex] |= 1 << bitPosition;  
  }
}

bool GfxRenderer::readPixel(const int x, const int y) const {
  const uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  int rotatedX = 0;
  int rotatedY = 0;
  rotateCoordinates(x, y, &rotatedX, &rotatedY);
  if (rotatedX < 0 || rotatedX >= panelWidth || rotatedY < 0 || rotatedY >= panelHeight) {
    return false;
  }

  const uint32_t byteIndex = rotatedY * panelWidthBytes + (rotatedX / 8);
  const uint8_t bitPosition = 7 - (rotatedX % 8);
  return (frameBuffer[byteIndex] & (1 << bitPosition)) == 0;
}

bool GfxRenderer::readPackedRow1bpp(const int x, const int y, const int width, uint8_t* outRow) const {
  if (!outRow || width <= 0) {
    return false;
  }
  const int rowBytes = (width + 7) / 8;
  memset(outRow, 0, static_cast<size_t>(rowBytes));

  const uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  for (int col = 0; col < width; col++) {
    int rotatedX = 0;
    int rotatedY = 0;
    rotateCoordinates(x + col, y, &rotatedX, &rotatedY);
    if (rotatedX < 0 || rotatedX >= panelWidth || rotatedY < 0 || rotatedY >= panelHeight) {
      continue;
    }
    const uint32_t byteIndex = rotatedY * panelWidthBytes + (rotatedX / 8);
    const uint8_t bitPosition = 7 - (rotatedX % 8);
    if ((frameBuffer[byteIndex] & (1 << bitPosition)) == 0) {
      outRow[col / 8] |= static_cast<uint8_t>(0x80 >> (col % 8));
    }
  }
  return true;
}

void GfxRenderer::drawPackedRow1bpp(const int x, const int y, const int width, const uint8_t* row) const {
  if (!row || width <= 0) {
    return;
  }
  uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    return;
  }

  for (int col = 0; col < width; col++) {
    int rotatedX = 0;
    int rotatedY = 0;
    rotateCoordinates(x + col, y, &rotatedX, &rotatedY);
    if (rotatedX < 0 || rotatedX >= panelWidth || rotatedY < 0 || rotatedY >= panelHeight) {
      continue;
    }
    const bool ink = (row[col / 8] & (0x80 >> (col % 8))) != 0;
    const uint32_t byteIndex = rotatedY * panelWidthBytes + (rotatedX / 8);
    const uint8_t bitPosition = 7 - (rotatedX % 8);
    if (ink) {
      frameBuffer[byteIndex] &= static_cast<uint8_t>(~(1 << bitPosition));
    } else {
      frameBuffer[byteIndex] |= static_cast<uint8_t>(1 << bitPosition);
    }
  }
}

void GfxRenderer::clearScreen(const uint8_t color) const { display.clearScreen(color); }

void GfxRenderer::invertScreen() const {
  uint8_t* buffer = display.getFrameBuffer();
  if (!buffer) {
    Serial.printf("[%lu] [GFX] !! No framebuffer in invertScreen\n", millis());
    return;
  }
  for (uint32_t i = 0; i < frameBufferSize; i++) {
    buffer[i] = ~buffer[i];
  }
}

void GfxRenderer::displayBuffer(const HalDisplay::RefreshMode refreshMode) const { display.displayBuffer(refreshMode); }

bool GfxRenderer::deviceIsX3() const { return display.deviceIsX3(); }

int GfxRenderer::getScreenWidth() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      
      return panelHeight;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      
      return panelWidth;
  }
  return panelHeight;
}

int GfxRenderer::getScreenHeight() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      
      return panelWidth;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      
      return panelHeight;
  }
  return panelWidth;
}

uint8_t* GfxRenderer::getFrameBuffer() const { return display.getFrameBuffer(); }

size_t GfxRenderer::getBufferSize() const { return frameBufferSize; }




void GfxRenderer::copyGrayscaleLsbBuffers() const { display.copyGrayscaleLsbBuffers(display.getFrameBuffer()); }

void GfxRenderer::copyGrayscaleMsbBuffers() const { display.copyGrayscaleMsbBuffers(display.getFrameBuffer()); }

void GfxRenderer::displayGrayBuffer(const bool quality) const { display.displayGrayBuffer(quality); }

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

  
  if (bwBufferChunks.empty()) {
    bwBufferChunks.assign((frameBufferSize + BW_BUFFER_CHUNK_SIZE - 1) / BW_BUFFER_CHUNK_SIZE, nullptr);
  }

  for (size_t i = 0; i < bwBufferChunks.size(); i++) {
    
    if (bwBufferChunks[i]) {
      Serial.printf("[%lu] [GFX] !! BW buffer chunk %zu already stored - this is likely a bug, freeing chunk\n",
                    millis(), i);
      free(bwBufferChunks[i]);
      bwBufferChunks[i] = nullptr;
    }

    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    const size_t chunkSize = std::min(BW_BUFFER_CHUNK_SIZE, static_cast<size_t>(frameBufferSize) - offset);
    bwBufferChunks[i] = static_cast<uint8_t*>(malloc(chunkSize));

    if (!bwBufferChunks[i]) {
      Serial.printf("[%lu] [GFX] !! Failed to allocate BW buffer chunk %zu (%zu bytes)\n", millis(), i,
                    chunkSize);
      
      freeBwBufferChunks();
      return false;
    }

    memcpy(bwBufferChunks[i], frameBuffer + offset, chunkSize);
  }

  Serial.printf("[%lu] [GFX] Stored BW buffer in %zu chunks (%zu bytes each)\n", millis(), bwBufferChunks.size(),
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

  for (size_t i = 0; i < bwBufferChunks.size(); i++) {
    
    if (!bwBufferChunks[i]) {
      Serial.printf("[%lu] [GFX] !! BW buffer chunks not stored - this is likely a bug\n", millis());
      freeBwBufferChunks();
      return;
    }

    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    const size_t chunkSize = std::min(BW_BUFFER_CHUNK_SIZE, static_cast<size_t>(frameBufferSize) - offset);
    memcpy(frameBuffer + offset, bwBufferChunks[i], chunkSize);
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

void GfxRenderer::removeAllStreamingFonts() {
  if (streamingFonts.empty()) {
    return;
  }

  std::set<const EpdFontData*> streamingData;
  for (const auto& entry : streamingFonts) {
    streamingData.insert(entry.first);
  }

  for (auto it = fontMap.begin(); it != fontMap.end();) {
    bool usesStreamingData = false;
    for (const auto style :
         {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC}) {
      const EpdFontData* data = it->second.getData(style);
      if (data != nullptr && streamingData.count(data) != 0) {
        usesStreamingData = true;
        break;
      }
    }

    if (usesStreamingData) {
      it = fontMap.erase(it);
    } else {
      ++it;
    }
  }

  streamingFonts.clear();
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
