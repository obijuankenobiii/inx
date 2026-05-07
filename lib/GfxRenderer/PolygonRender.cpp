#include "PolygonRender.h"

#include <cstdlib>

#include "GfxRenderer.h"

void PolygonRender::render(const int* xPoints, const int* yPoints, const int numPoints, const bool filled,
                         const bool state) const {
  if (numPoints < 2) {
    return;
  }
  if (filled) {
    if (numPoints < 3) {
      return;
    }
    int minY = yPoints[0];
    int maxY = yPoints[0];
    for (int i = 1; i < numPoints; i++) {
      if (yPoints[i] < minY) minY = yPoints[i];
      if (yPoints[i] > maxY) maxY = yPoints[i];
    }

    if (minY < 0) minY = 0;
    if (maxY >= gfx.getScreenHeight()) maxY = gfx.getScreenHeight() - 1;

    auto* nodeX = static_cast<int*>(malloc(numPoints * sizeof(int)));
    if (!nodeX) {
      Serial.printf("[%lu] [GFX] !! Failed to allocate polygon node buffer\n", millis());
      return;
    }

    for (int scanY = minY; scanY <= maxY; scanY++) {
      int nodes = 0;
      int j = numPoints - 1;
      for (int i = 0; i < numPoints; i++) {
        if ((yPoints[i] < scanY && yPoints[j] >= scanY) || (yPoints[j] < scanY && yPoints[i] >= scanY)) {
          const int dy = yPoints[j] - yPoints[i];
          if (dy != 0) {
            nodeX[nodes++] = xPoints[i] + (scanY - yPoints[i]) * (xPoints[j] - xPoints[i]) / dy;
          }
        }
        j = i;
      }

      for (int i = 0; i < nodes - 1; i++) {
        for (int k = i + 1; k < nodes; k++) {
          if (nodeX[i] > nodeX[k]) {
            const int temp = nodeX[i];
            nodeX[i] = nodeX[k];
            nodeX[k] = temp;
          }
        }
      }

      for (int i = 0; i < nodes - 1; i += 2) {
        int startX = nodeX[i];
        int endX = nodeX[i + 1];
        if (startX < 0) startX = 0;
        if (endX >= gfx.getScreenWidth()) endX = gfx.getScreenWidth() - 1;
        for (int x = startX; x <= endX; x++) {
          gfx.drawPixel(x, scanY, state);
        }
      }
    }

    free(nodeX);
    return;
  }
  for (int i = 0; i < numPoints; ++i) {
    const int j = (i + 1) % numPoints;
    gfx.line.render(xPoints[i], yPoints[i], xPoints[j], yPoints[j], state);
  }
}
