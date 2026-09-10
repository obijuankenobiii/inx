#include "Recent.h"

#include <GfxRenderer.h>

#include "flow/Flow.h"
#include "grid/Grid.h"
#include "list/List.h"
#include "carousel/Carousel.h"
#include "state/SystemSetting.h"

namespace widget {

Recent::Mode Recent::modeFromSetting(const uint8_t value) {
  switch (value) {
    case SystemSetting::RECENT_GRID:
      return Mode::Grid;
    case SystemSetting::RECENT_BOOK_LIST:
      return Mode::List;
    case SystemSetting::RECENT_CAROUSEL:
      return Mode::Carousel;
    case SystemSetting::RECENT_FLOW:
    case SystemSetting::RECENT_LIST_DEPRECATED:
    case SystemSetting::RECENT_SIMPLE:
    default:
      return Mode::Flow;
  }
}

const char* Recent::modeLabel(const Mode mode) {
  switch (mode) {
    case Mode::Grid:
      return "Grid";
    case Mode::List:
      return "List";
    case Mode::Carousel:
      return "Carousel";
    case Mode::Flow:
    default:
      return "Flow";
  }
}

void Recent::render(const Mode mode, const int x, const int y, const int width, const int height,
                    const int selectedIndex) const {
  if (width <= 0 || height <= 0) return;
  switch (mode) {
    case Mode::Grid:
      grid::Grid::render(renderer_, x, y, width, height, selectedIndex);
      return;
    case Mode::List:
      list::List::render(renderer_, x, y, width, height, selectedIndex);
      return;
    case Mode::Carousel: {
      const int topHeight = std::max(1, height / 2);
      carousel::Carousel::render(renderer_, x, y, width, topHeight, selectedIndex);
      renderer_.rectangle.fill(x, y + topHeight, width, 1, static_cast<int>(GfxRenderer::FillTone::Gray));
      carousel::Carousel::renderBottom(renderer_, x, y + topHeight, width, height - topHeight, selectedIndex);
      return;
    }
    case Mode::Flow:
    default:
      flow::Flow::render(renderer_, x, y, width, height, selectedIndex);
      return;
  }
}

void Recent::preview(const Mode mode, const int x, const int y, const int width, const int height) const {
  if (width <= 0 || height <= 0) return;
  renderer_.rectangle.fill(x, y, width, height, false);
  switch (mode) {
    case Mode::Grid:
      grid::Grid::preview(renderer_, x, y, width, height);
      return;
    case Mode::List:
      list::List::preview(renderer_, x, y, width, height);
      return;
    case Mode::Carousel: {
      const int topHeight = std::max(1, height / 2);
      carousel::Carousel::preview(renderer_, x, y, width, topHeight);
      renderer_.rectangle.fill(x, y + topHeight, width, 1, static_cast<int>(GfxRenderer::FillTone::Gray));
      carousel::Carousel::previewBottom(renderer_, x, y + topHeight, width, height - topHeight);
      return;
    }
    case Mode::Flow:
    default:
      flow::Flow::preview(renderer_, x, y, width, height);
      return;
  }
}

}  // namespace widget
