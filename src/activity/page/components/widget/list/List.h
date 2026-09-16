#pragma once

class GfxRenderer;

namespace widget::list {

class List final {
 public:
  static void render(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex,
                     bool drawSelection = true);
  static void renderSelection(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex);
  static int visibleStartIndex(int selectedIndex, int count);
  static void preview(GfxRenderer& renderer, int x, int y, int width, int height);
};

}  // namespace widget::list
