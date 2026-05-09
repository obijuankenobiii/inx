#pragma once

/**
 * @file ImageRender.h
 * @brief Factory-style dispatch for rendering cached page images.
 */

#include <Bitmap.h>
#include <BitmapRender.h>
#include <ImageRenderMode.h>

#include <string>

class GfxRenderer;

class ImageRender {
 public:
  struct Options {
    ImageRenderMode mode = ImageRenderMode::OneBit;
    bool cropToFill = false;
    BitmapRender::RoundedOutside roundedOutside = BitmapRender::RoundedOutside::None;
    bool useDisplayCache = true;
  };

  static ImageRender create(GfxRenderer& renderer, const std::string& path);
  static bool getDimensions(const std::string& path, int* outW, int* outH);

  bool getDimensions(int* outW, int* outH) const;
  bool render(int x, int y, int width, int height) const;
  bool render(int x, int y, int width, int height, const Options& options) const;
  bool render(int x, int y, int width, int height, ImageRenderMode mode) const;

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
