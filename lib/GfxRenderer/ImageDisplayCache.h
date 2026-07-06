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
};

class ImageDisplayCache {
 public:
  static bool renderIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width,
                                int height, const ImageDisplayCacheOptions& options);
  static bool displayTwoBitIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width,
                                       int height, const ImageDisplayCacheOptions& options, bool quality = false,
                                       bool fastQuality = false);
  static bool store(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                    const ImageDisplayCacheOptions& options);

  // Writes both the GRAY2_LSB and GRAY2_MSB cache files directly from a decoded level buffer (see
  // ImageLevelCapture in BitmapUtil.h) instead of reading the live framebuffer. Used to defer a quality
  // image's disk cache write until after the physical refresh, once the framebuffer no longer holds
  // this image's pixels. `levels` must be sized exactly width*height (i.e. the decoded image exactly
  // filled the requested box, no letterboxing) and quality is always true for this path.
  static bool storeFromLevels(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                              bool cropToFill, const uint8_t* levels, bool deviceIsX3);

 private:
  static bool exists(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                     const ImageDisplayCacheOptions& options);
  static std::string pathFor(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                             const ImageDisplayCacheOptions& options);
};
