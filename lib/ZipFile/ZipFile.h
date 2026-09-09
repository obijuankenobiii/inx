#pragma once

/**
 * @file ZipFile.h
 * @brief Public interface and types for ZipFile.
 */

#include <SdFat.h>

#include <string>
#include <unordered_map>
#include <vector>

class ZipFile {
 public:
  struct FileStatSlim {
    uint16_t method;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint32_t localHeaderOffset;
  };

  struct ZipDetails {
    uint32_t centralDirOffset;
    uint16_t totalEntries;
    bool isSet;
  };

  struct SizeTarget {
    uint64_t hash;
    uint16_t len;
    uint16_t index;
  };

  static uint64_t fnvHash64(const char* s, size_t len) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; i++) {
      hash ^= static_cast<uint8_t>(s[i]);
      hash *= 1099511628211ull;
    }
    return hash;
  }

 private:
  const std::string& filePath;
  FsFile file;
  ZipDetails zipDetails = {0, 0, false};
  std::unordered_map<std::string, FileStatSlim> fileStatSlimCache;

  uint32_t lastCentralDirPos = 0;
  bool lastCentralDirPosValid = false;

  bool loadFileStatSlim(const char* filename, FileStatSlim* fileStat);
  long getDataOffset(const FileStatSlim& fileStat);
  bool loadZipDetails();

 public:
  explicit ZipFile(const std::string& filePath) : filePath(filePath) {}
  // FsFile does not close itself on destruction (DESTRUCTOR_CLOSES_FILE=0 in SdFatConfig.h), and every
  // caller that only needs one read (e.g. Epub::readItemContentsToStream's `ZipFile(path).readFileToStream(...)`
  // one-liner) relies on this destructor to release the handle - without it, each such call leaks one
  // open SD file handle for the life of the process.
  ~ZipFile() { close(); }

  bool isOpen() const { return !!file; }
  bool open();
  bool close();
  bool loadAllFileStatSlims();
  /** Returns the central-directory entry names after loadAllFileStatSlims(). */
  std::vector<std::string> fileNames() const;
  bool getInflatedFileSize(const char* filename, size_t* size);

  int fillUncompressedSizes(std::vector<SizeTarget>& targets, std::vector<uint32_t>& sizes);

  uint8_t* readFileToMemory(const char* filename, size_t* size = nullptr, bool trailingNullByte = false);
  bool readFileToStream(const char* filename, Print& out, size_t chunkSize);
};
