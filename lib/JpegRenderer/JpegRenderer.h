#pragma once

/**
 * @file JpegRenderer.h
 * @brief Direct JPEG rendering helpers for page images.
 */

#include <string>

class FsFile;
class GfxRenderer;

class JpegRenderer {
 public:
  explicit JpegRenderer(GfxRenderer& renderer) : renderer_(renderer) {}

  bool drawJpeg(FsFile& jpegFile, int x, int y, int targetWidth, int targetHeight, bool cropToFill = false) const;
  bool drawJpegFromPath(const std::string& path, int x, int y, int targetWidth, int targetHeight,
                        bool cropToFill = false) const;

  static bool getDimensions(FsFile& jpegFile, int* outW, int* outH);
  static bool getDimensions(const std::string& path, int* outW, int* outH);

 private:
  GfxRenderer& renderer_;
};

