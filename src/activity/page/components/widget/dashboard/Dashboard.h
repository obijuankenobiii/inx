#pragma once

class GfxRenderer;

namespace widget::dashboard {

/** inx-pro-style Home layout: the latest book above the remaining recent books. */
class Dashboard final {
 public:
  static void render(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex,
                     bool carouselFocused = false);
  static void preview(GfxRenderer& renderer, int x, int y, int width, int height, bool carouselFocused = false);
};

}  // namespace widget::dashboard
