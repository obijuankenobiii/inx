#pragma once

/**
 * @file JpegRender.h
 * @brief Direct JPEG rendering helpers for page images.
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

// Captures the per-pixel dither level (0-3) of a single JPEG decode so a second render pass (e.g. the
// MSB plane of a grayscale image) can redraw from memory instead of decoding the file again. Packed 2
// bits/pixel (4 pixels per byte in `values`) rather than one byte per pixel, so a full-page image is a
// ~86KB capture instead of ~345KB - the difference between "never fits any safe cap" and "fits a bounded
// one". Caller owns `values` and is responsible for sizing/freeing it; `capacity` (in BYTES, i.e. already
// accounting for the 4x packing) is a safety bound `render()` checks before writing, so oversized images
// simply aren't captured (render() still succeeds, just uncached).
struct JpegLevelCapture {
  uint8_t* values = nullptr;
  size_t capacity = 0;
  int width = 0;
  int height = 0;
  int drawOffsetX = 0;
  int drawOffsetY = 0;
  bool captured = false;
};

class JpegRender {
 public:
  explicit JpegRender(GfxRenderer& renderer) : renderer_(renderer) {}

  bool render(FsFile& jpegFile, int x, int y, int targetWidth, int targetHeight, bool cropToFill = false,
              ImageRenderMode mode = ImageRenderMode::OneBit, bool quality = false,
              JpegLevelCapture* capture = nullptr) const;
  bool fromPath(const std::string& path, int x, int y, int targetWidth, int targetHeight, bool cropToFill = false,
                ImageRenderMode mode = ImageRenderMode::OneBit, bool quality = false,
                JpegLevelCapture* capture = nullptr) const;

  // Redraws a capture made by an earlier render() call for a different plane, without touching the file.
  void replayCapture(const JpegLevelCapture& capture, ImageRenderMode mode) const;

  static bool getDimensions(FsFile& jpegFile, int* outW, int* outH);
  static bool getDimensions(const std::string& path, int* outW, int* outH);

 private:
  GfxRenderer& renderer_;
};
