#pragma once

#include <string>

#include "state/RecentBooks.h"

class GfxRenderer;

namespace widget::support {

std::string titleFor(const RecentBook& book);
std::string cachePathFor(const RecentBook& book);

void drawPlaceholder(const GfxRenderer& renderer, const std::string& title, int x, int y, int width, int height,
                     int font);
void drawThumbnail(GfxRenderer& renderer, const RecentBook& book, int x, int y, int width, int height, int font,
                   bool roundedCornerBackdropIsDither = false);
void drawDitherRect(const GfxRenderer& renderer, int x, int y, int width, int height);
void drawMockProgress(const GfxRenderer& renderer, int x, int y, int width, float progress);

}  // namespace widget::support
