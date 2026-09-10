#pragma once

#include <cstdint>

class GfxRenderer;

namespace widget {

/** Thin mode dispatcher for the Home recent widget. */
class Recent final {
 public:
  enum class Mode : uint8_t { Flow = 0, Grid = 1, List = 2, Carousel = 3 };

  explicit Recent(GfxRenderer& renderer) : renderer_(renderer) {}

  static Mode modeFromSetting(uint8_t value);
  static const char* modeLabel(Mode mode);

  void render(Mode mode, int x, int y, int width, int height, int selectedIndex = 0) const;
  void preview(Mode mode, int x, int y, int width, int height) const;

 private:
  GfxRenderer& renderer_;
};

}  // namespace widget
