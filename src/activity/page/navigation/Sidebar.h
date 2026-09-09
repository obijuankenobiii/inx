#pragma once

#include <cstddef>

/**
 * @file Sidebar.h
 * @brief Reusable visual sidebar shell for top-level pages.
 */

class GfxRenderer;

namespace navigation {

/** Draws the inx-pro-style sidebar. Item actions are intentionally not wired yet. */
class Sidebar final {
 public:
  static void render(const GfxRenderer& renderer, const char* title = "Shortcuts");
  static void renderLibrary(const GfxRenderer& renderer, bool allBooksMode, int selected = -1);
  static int hitTest(const GfxRenderer& renderer, int tapX, int tapY, size_t count);
};

}  // namespace navigation
