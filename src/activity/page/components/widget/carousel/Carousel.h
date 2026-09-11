#pragma once

#include "state/RecentBooks.h"

class GfxRenderer;

namespace widget::carousel {

struct LeftThumbnailSize {
  int width;
  int height;
};

/** inx-pro Left-layout recent carousel: cover cards only, with no label or background. */
class Carousel final {
 public:
  /** Returns the same aspect-aware thumbnail size used by the Left carousel layout. */
  static LeftThumbnailSize leftThumbnailSize(const RecentBook& book, int width, int height);
  /** Draws the same black percentage tag used by the Left carousel layout. */
  static void renderProgressTag(GfxRenderer& renderer, const RecentBook& book, int x, int y, int width, int height);
  static void render(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex);
  static void renderRemaining(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex,
                              bool showSelection = false);
  static void preview(GfxRenderer& renderer, int x, int y, int width, int height);
  static void previewRemaining(GfxRenderer& renderer, int x, int y, int width, int height, bool showSelection = false);
  static void renderBottom(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex);
  static void previewBottom(GfxRenderer& renderer, int x, int y, int width, int height);
};

}  // namespace widget::carousel
