#pragma once

class GfxRenderer;

namespace widget::bookshelf {

/** Two-tier, cover-first bookshelf for the most recently opened books. */
class Bookshelf final {
 public:
  static void render(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex,
                     bool drawSelection = true);
  static void renderSelection(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex);
  static void preview(GfxRenderer& renderer, int x, int y, int width, int height);
};

}  // namespace widget::bookshelf
