#pragma once

/**
 * @file GfxRenderer.h
 * @brief Public interface and types for GfxRenderer.
 */

#include <EpdFontFamily.h>
#include <HalDisplay.h>

#include <map>
#include <memory>

#include "../../src/system/ExternalFont.h"
#include "BitmapRender.h"
#include "IconRender.h"
#include "LineRender.h"
#include "PolygonRender.h"
#include "RectangleRender.h"
#include "TextRender.h"
#include "UiRender.h"

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };

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
  std::map<int, EpdFontFamily> fontMap;
  std::map<const EpdFontData*, std::unique_ptr<ExternalFont>> streamingFonts;

  friend class BitmapRender;
  friend class TextRender;

  void freeBwBufferChunks();
  void rotateCoordinates(int x, int y, int* rotatedX, int* rotatedY) const;

 public:
  explicit GfxRenderer(HalDisplay& halDisplay);
  ~GfxRenderer();

  static constexpr int VIEWABLE_MARGIN_TOP = 9;
  static constexpr int VIEWABLE_MARGIN_RIGHT = 3;
  static constexpr int VIEWABLE_MARGIN_BOTTOM = 3;
  static constexpr int VIEWABLE_MARGIN_LEFT = 3;

  
  void insertFont(int fontId, EpdFontFamily font);
  void insertStreamingFont(int fontId, std::unique_ptr<ExternalFont> streamingFont, const EpdFontFamily& font);
  void removeFont(int fontId);
  void removeAllStreamingFonts();
  void addStreamingFontStyle(int fontId, EpdFontFamily::Style style, std::unique_ptr<ExternalFont> streamingFont);

  void setOrientation(const Orientation o) { orientation = o; }
  Orientation getOrientation() const { return orientation; }

  
  int getScreenWidth() const;
  int getScreenHeight() const;
  void displayBuffer(const HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH) const;
  void invertScreen() const;
  void clearScreen(uint8_t color = 0xFF) const;

  /** Solid ink/paper, or Gray (50% checkerboard dither in BW, similar to light fills in list UIs). */
  enum class FillTone : uint8_t { Paper, Ink, Gray };

  
  void drawPixel(int x, int y, bool state = true) const;
  bool readPixel(int x, int y) const;
  bool readPackedRow1bpp(int x, int y, int width, uint8_t* outRow) const;
  void drawPackedRow1bpp(int x, int y, int width, const uint8_t* row) const;

  
  void drawImage(const uint8_t bitmap[], int x, int y, int width, int height,
                 ImageOrientation imgOrientation = None) const;


  /** Pixels outside the rounded clip after `Bitmap.Draw` (same geometry as rounded `fillRect`). */
  enum class BitmapRoundedCornerOutside : uint8_t {
    None = 0,
    PaperOutside = 1,             
    /** ~25% ink on screen even/even pixels outside rounded corners (matches Recent carousel dither). */
    SparseInkAlignedOutside = 2,
  };

 private:
 public:
  
  void setRenderMode(const RenderMode mode) { this->renderMode = mode; }
  RenderMode getRenderMode() const { return renderMode; }
  void copyGrayscaleLsbBuffers() const;
  void copyGrayscaleMsbBuffers() const;
  void displayGrayBuffer() const;
  bool storeBwBuffer();    
  void restoreBwBuffer();  
  void cleanupGrayscaleWithFrameBuffer() const;
  /** Drop BW shadow chunks, grayscale HAL state, and force BW mode (call when leaving image-heavy readers). */
  void resetTransientReaderState();

  
  uint8_t* getFrameBuffer() const;
  static size_t getBufferSize();
  void grayscaleRevert() const;
  void getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const;

  

  RectangleRender rectangle;
  LineRender line;
  IconRender icon;
  PolygonRender polygon;
  BitmapRender bitmap;
  TextRender text;
  UiRender ui;
};
