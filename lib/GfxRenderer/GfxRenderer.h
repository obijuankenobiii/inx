#pragma once

#include <EpdFontFamily.h>
#include <HalDisplay.h>

#include <map>
#include <memory>

#include "../../src/system/ExternalFont.h"
#include "Bitmap.h"

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  enum ImageOrientation { None, Rotate90CW, Rotate180, Rotate270CW };

 private:
  static constexpr size_t BW_BUFFER_CHUNK_SIZE = 8000;
  static constexpr size_t BW_BUFFER_NUM_CHUNKS = HalDisplay::BUFFER_SIZE / BW_BUFFER_CHUNK_SIZE;

  HalDisplay& display;
  RenderMode renderMode;
  Orientation orientation;
  uint8_t* bwBufferChunks[BW_BUFFER_NUM_CHUNKS] = {nullptr};

  // Font Storage
  std::map<int, EpdFontFamily> fontMap;

  // Keyed by EpdFontData pointer to allow one ID to have separate files for Bold/Italic
  std::map<const EpdFontData*, std::unique_ptr<ExternalFont>> streamingFonts;

  // Text Rendering Helpers
  void renderChar(const EpdFontFamily& fontFamily, uint32_t cp, int* x, const int* y, bool pixelState,
                  EpdFontFamily::Style style) const;

  // NEW: Helper to calculate width without touching library RAM pointers
  int getStreamingTextWidth(const EpdFontFamily& family, const char* text, EpdFontFamily::Style style) const;

  // Helper to fetch glyph data from SD card streaming handles
  bool getGlyphBitmap(const EpdFontFamily& fontFamily, uint32_t offset, uint32_t length, uint8_t* outputBuffer,
                      EpdFontFamily::Style style) const;

  void freeBwBufferChunks();
  void rotateCoordinates(int x, int y, int* rotatedX, int* rotatedY) const;

 public:
  explicit GfxRenderer(HalDisplay& halDisplay) : display(halDisplay), renderMode(BW), orientation(Portrait) {}
  ~GfxRenderer() { freeBwBufferChunks(); }

  static constexpr int VIEWABLE_MARGIN_TOP = 9;
  static constexpr int VIEWABLE_MARGIN_RIGHT = 3;
  static constexpr int VIEWABLE_MARGIN_BOTTOM = 3;
  static constexpr int VIEWABLE_MARGIN_LEFT = 3;

  // Font Registration
  void insertFont(int fontId, const EpdFontFamily& font);
  void insertStreamingFont(int fontId, std::unique_ptr<ExternalFont> streamingFont, const EpdFontFamily& font);
  void removeFont(int fontId);
  void removeAllStreamingFonts();

  // Orientation control
  void setOrientation(const Orientation o) { orientation = o; }
  Orientation getOrientation() const { return orientation; }

  // Screen ops
  int getScreenWidth() const;
  int getScreenHeight() const;
  void displayBuffer(HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH) const;
  void invertScreen() const;
  void clearScreen(uint8_t color = 0xFF) const;

  // Drawing
  void drawPixel(int x, int y, bool state = true) const;
  void drawLine(int x1, int y1, int x2, int y2, bool state = true) const;
  void drawRect(const int x, const int y, const int width, const int height, const bool state = true,
                const bool rounded = false) const;
  void fillRect(const int x, const int y, const int width, const int height, const bool state = true,
                const bool rounded = false) const;

  void drawImage(const uint8_t bitmap[], int x, int y, int width, int height,
                 ImageOrientation imgOrientation = None) const;

  void drawIcon(const uint8_t bitmap[], int x, int y, int width, int height, ImageOrientation imgOrientation = None,
                bool invert = false) const;

  void drawBitmap(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight, float cropX = 0,
                  float cropY = 0) const;
  void drawBitmap1Bit(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight) const;
  void fillPolygon(const int* xPoints, const int* yPoints, int numPoints, bool state = true) const;

  // Text
  int getTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawCenteredText(int fontId, int y, const char* text, bool black = true,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawText(int fontId, int x, int y, const char* text, bool black = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  int getSpaceWidth(int fontId) const;
  int getFontAscenderSize(int fontId) const;
  int getLineHeight(int fontId) const;
  std::string truncatedText(int fontId, const char* text, int maxWidth,
                            EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;

  // UI Components
  void drawButtonHints(int fontId, const char* btn1, const char* btn2, const char* btn3, const char* btn4);
  void drawSideButtonHints(int fontId, const char* topBtn, const char* bottomBtn) const;

 private:
  void drawTextRotated90CW(int fontId, int x, int y, const char* text, bool black = true,
                           EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  int getTextHeight(int fontId) const;

 public:
  // Grayscale functions
  void setRenderMode(const RenderMode mode) { this->renderMode = mode; }
  void copyGrayscaleLsbBuffers() const;
  void copyGrayscaleMsbBuffers() const;
  void displayGrayBuffer() const;
  bool storeBwBuffer();
  void restoreBwBuffer();
  void cleanupGrayscaleWithFrameBuffer() const;

  // Low level functions
  uint8_t* getFrameBuffer() const;
  static size_t getBufferSize();
  void grayscaleRevert() const;
  void getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const;

  void drawSmallBitmapClean(const Bitmap& bitmap, const int x, const int y, const int maxWidth = 0,
                            const int maxHeight = 0) const;
  void drawSmallBitmapAdaptive(const Bitmap& bitmap, const int x, const int y, const int maxWidth = 0,
                               const int maxHeight = 0) const;
  void drawSmallBitmap(const Bitmap& bitmap, const int x, const int y, const int maxWidth = 0,
                       const int maxHeight = 0) const;

  void drawTransparentImage(const Bitmap& bitmap, int x, int y, int maxWidth = 0, int maxHeight = 0,
                            uint8_t transparentColor = 1, ImageOrientation imgOrientation = None) const;
  void drawTransparentImage2Bit(const uint8_t bitmap[], int x, int y, int width, int height, uint8_t alphaThreshold,
                                ImageOrientation imgOrientation = None) const;
};