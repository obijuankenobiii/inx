#include "Toggle.h"

#include <GfxRenderer.h>

namespace {

void fillCircleTone(const GfxRenderer& renderer, const int centerX, const int centerY, const int radius,
                    const GfxRenderer::FillTone tone) {
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dy * dy <= radius * radius) {
        if (tone == GfxRenderer::FillTone::Gray) {
          renderer.drawPixel(centerX + dx, centerY + dy, ((centerX + dx + centerY + dy) & 1) == 0);
        } else {
          renderer.drawPixel(centerX + dx, centerY + dy, tone == GfxRenderer::FillTone::Ink);
        }
      }
    }
  }
}

void fillPill(const GfxRenderer& renderer, const ToggleBounds& bounds, const GfxRenderer::FillTone tone) {
  const int radius = bounds.height / 2;
  const int leftCenter = bounds.x + radius;
  const int rightCenter = bounds.x + bounds.width - radius - 1;
  renderer.rectangle.fill(bounds.x + radius, bounds.y, bounds.width - radius * 2, bounds.height,
                          static_cast<int>(tone));
  fillCircleTone(renderer, leftCenter, bounds.y + radius, radius, tone);
  fillCircleTone(renderer, rightCenter, bounds.y + radius, radius, tone);
}

}  // namespace

ToggleBounds Toggle::bounds(const int valueColumnRight, const int itemY, const int itemHeight) {
  return {valueColumnRight - width, itemY + (itemHeight - height) / 2, width, height};
}

void Toggle::render(const GfxRenderer& renderer, const ToggleBounds& toggleBounds, const bool on,
                    const bool rowSelected) {
  const GfxRenderer::FillTone trackTone = on ? GfxRenderer::FillTone::Ink : GfxRenderer::FillTone::Gray;
  fillPill(renderer, toggleBounds, trackTone);

  const int radius = toggleBounds.height / 2 - 4;
  const int knobX = on ? toggleBounds.x + toggleBounds.width - toggleBounds.height / 2 - 1
                       : toggleBounds.x + toggleBounds.height / 2;
  fillCircleTone(renderer, knobX, toggleBounds.y + toggleBounds.height / 2, radius,
                 GfxRenderer::FillTone::Paper);

  if (rowSelected && on) {
    renderer.rectangle.render(toggleBounds.x, toggleBounds.y, toggleBounds.width, toggleBounds.height, false, true);
  }
}

void Toggle::render(const GfxRenderer& renderer, const int valueColumnRight, const int itemY, const int itemHeight,
                    const bool on, const bool rowSelected) {
  render(renderer, bounds(valueColumnRight, itemY, itemHeight), on, rowSelected);
}
