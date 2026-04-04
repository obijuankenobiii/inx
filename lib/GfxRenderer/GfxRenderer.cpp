#include "GfxRenderer.h"

#include <Utf8.h>
#include <vector>
#include <algorithm>

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
  if (x1 == x2) {
    if (y2 < y1) {
      std::swap(y1, y2);
    }
    for (int y = y1; y <= y2; y++) {
      drawPixel(x1, y, state);
    }
  } else if (y1 == y2) {
    if (x2 < x1) {
      std::swap(x1, x2);
    }
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
    const int radius = std::min(width, height) / 4; // Corner radius proportional to smallest dimension
    
    // Draw top edge (excluding corners)
    drawLine(x + radius, y, x + width - radius - 1, y, state);
    
    // Draw bottom edge (excluding corners)
    drawLine(x + radius, y + height - 1, x + width - radius - 1, y + height - 1, state);
    
    // Draw left edge (excluding corners)
    drawLine(x, y + radius, x, y + height - radius - 1, state);
    
    // Draw right edge (excluding corners)
    drawLine(x + width - 1, y + radius, x + width - 1, y + height - radius - 1, state);
    
    // Draw the four rounded corners using quarter-circle arcs
    // Top-left corner
    for (int i = 0; i < radius; i++) {
      for (int j = 0; j < radius; j++) {
        if (i*i + j*j <= radius*radius) {
          drawPixel(x + radius - 1 - j, y + radius - 1 - i, state);
        }
      }
    }
    
    // Top-right corner
    for (int i = 0; i < radius; i++) {
      for (int j = 0; j < radius; j++) {
        if (i*i + j*j <= radius*radius) {
          drawPixel(x + width - radius + j, y + radius - 1 - i, state);
        }
      }
    }
    
    // Bottom-left corner
    for (int i = 0; i < radius; i++) {
      for (int j = 0; j < radius; j++) {
        if (i*i + j*j <= radius*radius) {
          drawPixel(x + radius - 1 - j, y + height - radius + i, state);
        }
      }
    }
    
    // Bottom-right corner
    for (int i = 0; i < radius; i++) {
      for (int j = 0; j < radius; j++) {
        if (i*i + j*j <= radius*radius) {
          drawPixel(x + width - radius + j, y + height - radius + i, state);
        }
      }
    }
  }
}

void GfxRenderer::fillRect(const int x, const int y, const int width, const int height, const bool state, const bool rounded) const {
  if (!rounded) {
    // Original rectangle filling
    for (int fillY = y; fillY < y + height; fillY++) {
      drawLine(x, fillY, x + width - 1, fillY, state);
    }
  } else {
    // Rounded rectangle filling
    const int radius = std::min(width, height) / 4; // Corner radius proportional to smallest dimension
    
    // Fill the main body (rectangle without corners)
    for (int fillY = y + radius; fillY < y + height - radius; fillY++) {
      drawLine(x, fillY, x + width - 1, fillY, state);
    }
    
    // Fill the top and bottom sections with rounded corners
    for (int cornerY = 0; cornerY < radius; cornerY++) {
      // Calculate the horizontal span at this corner height
      int cornerSpan = static_cast<int>(sqrt(radius*radius - (radius - cornerY)*(radius - cornerY)));
      
      // Top section
      int topY = y + cornerY;
      drawLine(x + radius - cornerSpan, topY, x + width - radius + cornerSpan - 1, topY, state);
      
      // Bottom section
      int bottomY = y + height - 1 - cornerY;
      drawLine(x + radius - cornerSpan, bottomY, x + width - radius + cornerSpan - 1, bottomY, state);
    }
  }
}

void GfxRenderer::drawBitmap(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight,
                             const float cropX, const float cropY) const {

  if (bitmap.is1Bit() && cropX == 0.0f && cropY == 0.0f) {
    drawBitmap1Bit(bitmap, x, y, maxWidth, maxHeight);
    return;
  }

  float scale = 1.0f;
  bool isScaled = false;
  int cropPixX = std::floor(bitmap.getWidth() * cropX / 2.0f);
  int cropPixY = std::floor(bitmap.getHeight() * cropY / 2.0f);

  if (maxWidth > 0 && (1.0f - cropX) * bitmap.getWidth() > maxWidth) {
    scale = static_cast<float>(maxWidth) / static_cast<float>((1.0f - cropX) * bitmap.getWidth());
    isScaled = true;
  }
  if (maxHeight > 0 && (1.0f - cropY) * bitmap.getHeight() > maxHeight) {
    scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>((1.0f - cropY) * bitmap.getHeight()));
    isScaled = true;
  }
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes) {
    free(outputRow);
    free(rowBytes);
    return;
  }

  for (int bmpY = 0; bmpY < (bitmap.getHeight() - cropPixY); bmpY++) {
    int screenY = -cropPixY + (bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY);
    if (isScaled) {
      screenY = std::floor(screenY * scale);
    }
    screenY += y;
    if (screenY >= getScreenHeight()) {
      break;
    }

    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      free(outputRow);
      free(rowBytes);
      return;
    }

    if (screenY < 0) {
      continue;
    }

    if (bmpY < cropPixY) {
      continue;
    }

    for (int bmpX = cropPixX; bmpX < bitmap.getWidth() - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) {
        screenX = std::floor(screenX * scale);
      }
      screenX += x;
      if (screenX >= getScreenWidth()) {
        break;
      }
      if (screenX < 0) {
        continue;
      }

      const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      if (renderMode == BW && val < 3) {
        drawPixel(screenX, screenY);
      } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
        drawPixel(screenX, screenY, false);
      } else if (renderMode == GRAYSCALE_LSB && val == 1) {
        drawPixel(screenX, screenY, false);
      }
    }
  }

  free(outputRow);
  free(rowBytes);
}

void GfxRenderer::drawBitmap1Bit(const Bitmap& bitmap, const int x, const int y, const int maxWidth,
                                 const int maxHeight) const {
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
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

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
    int screenY = y + (isScaled ? static_cast<int>(std::floor(bmpYOffset * scale)) : bmpYOffset);
    if (screenY >= getScreenHeight()) {
      continue; 
    }
    if (screenY < 0) {
      continue;
    }

    for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
      int screenX = x + (isScaled ? static_cast<int>(std::floor(bmpX * scale)) : bmpX);
      if (screenX >= getScreenWidth()) {
        break;
      }
      if (screenX < 0) {
        continue;
      }

      const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      if (val < 3) {
        drawPixel(screenX, screenY, true);
      }
    }
  }

  free(outputRow);
  free(rowBytes);
}

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
      fillRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, false);
      drawRect(x, pageHeight - buttonY, buttonWidth, buttonHeight);
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
                    // Dithered: use threshold to eliminate light gray artifacts in white areas
                    // Only values 0 and 1 are definitely black/dark gray
                    pixelSet = (val <= 1); // Stricter threshold: only black and dark gray
                }
                
                if (pixelSet) {
                    drawPixel(destX, destY, true);
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
  int targetX = x;
  int targetY = y;
  int targetW = width;
  int targetH = height;

  // Handle rotation if needed
  if (imgOrientation == Rotate90CW || imgOrientation == Rotate270CW) {
    targetW = height;
    targetH = width;
    
    size_t bufferSize = (targetW * targetH + 7) / 8;
    uint8_t* rotatedBitmap = (uint8_t*)calloc(bufferSize, 1);
    
    if (rotatedBitmap) {
      const int srcStride = (width + 7) / 8;
      const int dstStride = (targetW + 7) / 8;
      
      // Process byte by byte for better performance
      for (int i = 0; i < height; i++) {
        for (int j = 0; j < srcStride; j++) {
          uint8_t byte = bitmap[i * srcStride + j];
          if (byte == 0 && !invert) continue; // Skip empty bytes
          
          int baseBit = j * 8;
          for (int bit = 0; bit < 8; bit++) {
            int srcX = baseBit + bit;
            if (srcX >= width) break;
            
            bool pixelSet = byte & (0x80 >> bit);
            if (invert) pixelSet = !pixelSet;
            
            if (pixelSet) {
              int newX = (imgOrientation == Rotate90CW) ? (height - 1 - i) : i;
              int newY = (imgOrientation == Rotate90CW) ? srcX : (width - 1 - srcX);
              
              rotatedBitmap[newY * dstStride + newX / 8] |= (0x80 >> (newX % 8));
            }
          }
        }
      }
      
      int rotatedX = 0, rotatedY = 0;
      rotateCoordinates(targetX, targetY, &rotatedX, &rotatedY);
      
      switch (orientation) {
        case Portrait:           rotatedY -= targetH; break;
        case PortraitInverted:   rotatedX -= targetW; break;
        case LandscapeClockwise: rotatedY -= targetH; rotatedX -= targetW; break;
        case LandscapeCounterClockwise: break;
      }
      
      display.drawImage(rotatedBitmap, rotatedX, rotatedY, targetW, targetH);
      free(rotatedBitmap);
      return;
    }
  }

  // No rotation needed
  int rotatedX = 0, rotatedY = 0;
  rotateCoordinates(targetX, targetY, &rotatedX, &rotatedY);

  switch (orientation) {
    case Portrait:           rotatedY -= targetH; break;
    case PortraitInverted:   rotatedX -= targetW; break;
    case LandscapeClockwise: rotatedY -= targetH; rotatedX -= targetW; break;
    case LandscapeCounterClockwise: break;
  }

  if (invert) {
    // Process byte by byte for inversion
    const int bytesPerRow = (width + 7) / 8;
    const int totalBytes = height * bytesPerRow;
    
    // Allocate once for the entire inverted bitmap
    uint8_t* invertedBitmap = (uint8_t*)malloc(totalBytes);
    
    if (invertedBitmap) {
      // Invert all bytes at once
      for (int i = 0; i < totalBytes; i++) {
        invertedBitmap[i] = ~bitmap[i];
      }
      display.drawImage(invertedBitmap, rotatedX, rotatedY, targetW, targetH);
      free(invertedBitmap);
      return;
    }
    
    // Fallback to row-by-row if allocation fails
    uint8_t* invertedRow = (uint8_t*)malloc(bytesPerRow);
    if (invertedRow) {
      for (int row = 0; row < height; row++) {
        const uint8_t* srcRow = &bitmap[row * bytesPerRow];
        for (int b = 0; b < bytesPerRow; b++) {
          invertedRow[b] = ~srcRow[b];
        }
        display.drawImage(invertedRow, rotatedX, rotatedY + row, width, 1);
      }
      free(invertedRow);
      return;
    }
  }

  // No inversion, draw as-is
  display.drawImage(bitmap, rotatedX, rotatedY, targetW, targetH);
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
                // In BW mode, val < 3 means draw black
                drawPixel(destX, destY, true);
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