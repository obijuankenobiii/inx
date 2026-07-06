#pragma once

/**
 * @file ImageRender.h
 * @brief Factory-style dispatch for rendering cached page images.
 */

#include <Bitmap.h>
#include <BitmapRender.h>
#include <ImageRenderMode.h>

#include <functional>
#include <string>

class GfxRenderer;

class ImageRender {
 public:
  struct Options {
    ImageRenderMode mode = ImageRenderMode::OneBit;
    bool cropToFill = false;
    BitmapRender::RoundedOutside roundedOutside = BitmapRender::RoundedOutside::None;
    bool useDisplayCache = true;
    bool quality = false;
    bool fastQuality = false;
  };

  static ImageRender create(GfxRenderer& renderer, const std::string& path);
  static bool getDimensions(const std::string& path, int* outW, int* outH);

  bool getDimensions(int* outW, int* outH) const;
  bool render(int x, int y, int width, int height) const;
  bool render(int x, int y, int width, int height, const Options& options) const;
  bool render(int x, int y, int width, int height, ImageRenderMode mode) const;
  bool displayCachedTwoBit(int x, int y, int width, int height, const Options& options, bool quality = false) const;
  // Full-screen 2-bit grayscale display in ONE call: serves from the display cache if present, otherwise
  // renders both planes (storing them) and drives the gray refresh, then resets BW mode + a clean baseline.
  // `quality` selects the quality LUT (GRAY2) vs the fast LUT (GRAYSCALE). Used by the sleep screen.
  bool displayGrayscale(int x, int y, int width, int height, const Options& options, bool quality) const;

  // General 2-bit grayscale display: runs both planes via `drawPlane` (which populates the framebuffer for the
  // current plane), drives the gray refresh, and resets to BW. This is the single entry point shared by the
  // book reader (text-preserving: preserveText=true, drawPlane rebuilds inverted text + image overlay) and any
  // other custom grayscale composite.
  static void displayGrayscale(GfxRenderer& renderer, bool quality, bool preserveText,
                               const std::function<void()>& drawPlane, bool fastQuality = false);

  // Writes out any quality (GRAY2) image level-cache entry captured during the two-pass render whose
  // disk cache write was deferred past the physical refresh (see the in-memory level cache in
  // ImageRender.cpp). Called once from GfxRenderer::renderGrayscalePasses() right after the display
  // flash, so the SD write never sits on the visible critical path.
  static void flushPendingDiskCache(GfxRenderer& renderer);

 private:
  enum class Format { Bitmap, Jpeg, Png };

  ImageRender(GfxRenderer& renderer, const std::string& path, Format format)
      : renderer_(renderer), path_(path), format_(format) {}

  static Format detectFormat(const std::string& path);
  static bool getDimensionsForFormat(const std::string& path, Format format, int* outW, int* outH);

  GfxRenderer& renderer_;
  const std::string& path_;
  Format format_;
};
