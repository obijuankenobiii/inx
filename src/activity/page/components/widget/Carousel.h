#pragma once

/**
 * @file Carousel.h
 * @brief Reusable recent-book carousel layout used by the legacy Recent page.
 */

#include <vector>

#include "state/RecentBooks.h"
#include "system/UiLayout.h"

class GfxRenderer;

namespace widget {

/**
 * Lays out the centered Flow carousel without owning book or image state.
 *
 * The thumbnail callback deliberately belongs to the caller. RecentActivity
 * owns the display-cache queue, so the widget can be reused without bypassing
 * that device-specific optimization.
 */
class Carousel final {
 public:
  static constexpr int kHeight = UiLayout::FLOW_CAROUSEL_HEIGHT;

  using ThumbnailRenderer = void (*)(void* context, const RecentBook& book, int x, int y, int width, int height,
                                      int placeholderFontId, bool roundedCornerBackdropIsDither);

  explicit Carousel(GfxRenderer& renderer) : renderer_(renderer) {}

  /** Render recent books from the shared recent-book store, as Home widgets do. */
  void render(int index, int x, int y, int width, int height) const;

  void render(const std::vector<RecentBook>& books, int index, int x, int y, int width, int height,
              ThumbnailRenderer thumbnailRenderer, void* context) const;

 private:
  static void renderDefaultThumbnail(void* context, const RecentBook& book, int x, int y, int width, int height,
                                     int placeholderFontId, bool roundedCornerBackdropIsDither);
  GfxRenderer& renderer_;
};

}  // namespace widget
