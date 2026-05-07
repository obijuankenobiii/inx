#include "RectangleRender.h"

#include <algorithm>
#include <cmath>

#include "GfxRenderer.h"

namespace {
int RoundedRectCornerRadius(const int width, const int height) {
  const int m = std::min(width, height);
  if (m < 5) {
    return 1;
  }
  int r = m / 10;
  if (r < 2) {
    r = 2;
  }
  if (2 * r > m) {
    r = m / 2;
  }
  return std::max(1, r);
}

int CornerSpanFromRy(const int r, const int ry) {
  const int inner = r * r - ry * ry;
  if (inner <= 0) {
    return 0;
  }
  return static_cast<int>(std::sqrt(static_cast<double>(inner)));
}
}  // namespace

void RectangleRender::render(const int x, const int y, const int width, const int height, const bool state,
                           const bool rounded) const {
  if (!rounded) {
    gfx.line.render(x, y, x + width - 1, y, state);
    gfx.line.render(x + width - 1, y, x + width - 1, y + height - 1, state);
    gfx.line.render(x + width - 1, y + height - 1, x, y + height - 1, state);
    gfx.line.render(x, y, x, y + height - 1, state);
    return;
  }

  const int radius = RoundedRectCornerRadius(width, height);
  const int left = x + radius;
  const int right = x + width - radius - 1;
  if (right > left) {
    gfx.line.render(left + 1, y, right - 1, y, state);
    gfx.line.render(left + 1, y + height - 1, right - 1, y + height - 1, state);
  }
  const int top = y + radius;
  const int bottom = y + height - radius - 1;
  if (bottom > top) {
    gfx.line.render(x, top + 1, x, bottom - 1, state);
    gfx.line.render(x + width - 1, top + 1, x + width - 1, bottom - 1, state);
  }

  for (int h = 0; h <= radius; ++h) {
    const int span = CornerSpanFromRy(radius, radius - h);
    gfx.drawPixel(x + radius - span, y + h, state);
    gfx.drawPixel(x + width - radius - 1 + span, y + h, state);
    gfx.drawPixel(x + radius - span, y + height - 1 - h, state);
    gfx.drawPixel(x + width - radius - 1 + span, y + height - 1 - h, state);
  }
}

void RectangleRender::dotted(const int x, const int y, const int width, const int height, const bool state) const {
  if (width <= 0 || height <= 0) {
    return;
  }
  constexpr int kDash = 2;
  constexpr int kGap = 2;
  const int x1 = x;
  const int y1 = y;
  const int x2 = x + width - 1;
  const int y2 = y + height - 1;
  const int sw = gfx.getScreenWidth();
  const int sh = gfx.getScreenHeight();

  auto hSeg = [&](const int cy, int xa, int xb) {
    if (cy < 0 || cy >= sh) {
      return;
    }
    if (xa > xb) {
      std::swap(xa, xb);
    }
    int xi = xa;
    while (xi <= xb) {
      for (int d = 0; d < kDash && xi <= xb; d++, xi++) {
        if (xi >= 0 && xi < sw) {
          gfx.drawPixel(xi, cy, state);
        }
      }
      xi += kGap;
    }
  };
  auto vSeg = [&](const int cx, int ya, int yb) {
    if (cx < 0 || cx >= sw) {
      return;
    }
    if (ya > yb) {
      std::swap(ya, yb);
    }
    int yi = ya;
    while (yi <= yb) {
      for (int d = 0; d < kDash && yi <= yb; d++, yi++) {
        if (yi >= 0 && yi < sh) {
          gfx.drawPixel(cx, yi, state);
        }
      }
      yi += kGap;
    }
  };

  hSeg(y1, x1, x2);
  if (y2 != y1) {
    hSeg(y2, x1, x2);
  }
  vSeg(x1, y1, y2);
  if (x2 != x1) {
    vSeg(x2, y1, y2);
  }
}

void RectangleRender::fill(const int x, const int y, const int width, const int height, const bool state,
                           const bool rounded) const {
  fill(x, y, width, height, state ? static_cast<int>(GfxRenderer::FillTone::Ink)
                                  : static_cast<int>(GfxRenderer::FillTone::Paper),
       rounded);
}

void RectangleRender::fill(const int x, const int y, const int width, const int height, const int tone,
                           const bool rounded) const {
  const auto fillTone = static_cast<GfxRenderer::FillTone>(tone);
  if (fillTone == GfxRenderer::FillTone::Gray) {
    if (rounded) {
      fill(x, y, width, height, static_cast<int>(GfxRenderer::FillTone::Ink), true);
      return;
    }
    const int x1 = std::max(0, x);
    const int y1 = std::max(0, y);
    const int x2 = std::min(gfx.getScreenWidth(), x + width);
    const int y2 = std::min(gfx.getScreenHeight(), y + height);
    for (int fy = y1; fy < y2; fy++) {
      for (int fx = x1; fx < x2; fx++) {
        gfx.drawPixel(fx, fy, ((fx + fy) & 1) == 0);
      }
    }
    return;
  }

  const bool state = (fillTone == GfxRenderer::FillTone::Ink);
  if (!rounded) {
    for (int fillY = y; fillY < y + height; fillY++) {
      gfx.line.render(x, fillY, x + width - 1, fillY, state);
    }
    return;
  }

  const int radius = RoundedRectCornerRadius(width, height);
  for (int fillY = y + radius; fillY < y + height - radius; fillY++) {
    gfx.line.render(x, fillY, x + width - 1, fillY, state);
  }

  for (int cornerY = 0; cornerY < radius; cornerY++) {
    const int cornerSpan =
        static_cast<int>(std::sqrt(radius * radius - (radius - cornerY) * (radius - cornerY)));
    const int topY = y + cornerY;
    gfx.line.render(x + radius - cornerSpan, topY, x + width - radius + cornerSpan - 1, topY, state);

    const int bottomY = y + height - 1 - cornerY;
    gfx.line.render(x + radius - cornerSpan, bottomY, x + width - radius + cornerSpan - 1, bottomY, state);
  }
}
