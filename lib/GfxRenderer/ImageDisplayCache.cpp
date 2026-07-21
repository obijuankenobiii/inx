/**
 * @file ImageDisplayCache.cpp
 * @brief Definitions for ImageDisplayCache.
 */

#include "ImageDisplayCache.h"

#include <Arduino.h>
#include <HalGPIO.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <memory>

#include "GfxRenderer.h"

namespace {
constexpr uint32_t kMagic = 0x43445249;  // IRDC, little-endian on disk
constexpr uint16_t kVersion = 46;        // bump: discard old PNG high-quality planes
constexpr const char* kCacheDir = "/.system/cache";
constexpr size_t kIoBufferSize = 2048;

struct CacheHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t headerSize;
  uint16_t width;
  uint16_t height;
  uint16_t rowBytes;
  uint16_t reserved;
};

struct VisibleRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int sourceOffsetX = 0;
  int sourceOffsetY = 0;
};

uint32_t fnv1aAdd(uint32_t hash, const uint8_t byte) {
  hash ^= byte;
  return hash * 16777619u;
}

uint32_t fnv1aAddUint32(uint32_t hash, const uint32_t value) {
  hash = fnv1aAdd(hash, static_cast<uint8_t>(value & 0xFF));
  hash = fnv1aAdd(hash, static_cast<uint8_t>((value >> 8) & 0xFF));
  hash = fnv1aAdd(hash, static_cast<uint8_t>((value >> 16) & 0xFF));
  return fnv1aAdd(hash, static_cast<uint8_t>((value >> 24) & 0xFF));
}

uint32_t sourceSize(const std::string& path) {
  FsFile file;
  if (!SdMan.openFileForRead("IDC", path, file)) {
    return 0;
  }
  const uint32_t size = static_cast<uint32_t>(file.size());
  file.close();
  return size;
}

uint32_t cacheHash(const std::string& sourcePath, const int width, const int height, const VisibleRect& visible,
                   const ImageDisplayCacheOptions& options) {
  uint32_t hash = 2166136261u;
  for (const char c : sourcePath) {
    hash = fnv1aAdd(hash, static_cast<uint8_t>(c));
  }
  hash = fnv1aAddUint32(hash, sourceSize(sourcePath));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(width));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(height));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(visible.sourceOffsetX));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(visible.sourceOffsetY));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(visible.width));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(visible.height));
  hash = fnv1aAdd(hash, options.cropToFill ? 1 : 0);
  hash = fnv1aAdd(hash, static_cast<uint8_t>(options.mode));
  hash = fnv1aAdd(hash, options.renderPlane);
  hash = fnv1aAdd(hash, static_cast<uint8_t>(options.roundedOutside));
  hash = fnv1aAdd(hash, options.quality ? 1 : 0);
  hash = fnv1aAdd(hash, gpio.deviceIsX3() ? 1 : 0);
  return hash;
}

bool ensureCacheDir(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return false;
  }
  const std::string dir = path.substr(0, slash);
  if (dir.empty() || dir == "/") {
    return true;
  }

  auto isDirectory = [](const std::string& p) {
    if (!SdMan.exists(p.c_str())) {
      return false;
    }
    FsFile file = SdMan.open(p.c_str());
    const bool ok = file && file.isDirectory();
    file.close();
    return ok;
  };

  if (isDirectory(dir)) {
    return true;
  }

  size_t pos = 1;
  while (pos < dir.length()) {
    const size_t next = dir.find('/', pos);
    const std::string segment = dir.substr(0, next == std::string::npos ? dir.length() : next);
    if (!segment.empty() && segment != "/" && !isDirectory(segment)) {
      if (!SdMan.mkdir(segment.c_str()) && !isDirectory(segment)) {
        Serial.printf("[%lu] [IDC] Failed to create cache dir segment: %s\n", millis(), segment.c_str());
        return false;
      }
    }
    if (next == std::string::npos) {
      break;
    }
    pos = next + 1;
  }

  if (!isDirectory(dir)) {
    Serial.printf("[%lu] [IDC] Cache dir missing after mkdir: %s\n", millis(), dir.c_str());
    return false;
  }
  return true;
}

bool visibleBounds(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                   VisibleRect& out) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int x1 = std::max(0, x);
  const int y1 = std::max(0, y);
  const int x2 = std::min(screenW, x + width);
  const int y2 = std::min(screenH, y + height);
  if (x2 <= x1 || y2 <= y1) {
    return false;
  }
  out.x = x1;
  out.y = y1;
  out.width = x2 - x1;
  out.height = y2 - y1;
  out.sourceOffsetX = x1 - x;
  out.sourceOffsetY = y1 - y;
  return true;
}

const char* planeName(const ImageDisplayCacheOptions& options) {
  switch (static_cast<GfxRenderer::RenderMode>(options.renderPlane)) {
    case GfxRenderer::GRAYSCALE_LSB:
      return "GRAYSCALE_LSB";
    case GfxRenderer::GRAYSCALE_MSB:
      return "GRAYSCALE_MSB";
    case GfxRenderer::GRAY2_LSB:
      return "GRAY2_LSB";
    case GfxRenderer::GRAY2_MSB:
      return "GRAY2_MSB";
    case GfxRenderer::BW:
    default:
      return "BW";
  }
}

}  // namespace

std::string ImageDisplayCache::pathFor(GfxRenderer& renderer, const std::string& sourcePath, const int x, const int y,
                                       const int width, const int height, const ImageDisplayCacheOptions& options) {
  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return "";
  }
  const uint32_t hash = cacheHash(sourcePath, width, height, visible, options);
  char name[48];
  snprintf(name, sizeof(name), "/%02lx/%08lx.irdc", static_cast<unsigned long>((hash >> 24) & 0xFF),
           static_cast<unsigned long>(hash));
  return std::string(kCacheDir) + name;
}

bool ImageDisplayCache::renderIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, const int x,
                                          const int y, const int width, const int height,
                                          const ImageDisplayCacheOptions& options) {
  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return false;
  }

  const std::string cachePath = pathFor(renderer, sourcePath, x, y, width, height, options);
  if (cachePath.empty()) {
    return false;
  }
  if (!SdMan.exists(cachePath.c_str())) {
    return false;
  }
  FsFile file;
  if (!SdMan.openFileForRead("IDC", cachePath, file)) {
    if (options.quality) {
      Serial.printf("[%lu] [IDC-Q] cache open failed plane=%s path=%s\n", millis(), planeName(options),
                    cachePath.c_str());
    }
    return false;
  }

  CacheHeader header;
  const bool headerOk = file.read(&header, sizeof(header)) == sizeof(header) && header.magic == kMagic &&
                        header.version == kVersion && header.headerSize == sizeof(CacheHeader) &&
                        header.width == visible.width && header.height == visible.height &&
                        header.rowBytes == static_cast<uint16_t>((visible.width + 7) / 8);
  if (!headerOk) {
    if (options.quality) {
      Serial.printf(
          "[%lu] [IDC-Q] cache header invalid plane=%s path=%s magic=%08lx ver=%u header=%u wh=%ux%u row=%u "
          "expected=%dx%d/%d\n",
          millis(), planeName(options), cachePath.c_str(), static_cast<unsigned long>(header.magic), header.version,
          header.headerSize, header.width, header.height, header.rowBytes, visible.width, visible.height,
          (visible.width + 7) / 8);
    }
    file.close();
    return false;
  }

  const int rowBytes = header.rowBytes;
  if (rowBytes > static_cast<int>(kIoBufferSize)) {
    file.close();
    return false;
  }
  std::unique_ptr<uint8_t[]> rows(new (std::nothrow) uint8_t[kIoBufferSize]);
  if (!rows) {
    file.close();
    return false;
  }

  const int rowsPerRead = std::max(1, static_cast<int>(kIoBufferSize) / rowBytes);
  for (int rowBase = 0; rowBase < visible.height; rowBase += rowsPerRead) {
    const int rowsThisRead = std::min(rowsPerRead, visible.height - rowBase);
    const int bytesThisRead = rowsThisRead * rowBytes;
    if (file.read(rows.get(), bytesThisRead) != bytesThisRead) {
      if (options.quality) {
        Serial.printf("[%lu] [IDC-Q] cache row read failed plane=%s path=%s row=%d/%d\n", millis(), planeName(options),
                      cachePath.c_str(), rowBase, visible.height);
      }
      file.close();
      return false;
    }
    for (int row = 0; row < rowsThisRead; row++) {
      renderer.drawPackedRow1bpp(visible.x, visible.y + rowBase + row, visible.width, rows.get() + row * rowBytes);
    }
  }

  file.close();
  return true;
}

bool ImageDisplayCache::displayTwoBitIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, const int x,
                                                 const int y, const int width, const int height,
                                                 const ImageDisplayCacheOptions& options, const bool quality,
                                                 const bool fastQuality) {
  ImageDisplayCacheOptions lsbOptions = options;
  lsbOptions.mode = ImageRenderMode::TwoBit;
  lsbOptions.renderPlane = static_cast<uint8_t>(quality ? GfxRenderer::GRAY2_LSB : GfxRenderer::GRAYSCALE_LSB);
  lsbOptions.quality = quality;

  ImageDisplayCacheOptions msbOptions = options;
  msbOptions.mode = ImageRenderMode::TwoBit;
  msbOptions.renderPlane = static_cast<uint8_t>(quality ? GfxRenderer::GRAY2_MSB : GfxRenderer::GRAYSCALE_MSB);
  msbOptions.quality = quality;

  const std::string lsbPath = pathFor(renderer, sourcePath, x, y, width, height, lsbOptions);
  const std::string msbPath = pathFor(renderer, sourcePath, x, y, width, height, msbOptions);
  if (lsbPath.empty() || msbPath.empty() || !SdMan.exists(lsbPath.c_str()) || !SdMan.exists(msbPath.c_str())) {
    return false;
  }

  const bool useFastQuality = quality && fastQuality && !renderer.deviceIsX3();

  if (quality && !renderer.deviceIsX3()) {
    renderer.prepareQualityGrayscale();
  }

  renderer.clearScreen(quality ? 0xFF : 0x00);
  renderer.setRenderMode(quality ? GfxRenderer::GRAY2_LSB : GfxRenderer::GRAYSCALE_LSB);
  if (!renderIfAvailable(renderer, sourcePath, x, y, width, height, lsbOptions)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(quality ? 0xFF : 0x00);
  renderer.setRenderMode(quality ? GfxRenderer::GRAY2_MSB : GfxRenderer::GRAYSCALE_MSB);
  if (!renderIfAvailable(renderer, sourcePath, x, y, width, height, msbOptions)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }
  renderer.copyGrayscaleMsbBuffers();

  if (useFastQuality) {
    renderer.displayGrayBufferFastQuality();
  } else {
    renderer.displayGrayBuffer(quality);
  }
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen(0xFF);
  renderer.cleanupGrayscaleWithFrameBuffer();

  return true;
}

bool ImageDisplayCache::hasCachedTwoBit(GfxRenderer& renderer, const std::string& sourcePath, const int x, const int y,
                                        const int width, const int height, const ImageDisplayCacheOptions& options,
                                        const bool quality) {
  ImageDisplayCacheOptions lsbOptions = options;
  lsbOptions.mode = ImageRenderMode::TwoBit;
  lsbOptions.renderPlane = static_cast<uint8_t>(quality ? GfxRenderer::GRAY2_LSB : GfxRenderer::GRAYSCALE_LSB);
  lsbOptions.quality = quality;

  ImageDisplayCacheOptions msbOptions = options;
  msbOptions.mode = ImageRenderMode::TwoBit;
  msbOptions.renderPlane = static_cast<uint8_t>(quality ? GfxRenderer::GRAY2_MSB : GfxRenderer::GRAYSCALE_MSB);
  msbOptions.quality = quality;

  const std::string lsbPath = pathFor(renderer, sourcePath, x, y, width, height, lsbOptions);
  const std::string msbPath = pathFor(renderer, sourcePath, x, y, width, height, msbOptions);
  return !lsbPath.empty() && !msbPath.empty() && SdMan.exists(lsbPath.c_str()) && SdMan.exists(msbPath.c_str());
}

bool ImageDisplayCache::store(GfxRenderer& renderer, const std::string& sourcePath, const int x, const int y,
                              const int width, const int height, const ImageDisplayCacheOptions& options) {
  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return false;
  }

  const std::string cachePath = pathFor(renderer, sourcePath, x, y, width, height, options);
  if (cachePath.empty()) {
    if (options.quality) {
      Serial.printf("[%lu] [IDC-Q] store path empty plane=%s src=%s rect=%d,%d %dx%d\n", millis(), planeName(options),
                    sourcePath.c_str(), x, y, width, height);
    }
    return false;
  }
  if (!ensureCacheDir(cachePath)) {
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForWrite("IDC", cachePath, file)) {
    if (options.quality) {
      Serial.printf("[%lu] [IDC-Q] store open failed plane=%s path=%s\n", millis(), planeName(options),
                    cachePath.c_str());
    }
    return false;
  }

  const int rowBytes = (visible.width + 7) / 8;
  if (rowBytes > static_cast<int>(kIoBufferSize)) {
    file.close();
    SdMan.remove(cachePath.c_str());
    return false;
  }
  std::unique_ptr<uint8_t[]> rows(new (std::nothrow) uint8_t[kIoBufferSize]);
  if (!rows) {
    file.close();
    SdMan.remove(cachePath.c_str());
    return false;
  }

  const CacheHeader header = {.magic = kMagic,
                              .version = kVersion,
                              .headerSize = sizeof(CacheHeader),
                              .width = static_cast<uint16_t>(visible.width),
                              .height = static_cast<uint16_t>(visible.height),
                              .rowBytes = static_cast<uint16_t>(rowBytes),
                              .reserved = 0};
  if (file.write(&header, sizeof(header)) != sizeof(header)) {
    file.close();
    SdMan.remove(cachePath.c_str());
    return false;
  }

  const int rowsPerWrite = std::max(1, static_cast<int>(kIoBufferSize) / rowBytes);
  for (int rowBase = 0; rowBase < visible.height; rowBase += rowsPerWrite) {
    const int rowsThisWrite = std::min(rowsPerWrite, visible.height - rowBase);
    const int bytesThisWrite = rowsThisWrite * rowBytes;
    for (int row = 0; row < rowsThisWrite; row++) {
      renderer.readPackedRow1bpp(visible.x, visible.y + rowBase + row, visible.width, rows.get() + row * rowBytes);
    }
    if (file.write(rows.get(), bytesThisWrite) != static_cast<size_t>(bytesThisWrite)) {
      if (options.quality) {
        Serial.printf("[%lu] [IDC-Q] store row write failed plane=%s path=%s row=%d/%d\n", millis(), planeName(options),
                      cachePath.c_str(), rowBase, visible.height);
      }
      file.close();
      SdMan.remove(cachePath.c_str());
      return false;
    }
  }

  file.close();

  return true;
}
