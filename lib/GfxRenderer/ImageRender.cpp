/**
 * @file ImageRender.cpp
 * @brief Definitions for ImageRender.
 */

#include "ImageRender.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <memory>

#include "../../src/util/StringUtils.h"
#include "Bitmap.h"
#include "BitmapUtil.h"
#include "GfxRenderer.h"
#include "ImageDisplayCache.h"
#include "JpegRender.h"
#include "PngRender.h"

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
  return render(x, y, width, height, options, nullptr);
}

bool ImageRender::renderDisplayCacheOnly(int x, int y, int width, int height, const Options& options) const {
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
  if (!canUseDisplayCache) {
    return false;
  }
  return ImageDisplayCache::renderIfAvailable(renderer_, path_, x, y, width, height, cacheOptions);
}

bool ImageRender::render(int x, int y, int width, int height, const Options& options,
                         JpegLevelCapture* jpegCapture) const {
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
  // Skip the on-disk raster cache lookup only when replaying a capture (nothing to look up for - we
  // already have the pixels in memory). On the first (capture) call, jpegCapture->captured is still
  // false here, so a cache hit is still preferred over decoding at all.
  if (canUseDisplayCache && !(jpegCapture && jpegCapture->captured)) {
    const bool cacheHit = ImageDisplayCache::renderIfAvailable(renderer_, path_, x, y, width, height, cacheOptions);
    if (cacheHit) {
      return true;
    }
  }

  bool ok = false;
  if (format_ == Format::Jpeg) {
    JpegRender jpeg(renderer_);
    if (jpegCapture && jpegCapture->captured) {
      jpeg.replayCapture(*jpegCapture, options.mode);
      ok = true;
    } else {
      ok = jpeg.fromPath(path_, x, y, width, height, options.cropToFill, options.mode, options.quality, jpegCapture);
    }
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
  return ok;
}

bool ImageRender::displayCachedTwoBit(int x, int y, int width, int height, const Options& options,
                                      const bool quality) const {
  if (!options.useDisplayCache) {
    return false;
  }
  const bool effectiveQuality = quality && format_ != Format::Png;
  ImageDisplayCacheOptions cacheOptions;
  cacheOptions.cropToFill = options.cropToFill;
  cacheOptions.mode = ImageRenderMode::TwoBit;
  cacheOptions.roundedOutside = options.roundedOutside;
  cacheOptions.quality = effectiveQuality;
  const bool hit = ImageDisplayCache::displayTwoBitIfAvailable(renderer_, path_, x, y, width, height, cacheOptions,
                                                               effectiveQuality, options.fastQuality);
  return hit;
}

bool ImageRender::hasCachedTwoBit(int x, int y, int width, int height, const Options& options,
                                  const bool quality) const {
  if (!options.useDisplayCache) {
    return false;
  }
  const bool effectiveQuality = quality && format_ != Format::Png;
  ImageDisplayCacheOptions cacheOptions;
  cacheOptions.cropToFill = options.cropToFill;
  cacheOptions.mode = ImageRenderMode::TwoBit;
  cacheOptions.roundedOutside = options.roundedOutside;
  cacheOptions.quality = effectiveQuality;
  return ImageDisplayCache::hasCachedTwoBit(renderer_, path_, x, y, width, height, cacheOptions, effectiveQuality);
}

bool ImageRender::displayGrayscale(int x, int y, int width, int height, const Options& options,
                                   const bool quality) const {
  const bool effectiveQuality = quality && format_ != Format::Png;
  Options opt = options;
  opt.mode = ImageRenderMode::TwoBit;
  opt.quality = effectiveQuality;
  opt.useDisplayCache = true;

  if (displayCachedTwoBit(x, y, width, height, opt, effectiveQuality)) {
    return true;  // served from cache (handles both planes + refresh + cleanup)
  }

  // JPEGs decode via a slow SD read + DCT pass; renderGrayscalePasses below calls its drawPlane lambda
  // once per plane (LSB, then MSB), and a plain render() re-decodes the whole file each time. Capture the
  // first pass's per-pixel dither level and replay it for the second pass instead - same visual result,
  // no second decode. The capture is packed 2 bits/pixel (4 pixels/byte - see JpegLevelCapture), so this
  // covers a full screen-sized image in a bounded, freed-before-return buffer; oversized images just fall
  // through to the normal double-decode path below.
  constexpr size_t kMaxCapturePixels = 400000;
  const size_t capturePixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
  if (format_ == Format::Jpeg && capturePixelCount > 0 && capturePixelCount <= kMaxCapturePixels) {
    const size_t neededBytes = (capturePixelCount + 3) / 4;
    std::unique_ptr<uint8_t[]> captureBuffer(new (std::nothrow) uint8_t[neededBytes]);
    if (captureBuffer) {
      JpegLevelCapture capture;
      capture.values = captureBuffer.get();
      capture.capacity = neededBytes;

      renderer_.renderGrayscalePasses(
          effectiveQuality, /*preserveText=*/false,
          [&] {
            renderer_.clearScreen(effectiveQuality ? 0xFF : 0x00);
            render(x, y, width, height, opt, &capture);
          },
          opt.fastQuality);
      return true;
    }
  }

  renderer_.renderGrayscalePasses(
      effectiveQuality, /*preserveText=*/false,
      [&] {
        renderer_.clearScreen(effectiveQuality ? 0xFF : 0x00);
        render(x, y, width, height, opt);  // renders into the current plane's render mode AND stores to cache
      },
      opt.fastQuality);
  return true;
}

void ImageRender::displayGrayscale(GfxRenderer& renderer, const bool quality, const bool preserveText,
                                   const std::function<void()>& drawPlane, const bool fastQuality) {
  renderer.renderGrayscalePasses(quality, preserveText, drawPlane, fastQuality);
}

bool ImageRender::render(int x, int y, int width, int height) const { return render(x, y, width, height, Options()); }

bool ImageRender::render(int x, int y, int width, int height, ImageRenderMode mode) const {
  Options options;
  options.mode = mode;
  return render(x, y, width, height, options);
}
