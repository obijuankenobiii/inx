#pragma once

/**
 * @file Epub.h
 * @brief Public interface and types for Epub.
 */

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

  /** Loads book.bin from cache; on success returns immediately without re-parsing OPF/TOC/CSS. */
  bool load(bool buildIfMissing = true);
  bool clearCache();
  void setupCacheDir() const;
  
  std::string getCacheImgPath(const std::string& internalHref) const;
  bool extractItemToPath(const std::string& itemHref, const std::string& outPath, size_t chunkSize = 2048) const;
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
  std::string getCoverJpegPath(bool cropped = false) const;
  std::string getCoverItemHref() const;
  bool extractCoverItemToPath(const std::string& outPath) const;
  bool generateCoverBmp(bool cropped = false) const;
  std::string getThumbBmpPath() const;
  std::string getThumbJpegPath() const;
  std::string getSmallThumbBmpPath() const;
  bool generateThumbBmp() const;

  uint8_t* readItemContentsToBytes(const std::string& itemHref, size_t* size = nullptr, bool trailingNullByte = false) const;
  bool readItemContentsToStream(const std::string& itemHref, Print& out, size_t chunkSize) const;
  bool getItemSize(const std::string& itemHref, size_t* size) const;

  
  int getSpineItemsCount() const;
  BookMetadataCache::SpineEntry getSpineItem(int spineIndex) const;
  int getTocItemsCount() const;
  BookMetadataCache::TocEntry getTocItem(int tocIndex) const;
  int getSpineIndexForTocIndex(int tocIndex) const;
  int getTocIndexForSpineIndex(int spineIndex) const;
  int getSpineIndexForTextReference() const;
  /** First spine suitable for reading when opening a book (guide text ref, else TOC, else first HTML spine). */
  int getSpineIndexForInitialOpen() const;

  int getCssItemsCount() const;
  BookMetadataCache::CssEntry getCssItem(int cssIndex) const;
  std::string getCssContent(const std::string& cssPath) const;
  std::vector<std::string> getAllCssPaths() const;
  std::string getCombinedCss() const;
  
  
  size_t getCumulativeSpineItemSize(int spineIndex) const;
  size_t getBookSize() const;
  float calculateProgress(int currentSpineIndex, float currentSpineRead) const;
};
