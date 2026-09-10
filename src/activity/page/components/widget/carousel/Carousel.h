#pragma once

class GfxRenderer;

namespace widget::carousel {

/** inx-pro Left-layout recent carousel: cover cards only, with no label or background. */
class Carousel final {
 public:
  static void render(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex);
  static void preview(GfxRenderer& renderer, int x, int y, int width, int height);
  static void renderBottom(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex);
  static void previewBottom(GfxRenderer& renderer, int x, int y, int width, int height);
};

}  // namespace widget::carousel
