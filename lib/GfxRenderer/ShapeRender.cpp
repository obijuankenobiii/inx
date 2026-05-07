/**
 * @file ShapeRender.cpp
 */

#include "ShapeRender.h"

#include "GfxRenderer.h"

void ShapeRender::RectangleOps::render(const int x, const int y, const int width, const int height, const bool filled,
                                     const bool rounded) const {
  if (filled) {
    gfx.rectangle.fill(x, y, width, height, true, rounded);
  } else {
    gfx.rectangle.render(x, y, width, height, true, rounded);
  }
}

void ShapeRender::PolygonOps::render(const int* xPoints, const int* yPoints, const int numPoints, const bool filled) const {
  if (numPoints < 2) {
    return;
  }
  gfx.polygon.render(xPoints, yPoints, numPoints, filled, true);
}
