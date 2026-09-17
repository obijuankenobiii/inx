#pragma once

#include <cstddef>

/**
 * @file Sidebar.h
 * @brief Reusable visual sidebar shell for top-level pages.
 */

class GfxRenderer;

namespace navigation {

/** Reusable inx-pro-style sidebar renderer and hit-test geometry. */
class Sidebar final {
 public:
  static constexpr int shortcutCount = 5;
  static int width(const GfxRenderer& renderer);
  static void render(const GfxRenderer& renderer, const char* title = "Shortcuts", int selected = -1);
  static void renderLibrary(const GfxRenderer& renderer, bool allBooksMode, int selected = -1);
  static int hitTest(const GfxRenderer& renderer, int tapX, int tapY, size_t count);
};

}  // namespace navigation
