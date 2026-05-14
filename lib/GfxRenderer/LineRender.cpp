#include "LineRender.h"

#include <algorithm>

#include "GfxRenderer.h"

void LineRender::render(const int x1, const int y1, const int x2, const int y2, const bool state) const {
  int sx1 = x1;
  int sy1 = y1;
  int sx2 = x2;
  int sy2 = y2;
  const int maxX = gfx.getScreenWidth() - 1;
  const int maxY = gfx.getScreenHeight() - 1;

  if (sx1 == sx2) {
    if (sy2 < sy1) {
      std::swap(sy1, sy2);
    }
    if (sx1 < 0 || sx1 > maxX) return;
    sy1 = std::max(0, sy1);
    sy2 = std::min(sy2, maxY);
    if (sy1 > sy2) return;
    for (int y = sy1; y <= sy2; y++) {
      gfx.drawPixel(sx1, y, state);
    }
  } else if (sy1 == sy2) {
    if (sx2 < sx1) {
      std::swap(sx1, sx2);
    }
    if (sy1 < 0 || sy1 > maxY) return;
    sx1 = std::max(0, sx1);
    sx2 = std::min(sx2, maxX);
    if (sx1 > sx2) return;
    for (int x = sx1; x <= sx2; x++) {
      gfx.drawPixel(x, sy1, state);
    }
  } else {
    Serial.printf("[%lu] [GFX] Line drawing not supported\n", millis());
  }
}
