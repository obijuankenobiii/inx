#pragma once

#include <vector>

#include "state/RecentBooks.h"

class GfxRenderer;

namespace widget::flow {

class Flow final {
 public:
  static void render(GfxRenderer& renderer, int x, int y, int width, int height, int selectedIndex);
  static void preview(GfxRenderer& renderer, int x, int y, int width, int height);
};

}  // namespace widget::flow
