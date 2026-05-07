#pragma once

class GfxRenderer;

class LineRender {
 public:
  explicit LineRender(GfxRenderer& g) : gfx(g) {}
  void render(int x1, int y1, int x2, int y2, bool state = true) const;

 private:
  GfxRenderer& gfx;
};
