/**
 * @file ImageRender.cpp
 * @brief Definitions for ImageRender.
 */

#include "ImageRender.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <vector>

#include "../../src/util/StringUtils.h"
#include "Bitmap.h"
#include "BitmapUtil.h"
#include "GfxRenderer.h"
#include "ImageDisplayCache.h"
#include "JpegRender.h"
#include "PngRender.h"

namespace {
// Caps how large an image's level buffer (1 byte/pixel) can be before we skip the decode-once
// optimization and fall back to the old double-decode/sync-store behavior. ~120KB is comfortably
// affordable against this device's free heap for the lifetime of a single page render.
constexpr size_t kMaxCachedLevelBytes = 120 * 1024;

// In-memory cache of one quality (GRAY2) image's decoded levels, alive only for the duration of the
// current two-pass render cycle. Single slot: a second image on the same page safely misses (falls
// back to a normal decode for it) rather than risking any cross-image aliasing.
struct QualityLevelCache {
  std::string path;
  int x = 0, y = 0, width = 0, height = 0;
  bool cropToFill = false;
  std::vector<uint8_t> levelStorage;
  ImageLevelCapture capture;
  bool keyValid = false;
  bool diskFlushPending = false;

  bool matches(const std::string& p, const int rx, const int ry, const int rw, const int rh, const bool crop) const {
    return keyValid && x == rx && y == ry && width == rw && height == rh && cropToFill == crop && path == p;
  }

  // The old framebuffer-based disk cache captures the whole requested box (including any letterbox
  // background around a non-cropToFill image whose aspect ratio doesn't exactly match the box). The
  // level buffer only holds the pixels the decoder actually drew, so deferring the disk write to
  // flushPendingDiskCache() is only correct when the decoded image exactly fills the box with no
  // letterboxing - otherwise fall back to the old immediate framebuffer-based store.
  bool exactlyFillsBox(const int rx, const int ry, const int rw, const int rh) const {
    return capture.captured && capture.drawOffsetX == rx && capture.drawOffsetY == ry && capture.outWidth == rw &&
           capture.outHeight == rh;
  }

  void beginFor(const std::string& p, const int rx, const int ry, const int rw, const int rh, const bool crop) {
    path = p;
    x = rx;
    y = ry;
    width = rw;
    height = rh;
    cropToFill = crop;
    keyValid = true;
    diskFlushPending = false;
    capture = ImageLevelCapture{};
    const size_t need = static_cast<size_t>(rw) * static_cast<size_t>(rh);
    if (need == 0 || need > kMaxCachedLevelBytes) {
      levelStorage.clear();
      return;
    }
    levelStorage.assign(need, 0);
    capture.levels = levelStorage.data();
    capture.capacity = static_cast<int>(need);
  }
};

QualityLevelCache g_qualityLevelCache;
}  // namespace

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
  if (canUseDisplayCache) {
    const bool cacheHit = ImageDisplayCache::renderIfAvailable(renderer_, path_, x, y, width, height, cacheOptions);
    if (cacheHit) {
      return true;
    }
  }

  // Quality (GRAY2) images are drawn twice, once per bit-plane (see GfxRenderer::renderGrayscalePasses).
  // Decoding a JPEG/PNG on this ESP32 is expensive and plane-independent, so cache the per-pixel
  // quantized levels from the first pass and replay them for the second instead of re-decoding.
  // Rounded-corner masking isn't reflected in the level buffer, so those images skip this path.
  const bool qualityCacheEligible = options.quality && options.mode == ImageRenderMode::TwoBit &&
                                    options.roundedOutside == BitmapRender::RoundedOutside::None &&
                                    (format_ == Format::Jpeg || format_ == Format::Png);
  ImageLevelCapture* capture = nullptr;
  if (qualityCacheEligible) {
    if (g_qualityLevelCache.matches(path_, x, y, width, height, options.cropToFill) &&
        g_qualityLevelCache.capture.captured) {
      Serial.printf("[%lu] [IMGQ] level-cache hit, skipping decode: %s\n", millis(), path_.c_str());
      replayImageLevelCapture(g_qualityLevelCache.capture, renderer_, renderer_.deviceIsX3());
      if (canUseDisplayCache) {
        if (g_qualityLevelCache.exactlyFillsBox(x, y, width, height)) {
          g_qualityLevelCache.diskFlushPending = true;
        } else {
          ImageDisplayCache::store(renderer_, path_, x, y, width, height, cacheOptions);
        }
      }
      return true;
    }
    g_qualityLevelCache.beginFor(path_, x, y, width, height, options.cropToFill);
    capture = &g_qualityLevelCache.capture;
  }

  bool ok = false;
  if (format_ == Format::Jpeg) {
    JpegRender jpeg(renderer_);
    ok = jpeg.fromPath(path_, x, y, width, height, options.cropToFill, options.mode, options.quality, capture);
  } else if (format_ == Format::Png) {
    PngRender png(renderer_);
    ok = png.fromPath(path_, x, y, width, height, options.cropToFill, options.mode, capture);
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
    if (qualityCacheEligible && g_qualityLevelCache.exactlyFillsBox(x, y, width, height)) {
      // Defer the SD write to flushPendingDiskCache(), called after the physical refresh so it never
      // sits on the visible critical path.
      g_qualityLevelCache.diskFlushPending = true;
    } else {
      ImageDisplayCache::store(renderer_, path_, x, y, width, height, cacheOptions);
    }
  }
  return ok;
}

bool ImageRender::displayCachedTwoBit(int x, int y, int width, int height, const Options& options,
                                      const bool quality) const {
  if (!options.useDisplayCache) {
    return false;
  }
  ImageDisplayCacheOptions cacheOptions;
  cacheOptions.cropToFill = options.cropToFill;
  cacheOptions.mode = ImageRenderMode::TwoBit;
  cacheOptions.roundedOutside = options.roundedOutside;
  cacheOptions.quality = quality;
  const bool hit = ImageDisplayCache::displayTwoBitIfAvailable(renderer_, path_, x, y, width, height, cacheOptions,
                                                               quality, options.fastQuality);
  return hit;
}

bool ImageRender::displayGrayscale(int x, int y, int width, int height, const Options& options,
                                   const bool quality) const {
  Options opt = options;
  opt.mode = ImageRenderMode::TwoBit;
  opt.quality = quality;
  opt.useDisplayCache = true;

  if (displayCachedTwoBit(x, y, width, height, opt, quality)) {
    return true;  // served from cache (handles both planes + refresh + cleanup)
  }

  renderer_.renderGrayscalePasses(
      quality, /*preserveText=*/false,
      [&] {
        renderer_.clearScreen(quality ? 0xFF : 0x00);
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

void ImageRender::flushPendingDiskCache(GfxRenderer& renderer) {
  if (!g_qualityLevelCache.diskFlushPending || !g_qualityLevelCache.capture.captured) {
    g_qualityLevelCache.diskFlushPending = false;
    return;
  }
  const bool stored = ImageDisplayCache::storeFromLevels(renderer, g_qualityLevelCache.path, g_qualityLevelCache.x,
                                                         g_qualityLevelCache.y, g_qualityLevelCache.width,
                                                         g_qualityLevelCache.height, g_qualityLevelCache.cropToFill,
                                                         g_qualityLevelCache.capture.levels, renderer.deviceIsX3());
  Serial.printf("[%lu] [IMGQ] deferred disk cache flush %s: %s\n", millis(), stored ? "ok" : "failed",
                g_qualityLevelCache.path.c_str());
  g_qualityLevelCache.diskFlushPending = false;
}
