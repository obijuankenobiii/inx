#pragma once

#include <Print.h>
#include <memory>
#include <string>
#include <vector>

#include "Epub/BookMetadataCache.h"

class Epub {
 private:
  std::string tocNcxItem;
  std::string tocNavItem;
  std::string filepath;
  std::string contentBasePath;
  std::string cachePath;
  std::unique_ptr<BookMetadataCache> bookMetadataCache;

  bool findContentOpfFile(std::string* contentOpfFile) const;
  bool parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata);
  bool parseTocNcxFile() const;
  bool parseTocNavFile() const;

 public:
  explicit Epub(std::string filepath, const std::string& oldCacheDir = "") : filepath(std::move(filepath)) {
    std::string hash = std::to_string(std::hash<std::string>{}(this->filepath));
    cachePath = "/.metadata/epub/" + hash;
  }

  ~Epub() = default;

  bool load(bool buildIfMissing = true);
  bool clearCache();
  void setupCacheDir() const;
  
  std::string getCacheImgPath(const std::string& internalHref) const;
  bool extractAndConvertImageFullScreen(const std::string& itemHref, const std::string& outBmpPath, int targetW,
                                        int targetH, bool cropToFill) const;
  bool extractAndConvertImage(const std::string& itemHref, const std::string& outBmpPath, 
                              int targetW = 0, int targetH = 0) const;

  const std::string& getCachePath() const;
  const std::string& getPath() const;
  const std::string& getTitle() const;
  const std::string& getAuthor() const;
  const std::string& getLanguage() const;
  std::string& getBasePath() { return contentBasePath; }

  std::string getCoverBmpPath(bool cropped = false) const;
  bool generateCoverBmp(bool cropped = false) const;
  std::string getThumbBmpPath() const;
  std::string getSmallThumbBmpPath() const;
  bool generateThumbBmp() const;

  uint8_t* readItemContentsToBytes(const std::string& itemHref, size_t* size = nullptr, bool trailingNullByte = false) const;
  bool readItemContentsToStream(const std::string& itemHref, Print& out, size_t chunkSize) const;
  bool getItemSize(const std::string& itemHref, size_t* size) const;

  // Spine and TOC methods
  int getSpineItemsCount() const;
  BookMetadataCache::SpineEntry getSpineItem(int spineIndex) const;
  int getTocItemsCount() const;
  BookMetadataCache::TocEntry getTocItem(int tocIndex) const;
  int getSpineIndexForTocIndex(int tocIndex) const;
  int getTocIndexForSpineIndex(int spineIndex) const;
  int getSpineIndexForTextReference() const;
  
  // CSS methods
  int getCssItemsCount() const;
  BookMetadataCache::CssEntry getCssItem(int cssIndex) const;
  std::string getCssContent(const std::string& cssPath) const;
  std::vector<std::string> getAllCssPaths() const;
  std::string getCombinedCss() const;
  
  // Size and progress methods
  size_t getCumulativeSpineItemSize(int spineIndex) const;
  size_t getBookSize() const;
  float calculateProgress(int currentSpineIndex, float currentSpineRead) const;
};