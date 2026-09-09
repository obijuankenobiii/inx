#pragma once

#include <string>
#include <vector>

#include "util/LibraryIndex.h"

class GfxRenderer;

namespace views {
namespace library {

/** Pro-style 3x4 indexed library grid. */
class Grid final {
 public:
  Grid(GfxRenderer& renderer, const std::vector<LibraryIndex::Book>& items);

  static constexpr int itemsPerPage() { return 12; }
  void render(int selectedIndex = -1, int page = 0) const;

 private:
  static constexpr int columns = 3;
  static constexpr int rows = 4;
  static constexpr int margin = 8;
  static constexpr int gapX = 8;
  static constexpr int minGapY = 6;
  static constexpr int labelGap = 4;
  // Match the legacy LibraryActivity grid: the label band is 28 px and the
  // text is positioned with the same small upward font offset.
  static constexpr int labelHeight = 28;
  static constexpr int maxFrame = 148;

  GfxRenderer& renderer_;
  const std::vector<LibraryIndex::Book>& items_;

  int top() const;
  int visibleHeight() const;
  void itemBounds(int index, int& x, int& y, int& width, int& height) const;
  static std::string displayTitle(const LibraryIndex::Book& item);
  void drawItem(const LibraryIndex::Book& item, int x, int y, int width, int height, bool selected) const;
};

}  // namespace library
}  // namespace views
