/**
 * @file ImageDisplayCache.cpp
 * @brief Definitions for ImageDisplayCache.
 */

#include "ImageDisplayCache.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>

#include "GfxRenderer.h"

namespace {
constexpr uint32_t kMagic = 0x43445249;  // IRDC, little-endian on disk
constexpr uint16_t kVersion = 1;

struct CacheHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t headerSize;
  uint16_t width;
  uint16_t height;
  uint16_t rowBytes;
  uint16_t reserved;
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

uint32_t cacheHash(const std::string& sourcePath, const int width, const int height,
                   const ImageDisplayCacheOptions& options) {
  uint32_t hash = 2166136261u;
  for (const char c : sourcePath) {
    hash = fnv1aAdd(hash, static_cast<uint8_t>(c));
  }
  hash = fnv1aAddUint32(hash, sourceSize(sourcePath));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(width));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(height));
  hash = fnv1aAdd(hash, options.cropToFill ? 1 : 0);
  hash = fnv1aAdd(hash, static_cast<uint8_t>(options.bitmapDitherMode));
  hash = fnv1aAdd(hash, static_cast<uint8_t>(options.roundedOutside));
  hash = fnv1aAdd(hash, options.bitmapGrayStyle);
  return hash;
}

std::string parentDir(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) {
    return "/";
  }
  return path.substr(0, slash);
}

bool ensureCacheDir(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return false;
  }
  const std::string dir = path.substr(0, slash);
  if (SdMan.exists(dir.c_str())) {
    return true;
  }
  return SdMan.mkdir(dir.c_str());
}

bool validBounds(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  return width > 0 && height > 0 && x >= 0 && y >= 0 && x + width <= renderer.getScreenWidth() &&
         y + height <= renderer.getScreenHeight();
}
}  // namespace

std::string ImageDisplayCache::pathFor(const std::string& sourcePath, const int width, const int height,
                                       const ImageDisplayCacheOptions& options) {
  char name[32];
  snprintf(name, sizeof(name), "/%08lx.irdc", static_cast<unsigned long>(cacheHash(sourcePath, width, height, options)));
  return parentDir(sourcePath) + "/.display-cache" + name;
}

bool ImageDisplayCache::renderIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, const int x,
                                          const int y, const int width, const int height,
                                          const ImageDisplayCacheOptions& options) {
  if (!validBounds(renderer, x, y, width, height)) {
    return false;
  }

  const std::string cachePath = pathFor(sourcePath, width, height, options);
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
                        header.width == width && header.height == height &&
                        header.rowBytes == static_cast<uint16_t>((width + 7) / 8);
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

  for (int rowIndex = 0; rowIndex < height; rowIndex++) {
    if (file.read(row, rowBytes) != rowBytes) {
      file.close();
      return false;
    }
    renderer.drawPackedRow1bpp(x, y + rowIndex, width, row);
  }

  file.close();
  return true;
}

bool ImageDisplayCache::store(GfxRenderer& renderer, const std::string& sourcePath, const int x, const int y,
                              const int width, const int height, const ImageDisplayCacheOptions& options) {
  if (!validBounds(renderer, x, y, width, height)) {
    return false;
  }

  const std::string cachePath = pathFor(sourcePath, width, height, options);
  if (!ensureCacheDir(cachePath)) {
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForWrite("IDC", cachePath, file)) {
    return false;
  }

  const int rowBytes = (width + 7) / 8;
  uint8_t row[128];
  if (rowBytes > static_cast<int>(sizeof(row))) {
    file.close();
    SdMan.remove(cachePath.c_str());
    return false;
  }

  const CacheHeader header = {.magic = kMagic,
                              .version = kVersion,
                              .headerSize = sizeof(CacheHeader),
                              .width = static_cast<uint16_t>(width),
                              .height = static_cast<uint16_t>(height),
                              .rowBytes = static_cast<uint16_t>(rowBytes),
                              .reserved = 0};
  if (file.write(&header, sizeof(header)) != sizeof(header)) {
    file.close();
    SdMan.remove(cachePath.c_str());
    return false;
  }

  for (int rowIndex = 0; rowIndex < height; rowIndex++) {
    renderer.readPackedRow1bpp(x, y + rowIndex, width, row);
    if (file.write(row, rowBytes) != static_cast<size_t>(rowBytes)) {
      file.close();
      SdMan.remove(cachePath.c_str());
      return false;
    }
  }

  file.close();
  return true;
}
