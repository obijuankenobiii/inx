#pragma once

#include <cstdint>

class GfxRenderer;

class IconRender {
 public:
  enum class Orientation : uint8_t {
    None = 0,
    Rotate90CW = 1,
    Rotate180 = 2,
    Rotate270CW = 3,
  };

  explicit IconRender(GfxRenderer& g) : gfx(g) {}
  void render(const uint8_t bitmap[], int x, int y, int width, int height, Orientation orientation = Orientation::None,
            bool invert = false) const;

 private:
  GfxRenderer& gfx;
};
