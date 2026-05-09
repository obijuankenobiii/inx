#pragma once

/**
 * @file BitmapRender.h
 * @brief Bitmap / icon drawing helpers (delegates framebuffer work to GfxRenderer).
 */

#include "Bitmap.h"
#include "ImageRenderMode.h"

class GfxRenderer;

class BitmapRender {
 public:
  enum class RoundedOutside : uint8_t {
    None = 0,
    PaperOutside = 1,
    SparseInkAlignedOutside = 2,
  };

  enum class Orientation : uint8_t {
    None = 0,
    Rotate90CW = 1,
    Rotate180 = 2,
    Rotate270CW = 3,
  };

  explicit BitmapRender(GfxRenderer& g) : gfx(g) {}

  void render(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight, float cropX = 0.f,
              float cropY = 0.f, RoundedOutside roundedOutside = RoundedOutside::None,
              ImageRenderMode mode = ImageRenderMode::OneBit) const;

  void icon(const uint8_t bitmap[], int x, int y, int width, int height,
                Orientation orientation = Orientation::None, bool invert = false) const;

  void transparent(const Bitmap& bitmap, int x, int y, int maxWidth = 0, int maxHeight = 0,
                       uint8_t transparentColor = 1, Orientation orientation = Orientation::None) const;

  void sleepScreen(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight, float cropX = 0.f,
                       float cropY = 0.f, bool coverFill = false) const;

 private:
  void oneBit(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight,
                RoundedOutside roundedOutside = RoundedOutside::None) const;
  GfxRenderer& gfx;
};
