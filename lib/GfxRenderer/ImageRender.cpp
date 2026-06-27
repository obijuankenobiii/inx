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

  Bitmap bitmap(file);
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
  cacheOptions.mode = options.mode;
  cacheOptions.renderPlane = static_cast<uint8_t>(renderer_.getRenderMode());
  cacheOptions.roundedOutside = options.roundedOutside;
  cacheOptions.quality = options.quality;
  const bool canUseDisplayCache =
      options.useDisplayCache &&
      ((options.mode == ImageRenderMode::OneBit && renderer_.getRenderMode() == GfxRenderer::BW) ||
       options.mode == ImageRenderMode::TwoBit);
  if (options.quality && options.mode == ImageRenderMode::TwoBit) {
    Serial.printf("[%lu] [IMG-Q] render begin src=%s rect=%d,%d %dx%d plane=%d cache=%d format=%d\n", millis(),
                  path_.c_str(), x, y, width, height, static_cast<int>(renderer_.getRenderMode()),
                  canUseDisplayCache ? 1 : 0, static_cast<int>(format_));
  }
  if (canUseDisplayCache) {
    const bool cacheHit = ImageDisplayCache::renderIfAvailable(renderer_, path_, x, y, width, height, cacheOptions);
    if (options.quality && options.mode == ImageRenderMode::TwoBit) {
      Serial.printf("[%lu] [IMG-Q] render cache %s src=%s plane=%d\n", millis(), cacheHit ? "hit" : "miss",
                    path_.c_str(), static_cast<int>(renderer_.getRenderMode()));
    }
    if (cacheHit) {
      return true;
    }
  }

  bool ok = false;
  if (format_ == Format::Jpeg) {
    JpegRender jpeg(renderer_);
    ok = jpeg.fromPath(path_, x, y, width, height, options.cropToFill, options.mode, options.quality);
  } else if (format_ == Format::Png) {
    PngRender png(renderer_);
    ok = png.fromPath(path_, x, y, width, height, options.cropToFill, options.mode);
  } else {
    FsFile file;
    if (!SdMan.openFileForRead("EHP", path_, file)) {
      Serial.printf("[PAGEIMG] Failed to open image file: %s\n", path_.c_str());
      return false;
    }

    Bitmap bitmap(file);
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
      renderer_.bitmap.render(bitmap, x, y, width, height, cropX, cropY, options.roundedOutside, options.mode);
    }
    file.close();
  }

  if (ok && options.roundedOutside != BitmapRender::RoundedOutside::None) {
    renderer_.bitmap.maskRoundedOutside(x, y, width, height, options.roundedOutside);
  }

  if (ok && canUseDisplayCache) {
    ImageDisplayCache::store(renderer_, path_, x, y, width, height, cacheOptions);
  }
  if (options.quality && options.mode == ImageRenderMode::TwoBit) {
    Serial.printf("[%lu] [IMG-Q] render end ok=%d src=%s plane=%d\n", millis(), ok ? 1 : 0, path_.c_str(),
                  static_cast<int>(renderer_.getRenderMode()));
  }
  return ok;
}

bool ImageRender::displayCachedTwoBit(int x, int y, int width, int height, const Options& options,
                                      const bool quality) const {
  if (!options.useDisplayCache) {
    if (quality) {
      Serial.printf("[%lu] [IMG-Q] display cache disabled src=%s\n", millis(), path_.c_str());
    }
    return false;
  }
  ImageDisplayCacheOptions cacheOptions;
  cacheOptions.cropToFill = options.cropToFill;
  cacheOptions.mode = ImageRenderMode::TwoBit;
  cacheOptions.roundedOutside = options.roundedOutside;
  cacheOptions.quality = quality;
  const bool hit =
      ImageDisplayCache::displayTwoBitIfAvailable(renderer_, path_, x, y, width, height, cacheOptions, quality,
                                                  options.fastQuality);
  if (quality) {
    Serial.printf("[%lu] [IMG-Q] display cache %s src=%s rect=%d,%d %dx%d\n", millis(), hit ? "hit" : "miss",
                  path_.c_str(), x, y, width, height);
  }
  return hit;
}

bool ImageRender::displayGrayscale(int x, int y, int width, int height, const Options& options,
                                   const bool quality) const {
  Options opt = options;
  opt.mode = ImageRenderMode::TwoBit;
  opt.quality = quality;
  opt.useDisplayCache = true;

  if (quality) {
    Serial.printf("[%lu] [IMG-Q] display grayscale begin src=%s rect=%d,%d %dx%d fast=%d\n", millis(), path_.c_str(),
                  x, y, width, height, opt.fastQuality ? 1 : 0);
  }
  if (displayCachedTwoBit(x, y, width, height, opt, quality)) {
    if (quality) {
      Serial.printf("[%lu] [IMG-Q] display grayscale served from cache src=%s\n", millis(), path_.c_str());
    }
    return true;  // served from cache (handles both planes + refresh + cleanup)
  }

  renderer_.renderGrayscalePasses(quality, /*preserveText=*/false, [&] {
    renderer_.clearScreen(quality ? 0xFF : 0x00);
    render(x, y, width, height, opt);  // renders into the current plane's render mode AND stores to cache
  }, opt.fastQuality);
  if (quality) {
    Serial.printf("[%lu] [IMG-Q] display grayscale live render complete src=%s\n", millis(), path_.c_str());
  }
  return true;
}

void ImageRender::displayGrayscale(GfxRenderer& renderer, const bool quality, const bool preserveText,
                                   const std::function<void()>& drawPlane, const bool fastQuality) {
  if (quality) {
    Serial.printf("[%lu] [IMG-Q] composite grayscale begin preserveText=%d fast=%d\n", millis(), preserveText ? 1 : 0,
                  fastQuality ? 1 : 0);
  }
  renderer.renderGrayscalePasses(quality, preserveText, drawPlane, fastQuality);
  if (quality) {
    Serial.printf("[%lu] [IMG-Q] composite grayscale end\n", millis());
  }
}

bool ImageRender::render(int x, int y, int width, int height) const {
  return render(x, y, width, height, Options());
}

bool ImageRender::render(int x, int y, int width, int height, ImageRenderMode mode) const {
  Options options;
  options.mode = mode;
  return render(x, y, width, height, options);
}
