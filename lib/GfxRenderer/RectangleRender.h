#pragma once

class GfxRenderer;

class RectangleRender {
 public:
  explicit RectangleRender(GfxRenderer& g) : gfx(g) {}
  void render(int x, int y, int width, int height, bool state = true, bool rounded = false) const;
  void dotted(int x, int y, int width, int height, bool state = true) const;
  void fill(int x, int y, int width, int height, bool state = true, bool rounded = false) const;
  void fill(int x, int y, int width, int height, int tone, bool rounded = false) const;

 private:
  GfxRenderer& gfx;
};
