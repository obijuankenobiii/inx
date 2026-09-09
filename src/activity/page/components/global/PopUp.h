#pragma once

#include <string>
#include <vector>

class GfxRenderer;

struct PopUpBounds {
  int x;
  int y;
  int width;
  int height;
  int header;
  int row;
  int rows;
};

/** Shared selector popup used by settings and other list-based sub-pages. */
class PopUp {
 public:
  static constexpr int maxRows = 6;

  static PopUpBounds bounds(const GfxRenderer& renderer, int count, int contentTop = 0);
  static void background(GfxRenderer& renderer, const PopUpBounds& box);
  static void title(GfxRenderer& renderer, const PopUpBounds& box, const std::string& value);
  static void list(GfxRenderer& renderer, const PopUpBounds& box, const std::vector<std::string>& values,
                   int selected, int scroll);
  static void border(GfxRenderer& renderer, const PopUpBounds& box);
};
