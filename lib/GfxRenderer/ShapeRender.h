#pragma once

/**
 * @file ShapeRender.h
 * @brief Grouped shape operations (delegates to GfxRenderer framebuffer).
 */

class GfxRenderer;

class ShapeRender {
 public:
  class RectangleOps {
   public:
    explicit RectangleOps(GfxRenderer& g) : gfx(g) {}
    void render(int x, int y, int width, int height, bool filled, bool rounded = false) const;

   private:
    GfxRenderer& gfx;
  };

  class PolygonOps {
   public:
    explicit PolygonOps(GfxRenderer& g) : gfx(g) {}
    void render(const int* xPoints, const int* yPoints, int numPoints, bool filled) const;

   private:
    GfxRenderer& gfx;
  };

  explicit ShapeRender(GfxRenderer& g) : Rectangle(g), Polygon(g) {}

  RectangleOps Rectangle;
  PolygonOps Polygon;
};
