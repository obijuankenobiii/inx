/**
 * @file ImageDisplayCache.cpp
 * @brief Definitions for ImageDisplayCache.
 */

#include "ImageDisplayCache.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <map>

#include "GfxRenderer.h"

namespace {
constexpr uint32_t kMagic = 0x43445249;  // IRDC, little-endian on disk
constexpr uint16_t kVersion = 21;
constexpr const char* kCacheDir = "/.system/cache";

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
  static std::map<std::string, uint32_t> sizeCache;
  const auto cached = sizeCache.find(path);
  if (cached != sizeCache.end()) {
    return cached->second;
  }

  FsFile file;
  if (!SdMan.openFileForRead("IDC", path, file)) {
    sizeCache[path] = 0;
    return 0;
  }
  const uint32_t size = static_cast<uint32_t>(file.size());
  file.close();
  sizeCache[path] = size;
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
}  // namespace

std::string ImageDisplayCache::pathFor(GfxRenderer& renderer, const std::string& sourcePath, const int x, const int y,
                                       const int width, const int height,
                                       const ImageDisplayCacheOptions& options) {
  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return "";
  }
  char name[32];
  snprintf(name, sizeof(name), "/%08lx.irdc",
           static_cast<unsigned long>(cacheHash(sourcePath, width, height, visible, options)));
  return std::string(kCacheDir) + name;
}

bool ImageDisplayCache::exists(GfxRenderer& renderer, const std::string& sourcePath, const int x, const int y,
                               const int width, const int height, const ImageDisplayCacheOptions& options) {
  const std::string cachePath = pathFor(renderer, sourcePath, x, y, width, height, options);
  return !cachePath.empty() && SdMan.exists(cachePath.c_str());
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
    return false;
  }

  CacheHeader header;
  const bool headerOk = file.read(&header, sizeof(header)) == sizeof(header) && header.magic == kMagic &&
                        header.version == kVersion && header.headerSize == sizeof(CacheHeader) &&
                        header.width == visible.width && header.height == visible.height &&
                        header.rowBytes == static_cast<uint16_t>((visible.width + 7) / 8);
  if (!headerOk) {
    file.close();
    return false;
  }

  const int rowBytes = header.rowBytes;
  uint8_t row[128];
  if (rowBytes > static_cast<int>(sizeof(row))) {
    file.close();
    return false;
  }

  for (int rowIndex = 0; rowIndex < visible.height; rowIndex++) {
    if (file.read(row, rowBytes) != rowBytes) {
      file.close();
      return false;
    }
    renderer.drawPackedRow1bpp(visible.x, visible.y + rowIndex, visible.width, row);
  }

  file.close();
  return true;
}

bool ImageDisplayCache::displayTwoBitIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, const int x,
                                                 const int y, const int width, const int height,
                                                 const ImageDisplayCacheOptions& options) {
  ImageDisplayCacheOptions lsbOptions = options;
  lsbOptions.mode = ImageRenderMode::TwoBit;
  lsbOptions.renderPlane = static_cast<uint8_t>(GfxRenderer::GRAYSCALE_LSB);

  ImageDisplayCacheOptions msbOptions = options;
  msbOptions.mode = ImageRenderMode::TwoBit;
  msbOptions.renderPlane = static_cast<uint8_t>(GfxRenderer::GRAYSCALE_MSB);

  if (!exists(renderer, sourcePath, x, y, width, height, lsbOptions) ||
      !exists(renderer, sourcePath, x, y, width, height, msbOptions)) {
    return false;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!renderIfAvailable(renderer, sourcePath, x, y, width, height, lsbOptions)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!renderIfAvailable(renderer, sourcePath, x, y, width, height, msbOptions)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}

bool ImageDisplayCache::store(GfxRenderer& renderer, const std::string& sourcePath, const int x, const int y,
                              const int width, const int height, const ImageDisplayCacheOptions& options) {
  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return false;
  }

  const std::string cachePath = pathFor(renderer, sourcePath, x, y, width, height, options);
  if (cachePath.empty()) {
    return false;
  }
  if (!ensureCacheDir(cachePath)) {
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForWrite("IDC", cachePath, file)) {
    return false;
  }

  const int rowBytes = (visible.width + 7) / 8;
  uint8_t row[128];
  if (rowBytes > static_cast<int>(sizeof(row))) {
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

  for (int rowIndex = 0; rowIndex < visible.height; rowIndex++) {
    renderer.readPackedRow1bpp(visible.x, visible.y + rowIndex, visible.width, row);
    if (file.write(row, rowBytes) != static_cast<size_t>(rowBytes)) {
      file.close();
      SdMan.remove(cachePath.c_str());
      return false;
    }
  }

  file.close();
  return true;
}
