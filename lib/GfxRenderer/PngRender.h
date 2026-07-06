#pragma once

/**
 * @file PngRender.h
 * @brief Direct PNG rendering helpers for page images.
 */

#include <string>

#include "BitmapUtil.h"
#include "ImageRenderMode.h"

#ifdef SIMULATOR
#include <SDCardManager.h>
#else
class FsFile;
#endif
class GfxRenderer;

class PngRender {
 public:
  explicit PngRender(GfxRenderer& renderer) : renderer_(renderer) {}

  // `capture`, when non-null, is populated with per-pixel quantized levels for a TwoBit decode (see
  // ImageLevelCapture in BitmapUtil.h) so a second call for the opposite GRAY2 plane can skip decoding.
  bool render(FsFile& pngFile, int x, int y, int targetWidth, int targetHeight, bool cropToFill = false,
              ImageRenderMode mode = ImageRenderMode::OneBit, ImageLevelCapture* capture = nullptr) const;
  bool fromPath(const std::string& path, int x, int y, int targetWidth, int targetHeight, bool cropToFill = false,
                ImageRenderMode mode = ImageRenderMode::OneBit, ImageLevelCapture* capture = nullptr) const;

  static bool getDimensions(FsFile& pngFile, int* outW, int* outH);
  static bool getDimensions(const std::string& path, int* outW, int* outH);

 private:
  GfxRenderer& renderer_;
};
