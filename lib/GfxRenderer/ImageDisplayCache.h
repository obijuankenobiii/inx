#pragma once

/**
 * @file ImageDisplayCache.h
 * @brief Raw display-pixel cache for rendered image rectangles.
 */

#include <cstdint>
#include <string>

#include "BitmapRender.h"
#include "ImageRenderMode.h"

class GfxRenderer;

struct ImageDisplayCacheOptions {
  bool cropToFill = false;
  ImageRenderMode mode = ImageRenderMode::OneBit;
  uint8_t renderPlane = 0;
  BitmapRender::RoundedOutside roundedOutside = BitmapRender::RoundedOutside::None;
  bool quality = false;
  bool qualityFast = false;  // book reader's fast-quality tone profile (distinct cache key from sleep's)
};

class ImageDisplayCache {
 public:
  static bool renderIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width,
                                int height, const ImageDisplayCacheOptions& options);
  static bool displayTwoBitIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width,
                                       int height, const ImageDisplayCacheOptions& options, bool quality = false);
  static bool store(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                    const ImageDisplayCacheOptions& options);

 private:
  static bool exists(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                     const ImageDisplayCacheOptions& options);
  static std::string pathFor(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                             const ImageDisplayCacheOptions& options);
};
