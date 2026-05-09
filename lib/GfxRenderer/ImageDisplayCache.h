#pragma once

/**
 * @file ImageDisplayCache.h
 * @brief Raw display-pixel cache for rendered image rectangles.
 */

#include <cstdint>
#include <string>

#include "BitmapRender.h"

class GfxRenderer;

struct ImageDisplayCacheOptions {
  bool cropToFill = false;
  BitmapDitherMode bitmapDitherMode = BitmapDitherMode::None;
  BitmapRender::RoundedOutside roundedOutside = BitmapRender::RoundedOutside::None;
  uint8_t bitmapGrayStyle = 0;
};

class ImageDisplayCache {
 public:
  static bool renderIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width,
                                int height, const ImageDisplayCacheOptions& options);
  static bool store(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                    const ImageDisplayCacheOptions& options);

 private:
  static std::string pathFor(const std::string& sourcePath, int width, int height,
                             const ImageDisplayCacheOptions& options);
};
