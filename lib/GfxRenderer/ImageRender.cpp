/**
 * @file ImageRender.cpp
 * @brief Definitions for ImageRender.
 */

#include "ImageRender.h"

#include "Bitmap.h"
#include "GfxRenderer.h"
#include "ImageDisplayCache.h"
#include "JpegRender.h"
#include "PngRender.h"
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include "../../src/util/StringUtils.h"

ImageRender ImageRender::create(GfxRenderer& renderer, const std::string& path) {
  return ImageRender(renderer, path, detectFormat(path));
}

ImageRender::Format ImageRender::detectFormat(const std::string& path) {
  if (StringUtils::checkFileExtension(path, ".jpg") || StringUtils::checkFileExtension(path, ".jpeg")) {
    return Format::Jpeg;
  }
  if (StringUtils::checkFileExtension(path, ".png")) {
    return Format::Png;
  }
  return Format::Bitmap;
}

bool ImageRender::getDimensions(const std::string& path, int* outW, int* outH) {
  return getDimensionsForFormat(path, detectFormat(path), outW, outH);
}

bool ImageRender::getDimensions(int* outW, int* outH) const {
  return getDimensionsForFormat(path_, format_, outW, outH);
}

bool ImageRender::getDimensionsForFormat(const std::string& path, Format format, int* outW, int* outH) {
  if (format == Format::Jpeg) {
    return JpegRender::getDimensions(path, outW, outH);
  }
  if (format == Format::Png) {
    return PngRender::getDimensions(path, outW, outH);
  }

  FsFile file;
  if (!SdMan.openFileForRead("EHP", path, file)) {
    return false;
  }

  Bitmap bitmap(file, BitmapDitherMode::None);
  const bool ok = bitmap.parseHeaders() == BmpReaderError::Ok;
  if (ok) {
    *outW = bitmap.getWidth();
    *outH = bitmap.getHeight();
  }
  file.close();
  return ok;
}

bool ImageRender::render(int x, int y, int width, int height, const Options& options) const {
  ImageDisplayCacheOptions cacheOptions;
  cacheOptions.cropToFill = options.cropToFill;
  cacheOptions.bitmapDitherMode = options.bitmapDitherMode;
  cacheOptions.roundedOutside = options.roundedOutside;
  cacheOptions.bitmapGrayStyle = static_cast<uint8_t>(renderer_.getBitmapGrayRenderStyle());
  if (options.useDisplayCache &&
      ImageDisplayCache::renderIfAvailable(renderer_, path_, x, y, width, height, cacheOptions)) {
    return true;
  }

  bool ok = false;
  if (format_ == Format::Jpeg) {
    JpegRender jpeg(renderer_);
    ok = jpeg.fromPath(path_, x, y, width, height, options.cropToFill);
  } else if (format_ == Format::Png) {
    PngRender png(renderer_);
    ok = png.fromPath(path_, x, y, width, height, options.cropToFill);
  } else {
    FsFile file;
    if (!SdMan.openFileForRead("EHP", path_, file)) {
      Serial.printf("[PAGEIMG] Failed to open image file: %s\n", path_.c_str());
      return false;
    }

    Bitmap bitmap(file, options.bitmapDitherMode);
    ok = bitmap.parseHeaders() == BmpReaderError::Ok;
    if (ok) {
      float cropX = 0.f;
      float cropY = 0.f;
      if (options.cropToFill && bitmap.getWidth() > 0 && bitmap.getHeight() > 0 && width > 0 && height > 0) {
        const float imageRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float targetRatio = static_cast<float>(width) / static_cast<float>(height);
        if (imageRatio > targetRatio) {
          cropX = 1.0f - (targetRatio / imageRatio);
        } else {
          cropY = 1.0f - (imageRatio / targetRatio);
        }
      }
      renderer_.bitmap.render(bitmap, x, y, width, height, cropX, cropY, options.roundedOutside);
    }
    file.close();
  }

  if (ok && options.useDisplayCache) {
    ImageDisplayCache::store(renderer_, path_, x, y, width, height, cacheOptions);
  }
  return ok;
}

bool ImageRender::render(int x, int y, int width, int height) const {
  return render(x, y, width, height, Options());
}

bool ImageRender::render(int x, int y, int width, int height, BitmapDitherMode imageDitherMode) const {
  Options options;
  options.bitmapDitherMode = imageDitherMode;
  return render(x, y, width, height, options);
}
