#pragma once

class GfxRenderer;

namespace widget::grid2x2 {

/** Two-column, two-row recent widget using the Pro Left-carousel thumbnail treatment. */
class Grid2x2 final {
 public:
  static void render(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex);
  static void preview(GfxRenderer& renderer, int x, int y, int width, int height);
};

}  // namespace widget::grid2x2
