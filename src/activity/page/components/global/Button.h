#pragma once

#include "system/UiLayout.h"

class GfxRenderer;

struct ButtonBounds {
  int x;
  int y;
  int width;
  int height;
};

class Button {
 public:
  static constexpr int horizontalPadding = 25;
  static constexpr int height = UiLayout::LIST_ITEM_HEIGHT;

  static int width(const GfxRenderer& renderer, const char* label, int font);
  static void render(const GfxRenderer& renderer, const ButtonBounds& bounds, const char* label, bool fill,
                     int font);
};

