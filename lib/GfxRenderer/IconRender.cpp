#include "IconRender.h"

#include "GfxRenderer.h"

namespace {

bool ReadIconBitMsbFirst(const uint8_t* data, const int width, const int x, const int y) {
  const int bytesPerRow = (width + 7) / 8;
  const int index = y * bytesPerRow + (x / 8);
  const uint8_t mask = static_cast<uint8_t>(0x80 >> (x % 8));
  return (data[index] & mask) != 0;
}

}  // namespace

void IconRender::render(const uint8_t bitmap[], const int x, const int y, const int width, const int height,
                      const Orientation orientation, const bool invert) const {
  for (int dx = 0; dx < width; ++dx) {
    for (int dy = 0; dy < height; ++dy) {
      int srcX = dx;
      int srcY = dy;
      switch (orientation) {
        case Orientation::Rotate90CW:
          srcX = dy;
          srcY = width - 1 - dx;
          break;
        case Orientation::Rotate180:
          srcX = width - 1 - dx;
          srcY = height - 1 - dy;
          break;
        case Orientation::Rotate270CW:
          srcX = height - 1 - dy;
          srcY = dx;
          break;
        case Orientation::None:
        default:
          break;
      }

      bool drawPixel = ReadIconBitMsbFirst(bitmap, width, srcX, srcY);
      if (invert) {
        drawPixel = !drawPixel;
      }
      if (drawPixel) {
        gfx.drawPixel(x + dx, y + dy, true);
      }
    }
  }
}
