#pragma once

/**
 * @file JpegRender.h
 * @brief Direct JPEG rendering helpers for page images.
 */

#include <string>

#include "ImageRenderMode.h"

#ifdef SIMULATOR
#include <SDCardManager.h>
#else
class FsFile;
#endif
class GfxRenderer;

class JpegRender {
 public:
  explicit JpegRender(GfxRenderer& renderer) : renderer_(renderer) {}

  bool render(FsFile& jpegFile, int x, int y, int targetWidth, int targetHeight, bool cropToFill = false,
              ImageRenderMode mode = ImageRenderMode::OneBit, bool quality = false) const;
  bool fromPath(const std::string& path, int x, int y, int targetWidth, int targetHeight,
                bool cropToFill = false, ImageRenderMode mode = ImageRenderMode::OneBit, bool quality = false) const;

  static bool getDimensions(FsFile& jpegFile, int* outW, int* outH);
  static bool getDimensions(const std::string& path, int* outW, int* outH);

 private:
  GfxRenderer& renderer_;
};
