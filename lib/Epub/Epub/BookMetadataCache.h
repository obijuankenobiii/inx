#pragma once

#include <SDCardManager.h>

#include <algorithm>
#include <string>
#include <vector>

class BookMetadataCache {
 public:
  struct BookMetadata {
    std::string title;
    std::string author;
    std::string language;
    std::string coverItemHref;
    std::string textReferenceHref;
  };

  struct SpineEntry {
    std::string href;
    size_t cumulativeSize;
    int16_t tocIndex;

    SpineEntry() : cumulativeSize(0), tocIndex(-1) {}
    SpineEntry(std::string href, const size_t cumulativeSize, const int16_t tocIndex)
        : href(std::move(href)), cumulativeSize(cumulativeSize), tocIndex(tocIndex) {}
  };

  struct TocEntry {
    std::string title;
    std::string href;
    std::string anchor;
    uint8_t level;
    int16_t spineIndex;

    TocEntry() : level(0), spineIndex(-1) {}
    TocEntry(std::string title, std::string href, std::string anchor, const uint8_t level, const int16_t spineIndex)
        : title(std::move(title)),
          href(std::move(href)),
          anchor(std::move(anchor)),
          level(level),
          spineIndex(spineIndex) {}
  };

  // CSS entry structure for caching CSS files
  struct CssEntry {
    std::string path;
    std::string content;
    uint32_t size;

    CssEntry() : size(0) {}
    CssEntry(std::string path, std::string content, const uint32_t size)
        : path(std::move(path)), content(std::move(content)), size(size) {}
  };

 private:
  std::string cachePath;
  size_t lutOffset;
  uint16_t spineCount;
  uint16_t tocCount;
  uint16_t cssCount;  // New: CSS file count
  bool loaded;
  bool buildMode;

  FsFile bookFile;
  // Temp file handles during build
  FsFile spineFile;
  FsFile tocFile;
  FsFile cssFile;  // New: CSS temp file handle

  // Index for fast href→spineIndex lookup (used only for large EPUBs)
  struct SpineHrefIndexEntry {
    uint64_t hrefHash;  // FNV-1a 64-bit hash
    uint16_t hrefLen;   // length for collision reduction
    int16_t spineIndex;
  };
  std::vector<SpineHrefIndexEntry> spineHrefIndex;
  bool useSpineHrefIndex = false;

  static constexpr uint16_t LARGE_SPINE_THRESHOLD = 400;
  static constexpr uint32_t MAX_CSS_SIZE = 1024 * 1024;  // 1MB max per CSS file

  // FNV-1a 64-bit hash function
  static uint64_t fnvHash64(const std::string& s) {
    uint64_t hash = 14695981039346656037ull;
    for (char c : s) {
      hash ^= static_cast<uint8_t>(c);
      hash *= 1099511628211ull;
    }
    return hash;
  }

  uint32_t writeSpineEntry(FsFile& file, const SpineEntry& entry) const;
  uint32_t writeTocEntry(FsFile& file, const TocEntry& entry) const;
  uint32_t writeCssEntry(FsFile& file, const CssEntry& entry) const;  // New
  SpineEntry readSpineEntry(FsFile& file) const;
  TocEntry readTocEntry(FsFile& file) const;
  CssEntry readCssEntry(FsFile& file) const;  // New

 public:
  BookMetadata coreMetadata;

  explicit BookMetadataCache(std::string cachePath)
      : cachePath(std::move(cachePath)), 
        lutOffset(0), 
        spineCount(0), 
        tocCount(0), 
        cssCount(0),  // Initialize CSS count
        loaded(false), 
        buildMode(false) {}
  ~BookMetadataCache() = default;

  // Building phase (stream to disk immediately)
  bool beginWrite();
  bool beginContentOpfPass();
  void createSpineEntry(const std::string& href);
  bool endContentOpfPass();
  bool beginTocPass();
  void createTocEntry(const std::string& title, const std::string& href, const std::string& anchor, uint8_t level);
  bool endTocPass();
  
  // New: CSS building phase methods
  bool beginCssPass();
  void createCssEntry(const std::string& path, const std::string& content);
  bool endCssPass();
  
  // New: Extract and cache all CSS files from EPUB
  bool extractAndCacheCssFiles(const std::string& epubPath);
  
  bool endWrite();
  bool cleanupTmpFiles() const;

  // Post-processing to update mappings and sizes
  bool buildBookBin(const std::string& epubPath, const BookMetadata& metadata);

  // Reading phase (read mode)
  bool load();
  SpineEntry getSpineEntry(int index);
  TocEntry getTocEntry(int index);
  
  // New: CSS reading methods
  CssEntry getCssEntry(int index);
  std::string getCssContent(const std::string& cssPath);
  std::vector<std::string> getAllCssPaths();
  
  // Getters
  int getSpineCount() const { return spineCount; }
  int getTocCount() const { return tocCount; }
  int getCssCount() const { return cssCount; }  // New
  bool isLoaded() const { return loaded; }
};