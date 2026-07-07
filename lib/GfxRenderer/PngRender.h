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

  bool render(FsFile& pngFile, int x, int y, int targetWidth, int targetHeight, bool cropToFill = false,
              ImageRenderMode mode = ImageRenderMode::OneBit) const;
  bool fromPath(const std::string& path, int x, int y, int targetWidth, int targetHeight, bool cropToFill = false,
                ImageRenderMode mode = ImageRenderMode::OneBit) const;

  static bool getDimensions(FsFile& pngFile, int* outW, int* outH);
  static bool getDimensions(const std::string& path, int* outW, int* outH);

 private:
  GfxRenderer& renderer_;
};
