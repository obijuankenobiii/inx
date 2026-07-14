#pragma once

class GfxRenderer;

class LineRender {
 public:
  enum class Style { Solid, Dotted };

  explicit LineRender(GfxRenderer& g) : gfx(g) {}
  void render(int x1, int y1, int x2, int y2, bool state = true, Style style = Style::Solid) const;

 private:
  GfxRenderer& gfx;
};
