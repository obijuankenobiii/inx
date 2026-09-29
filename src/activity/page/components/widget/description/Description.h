#pragma once

class GfxRenderer;

namespace widget::description {

/** Left inx-pro-style cover carousel with title, author, and description details. */
class Description final {
 public:
  static void render(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex);
  static void preview(GfxRenderer& renderer, int x, int y, int width, int height);
};

}  // namespace widget::description
