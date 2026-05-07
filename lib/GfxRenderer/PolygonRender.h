#pragma once

class GfxRenderer;

class PolygonRender {
 public:
  explicit PolygonRender(GfxRenderer& g) : gfx(g) {}
  void render(const int* xPoints, const int* yPoints, int numPoints, bool filled, bool state = true) const;

 private:
  GfxRenderer& gfx;
};
