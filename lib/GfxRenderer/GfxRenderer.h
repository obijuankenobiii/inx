#pragma once

/**
 * @file GfxRenderer.h
 * @brief Public interface and types for GfxRenderer.
 */

#include <EpdFontFamily.h>
#include <HalDisplay.h>

#include <map>

#include "Bitmap.h"

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };

  /** How 2bpp grays map to ink when drawing scaled bitmaps in BW (reader can override per session). */
  enum class BitmapGrayRenderStyle : uint8_t {
    Balanced,  ///< Legacy: only dark gray + black ink (light gray omitted)
    FullGray,  ///< "Balance" contrast: ink both gray stages (former full-gray behavior)
    Dark       ///< Stronger ink / tighter snap than FullGray
  };

  
  enum Orientation {
    Portrait,                  
    LandscapeClockwise,        
    PortraitInverted,          
    LandscapeCounterClockwise  
  };

  
  enum ImageOrientation { None, Rotate90CW, Rotate180, Rotate270CW };

 private:
  static constexpr size_t BW_BUFFER_CHUNK_SIZE = 8000;  
  static constexpr size_t BW_BUFFER_NUM_CHUNKS = HalDisplay::BUFFER_SIZE / BW_BUFFER_CHUNK_SIZE;
  static_assert(BW_BUFFER_CHUNK_SIZE * BW_BUFFER_NUM_CHUNKS == HalDisplay::BUFFER_SIZE,
                "BW buffer chunking does not line up with display buffer size");

  HalDisplay& display;
  RenderMode renderMode;
  Orientation orientation;
  uint8_t* bwBufferChunks[BW_BUFFER_NUM_CHUNKS] = {nullptr};
  /** Set true if any drawBitmap in this page pass had enough mid-gray pixels to warrant the e-ink grayscale pass. */
  mutable bool anyBitmapImageWantsGrayscale = false;
  mutable BitmapGrayRenderStyle bitmapGrayRenderStyle = BitmapGrayRenderStyle::Balanced;
  std::map<int, EpdFontFamily> fontMap;
  void renderChar(const EpdFontFamily& fontFamily, uint32_t cp, int* x, const int* y, bool pixelState,
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

  
  void insertFont(int fontId, EpdFontFamily font);

  
  void setOrientation(const Orientation o) { orientation = o; }
  Orientation getOrientation() const { return orientation; }

  
  int getScreenWidth() const;
  int getScreenHeight() const;
  void displayBuffer(HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH) const;
  void invertScreen() const;
  void clearScreen(uint8_t color = 0xFF) const;

  /** Solid ink/paper, or Gray (50% checkerboard dither in BW, similar to light fills in list UIs). */
  enum class FillTone : uint8_t { Paper, Ink, Gray };

  
  void drawPixel(int x, int y, bool state = true) const;
  void drawLine(int x1, int y1, int x2, int y2, bool state = true) const;
  void drawRect(const int x, const int y, const int width, const int height, const bool state = true,
                const bool rounded = false) const;
  void fillRect(const int x, const int y, const int width, const int height, FillTone tone, bool rounded = false) const;
  void fillRect(const int x, const int y, const int width, const int height, const bool state = true,
                const bool rounded = false) const;

  
  void drawImage(const uint8_t bitmap[], int x, int y, int width, int height,
                 ImageOrientation imgOrientation = None) const;

  /** 1bpp MSB-first (MSB 1 = paper, 0 = ink); drawPixel in logical coords. invert XORs ink vs paper. */
  void drawIcon(const uint8_t bitmap[], int x, int y, int width, int height, ImageOrientation imgOrientation = None,
                bool invert = false) const;

  void drawBitmap(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight, float cropX = 0,
                  float cropY = 0) const;
  void drawBitmap1Bit(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight) const;

  void setBitmapGrayRenderStyle(BitmapGrayRenderStyle s) const { bitmapGrayRenderStyle = s; }
  BitmapGrayRenderStyle getBitmapGrayRenderStyle() const { return bitmapGrayRenderStyle; }
  void fillPolygon(const int* xPoints, const int* yPoints, int numPoints, bool state = true) const;

  
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

  
  void drawButtonHints(int fontId, const char* btn1, const char* btn2, const char* btn3, const char* btn4);
  void drawSideButtonHints(int fontId, const char* topBtn, const char* bottomBtn) const;

 private:
  /** BW mode: map 2bpp palette stage (0–3) to screen using halftones so levels 1 and 2 are not solid black. */
  void drawBwFrom2bppStage(int px, int py, uint8_t stage03) const;
  
  void drawTextRotated90CW(int fontId, int x, int y, const char* text, bool black = true,
                           EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  int getTextHeight(int fontId) const;

 public:
  
  void setRenderMode(const RenderMode mode) { this->renderMode = mode; }
  void copyGrayscaleLsbBuffers() const;
  void copyGrayscaleMsbBuffers() const;
  void displayGrayBuffer() const;
  bool storeBwBuffer();    
  void restoreBwBuffer();  
  void cleanupGrayscaleWithFrameBuffer() const;
  void resetBitmapGrayscaleDetection() const { anyBitmapImageWantsGrayscale = false; }
  bool needsBitmapGrayscale() const;

  
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
void drawTransparentImage2Bit(const uint8_t bitmap[], int x, int y, int width, int height,
                             uint8_t alphaThreshold, ImageOrientation imgOrientation = None) const;

};

/**
 * RAII: temporarily sets scaled-bitmap gray style for drawBitmap; restores on destruction.
 */
struct BitmapGrayStyleScope {
  GfxRenderer& r;
  GfxRenderer::BitmapGrayRenderStyle prev;
  BitmapGrayStyleScope(GfxRenderer& renderer, GfxRenderer::BitmapGrayRenderStyle style)
      : r(renderer), prev(renderer.getBitmapGrayRenderStyle()) {
    r.setBitmapGrayRenderStyle(style);
  }
  ~BitmapGrayStyleScope() { r.setBitmapGrayRenderStyle(prev); }
  BitmapGrayStyleScope(const BitmapGrayStyleScope&) = delete;
  BitmapGrayStyleScope& operator=(const BitmapGrayStyleScope&) = delete;
};