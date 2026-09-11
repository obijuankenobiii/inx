#pragma once

class GfxRenderer;

struct ToggleBounds {
  int x;
  int y;
  int width;
  int height;
};

/** Reusable Pro-style on/off toggle for settings rows. */
class Toggle {
 public:
  static constexpr int width = 52;
  static constexpr int height = 30;

  static ToggleBounds bounds(int valueColumnRight, int itemY, int itemHeight);
  static void render(const GfxRenderer& renderer, const ToggleBounds& bounds, bool on, bool rowSelected = false);
  static void render(const GfxRenderer& renderer, int valueColumnRight, int itemY, int itemHeight, bool on,
                     bool rowSelected = false);
};
