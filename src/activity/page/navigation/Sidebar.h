#pragma once

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
};

}  // namespace navigation
