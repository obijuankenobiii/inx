#pragma once

class GfxRenderer;

namespace widget::carousel {

/** inx-pro Left-layout recent carousel: cover cards only, with no label or background. */
class Carousel final {
 public:
  static void render(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex);
  static void renderRemaining(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex,
                              bool showSelection = false);
  static void preview(GfxRenderer& renderer, int x, int y, int width, int height);
  static void previewRemaining(GfxRenderer& renderer, int x, int y, int width, int height, bool showSelection = false);
  static void renderBottom(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex);
  static void previewBottom(GfxRenderer& renderer, int x, int y, int width, int height);
};

}  // namespace widget::carousel
