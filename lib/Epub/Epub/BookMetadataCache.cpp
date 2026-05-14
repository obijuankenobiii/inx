/**
 * @file BookMetadataCache.cpp
 * @brief Definitions for BookMetadataCache.
 */

#include "BookMetadataCache.h"

#include <HardwareSerial.h>
#include <Serialization.h>
#include <ZipFile.h>

#include <vector>
#include <algorithm>
#include <cstring>

#include "FsHelpers.h"

namespace {
constexpr uint8_t BOOK_CACHE_VERSION = 6;  
constexpr char bookBinFile[] = "/book.bin";
constexpr char tmpSpineBinFile[] = "/spine.bin.tmp";
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";
constexpr char tmpCssBinFile[] = "/css.bin.tmp";  
constexpr size_t kMaxParsedCssBytes = 50 * 1024;
}  



bool BookMetadataCache::beginWrite() {
  buildMode = true;
  spineCount = 0;
  tocCount = 0;
  cssCount = 0;
  Serial.printf("[%lu] [BMC] Entering write mode\n", millis());
  return true;
}

bool BookMetadataCache::beginContentOpfPass() {
  Serial.printf("[%lu] [BMC] Beginning content opf pass\n", millis());

  
  return SdMan.openFileForWrite("BMC", cachePath + tmpSpineBinFile, spineFile);
}

bool BookMetadataCache::endContentOpfPass() {
  spineFile.close();
  return true;
}

bool BookMetadataCache::beginTocPass() {
  Serial.printf("[%lu] [BMC] Beginning toc pass\n", millis());

  if (!SdMan.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    return false;
  }
  if (!SdMan.openFileForWrite("BMC", cachePath + tmpTocBinFile, tocFile)) {
    spineFile.close();
    return false;
  }

  if (spineCount >= LARGE_SPINE_THRESHOLD) {
    spineHrefIndex.clear();
    spineHrefIndex.reserve(spineCount);
    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      auto entry = readSpineEntry(spineFile);
      SpineHrefIndexEntry idx;
      idx.hrefHash = fnvHash64(entry.href);
      idx.hrefLen = static_cast<uint16_t>(entry.href.size());
      idx.spineIndex = static_cast<int16_t>(i);
      spineHrefIndex.push_back(idx);
    }
    std::sort(spineHrefIndex.begin(), spineHrefIndex.end(),
              [](const SpineHrefIndexEntry& a, const SpineHrefIndexEntry& b) {
                return a.hrefHash < b.hrefHash || (a.hrefHash == b.hrefHash && a.hrefLen < b.hrefLen);
              });
    spineFile.seek(0);
    useSpineHrefIndex = true;
    Serial.printf("[%lu] [BMC] Using fast index for %d spine items\n", millis(), spineCount);
  } else {
    useSpineHrefIndex = false;
  }

  return true;
}

void BookMetadataCache::appendSyntheticTocFromSpineIfEmpty() {
  if (!buildMode || !tocFile || spineCount == 0 || tocCount > 0) {
    return;
  }
  spineFile.seek(0);
  for (int i = 0; i < spineCount; ++i) {
    const SpineEntry se = readSpineEntry(spineFile);
    const std::string title = "Chapter " + std::to_string(i + 1);
    const TocEntry entry(title, se.href, "", 1, static_cast<int16_t>(i));
    writeTocEntry(tocFile, entry);
    ++tocCount;
  }
  Serial.printf("[%lu] [BMC] Synthetic TOC: %d spine entries (no nav/ncx rows)\n", millis(), spineCount);
}

bool BookMetadataCache::endTocPass() {
  tocFile.close();
  spineFile.close();

  spineHrefIndex.clear();
  spineHrefIndex.shrink_to_fit();
  useSpineHrefIndex = false;

  return true;
}


bool BookMetadataCache::beginCssPass() {
  if (!buildMode) {
    Serial.printf("[%lu] [BMC] beginCssPass called but not in build mode\n", millis());
    return false;
  }
  
  Serial.printf("[%lu] [BMC] Beginning CSS extraction pass\n", millis());
  
  cssCount = 0;
  
  
  return SdMan.openFileForWrite("BMC", cachePath + tmpCssBinFile, cssFile);
}


bool BookMetadataCache::endCssPass() {
  if (cssFile) {
    cssFile.close();
  }
  Serial.printf("[%lu] [BMC] Extracted %d CSS files\n", millis(), cssCount);
  return true;
}

bool BookMetadataCache::endWrite() {
  if (!buildMode) {
    Serial.printf("[%lu] [BMC] endWrite called but not in build mode\n", millis());
    return false;
  }

  buildMode = false;
  Serial.printf("[%lu] [BMC] Wrote %d spine, %d TOC, %d CSS entries\n", millis(), spineCount, tocCount, cssCount);
  return true;
}

bool BookMetadataCache::buildBookBin(const std::string& epubPath, const BookMetadata& metadata) {
  
  if (!SdMan.openFileForWrite("BMC", cachePath + bookBinFile, bookFile)) {
    return false;
  }

  if (!SdMan.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    bookFile.close();
    return false;
  }

  if (!SdMan.openFileForRead("BMC", cachePath + tmpTocBinFile, tocFile)) {
    bookFile.close();
    spineFile.close();
    return false;
  }

  
  bool hasCss = false;
  if (SdMan.exists((cachePath + tmpCssBinFile).c_str())) {
    if (!SdMan.openFileForRead("BMC", cachePath + tmpCssBinFile, cssFile)) {
      Serial.printf("[%lu] [BMC] Warning: Could not open CSS temp file\n", millis());
    } else {
      hasCss = true;
      
      cssFile.seek(0);
      cssCount = 0;
      while (cssFile.available()) {
        auto cssEntry = readCssEntry(cssFile);
        cssCount++;
      }
      cssFile.seek(0);
    }
  }

  constexpr uint32_t headerASize =
      sizeof(BOOK_CACHE_VERSION) +  sizeof(uint32_t) + sizeof(spineCount) + sizeof(tocCount) + sizeof(cssCount);
  const uint32_t metadataSize = metadata.title.size() + metadata.author.size() + metadata.language.size() +
                                metadata.coverItemHref.size() + metadata.textReferenceHref.size() +
                                sizeof(uint32_t) * 5;
  const uint32_t lutSize = sizeof(uint32_t) * spineCount + sizeof(uint32_t) * tocCount + sizeof(uint32_t) * cssCount;
  const uint32_t lutOffset = headerASize + metadataSize;

  
  serialization::writePod(bookFile, BOOK_CACHE_VERSION);
  serialization::writePod(bookFile, lutOffset);
  serialization::writePod(bookFile, spineCount);
  serialization::writePod(bookFile, tocCount);
  serialization::writePod(bookFile, cssCount);
  
  serialization::writeString(bookFile, metadata.title);
  serialization::writeString(bookFile, metadata.author);
  serialization::writeString(bookFile, metadata.language);
  serialization::writeString(bookFile, metadata.coverItemHref);
  serialization::writeString(bookFile, metadata.textReferenceHref);

  
  spineFile.seek(0);
  for (int i = 0; i < spineCount; i++) {
    uint32_t pos = spineFile.position();
    auto spineEntry = readSpineEntry(spineFile);
    serialization::writePod(bookFile, pos + lutOffset + lutSize);
  }

  
  tocFile.seek(0);
  for (int i = 0; i < tocCount; i++) {
    uint32_t pos = tocFile.position();
    auto tocEntry = readTocEntry(tocFile);
    serialization::writePod(bookFile, pos + lutOffset + lutSize + static_cast<uint32_t>(spineFile.position()));
  }

  
  if (hasCss) {
    cssFile.seek(0);
    uint32_t cssOffset = lutOffset + lutSize + static_cast<uint32_t>(spineFile.position()) + static_cast<uint32_t>(tocFile.position());
    for (int i = 0; i < cssCount; i++) {
      uint32_t pos = cssFile.position();
      auto cssEntry = readCssEntry(cssFile);
      serialization::writePod(bookFile, pos + cssOffset);
    }
  } else {
    
    for (int i = 0; i < cssCount; i++) {
      serialization::writePod(bookFile, static_cast<uint32_t>(0));
    }
  }

  
  

  
  std::vector<int16_t> spineToTocIndex(spineCount, -1);
  tocFile.seek(0);
  for (int j = 0; j < tocCount; j++) {
    auto tocEntry = readTocEntry(tocFile);
    if (tocEntry.spineIndex >= 0 && tocEntry.spineIndex < spineCount) {
      if (spineToTocIndex[tocEntry.spineIndex] == -1) {
        spineToTocIndex[tocEntry.spineIndex] = static_cast<int16_t>(j);
      }
    }
  }

  ZipFile zip(epubPath);
  
  if (!zip.open()) {
    Serial.printf("[%lu] [BMC] Could not open EPUB zip for size calculations\n", millis());
    bookFile.close();
    spineFile.close();
    tocFile.close();
    if (hasCss) cssFile.close();
    return false;
  }

  std::vector<uint32_t> spineSizes;
  bool useBatchSizes = false;

  if (spineCount >= LARGE_SPINE_THRESHOLD) {
    Serial.printf("[%lu] [BMC] Using batch size lookup for %d spine items\n", millis(), spineCount);

    std::vector<ZipFile::SizeTarget> targets;
    targets.reserve(spineCount);

    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      auto entry = readSpineEntry(spineFile);
      std::string path = FsHelpers::normalisePath(entry.href);

      ZipFile::SizeTarget t;
      t.hash = ZipFile::fnvHash64(path.c_str(), path.size());
      t.len = static_cast<uint16_t>(path.size());
      t.index = static_cast<uint16_t>(i);
      targets.push_back(t);
    }

    std::sort(targets.begin(), targets.end(), [](const ZipFile::SizeTarget& a, const ZipFile::SizeTarget& b) {
      return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
    });

    spineSizes.resize(spineCount, 0);
    int matched = zip.fillUncompressedSizes(targets, spineSizes);
    Serial.printf("[%lu] [BMC] Batch lookup matched %d/%d spine items\n", millis(), matched, spineCount);

    targets.clear();
    targets.shrink_to_fit();

    useBatchSizes = true;
  }

  uint32_t cumSize = 0;
  spineFile.seek(0);
  int lastSpineTocIndex = -1;
  for (int i = 0; i < spineCount; i++) {
    auto spineEntry = readSpineEntry(spineFile);

    spineEntry.tocIndex = spineToTocIndex[i];

    if (spineEntry.tocIndex == -1) {
      Serial.printf(
          "[%lu] [BMC] Warning: Could not find TOC entry for spine item %d: %s, using title from last section\n",
          millis(), i, spineEntry.href.c_str());
      spineEntry.tocIndex = lastSpineTocIndex;
    }
    lastSpineTocIndex = spineEntry.tocIndex;

    size_t itemSize = 0;
    if (useBatchSizes) {
      itemSize = spineSizes[i];
      if (itemSize == 0) {
        const std::string path = FsHelpers::normalisePath(spineEntry.href);
        if (!zip.getInflatedFileSize(path.c_str(), &itemSize)) {
          Serial.printf("[%lu] [BMC] Warning: Could not get size for spine item: %s\n", millis(), path.c_str());
        }
      }
    } else {
      const std::string path = FsHelpers::normalisePath(spineEntry.href);
      if (!zip.getInflatedFileSize(path.c_str(), &itemSize)) {
        Serial.printf("[%lu] [BMC] Warning: Could not get size for spine item: %s\n", millis(), path.c_str());
      }
    }

    cumSize += itemSize;
    spineEntry.cumulativeSize = cumSize;

    
    writeSpineEntry(bookFile, spineEntry);
  }

  
  tocFile.seek(0);
  for (int i = 0; i < tocCount; i++) {
    auto tocEntry = readTocEntry(tocFile);
    writeTocEntry(bookFile, tocEntry);
  }

  
  if (hasCss) {
    cssFile.seek(0);
    for (int i = 0; i < cssCount; i++) {
      auto cssEntry = readCssEntry(cssFile);
      writeCssEntry(bookFile, cssEntry);
    }
    cssFile.close();
  }

  
  zip.close();

  bookFile.close();
  spineFile.close();
  tocFile.close();

  Serial.printf("[%lu] [BMC] Successfully built book.bin\n", millis());
  return true;
}

bool BookMetadataCache::cleanupTmpFiles() const {
  if (SdMan.exists((cachePath + tmpSpineBinFile).c_str())) {
    SdMan.remove((cachePath + tmpSpineBinFile).c_str());
  }
  if (SdMan.exists((cachePath + tmpTocBinFile).c_str())) {
    SdMan.remove((cachePath + tmpTocBinFile).c_str());
  }
  if (SdMan.exists((cachePath + tmpCssBinFile).c_str())) {
    SdMan.remove((cachePath + tmpCssBinFile).c_str());
  }
  return true;
}

uint32_t BookMetadataCache::writeSpineEntry(FsFile& file, const SpineEntry& entry) const {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.href);
  serialization::writePod(file, entry.cumulativeSize);
  serialization::writePod(file, entry.tocIndex);
  return pos;
}

uint32_t BookMetadataCache::writeTocEntry(FsFile& file, const TocEntry& entry) const {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.title);
  serialization::writeString(file, entry.href);
  serialization::writeString(file, entry.anchor);
  serialization::writePod(file, entry.level);
  serialization::writePod(file, entry.spineIndex);
  return pos;
}


uint32_t BookMetadataCache::writeCssEntry(FsFile& file, const CssEntry& entry) const {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.path);
  serialization::writeString(file, entry.content);
  serialization::writePod(file, entry.size);
  return pos;
}

void BookMetadataCache::createSpineEntry(const std::string& href) {
  if (!buildMode || !spineFile) {
    Serial.printf("[%lu] [BMC] createSpineEntry called but not in build mode\n", millis());
    return;
  }

  const SpineEntry entry(href, 0, -1);
  writeSpineEntry(spineFile, entry);
  spineCount++;
}

void BookMetadataCache::createTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                                       const uint8_t level) {
  if (!buildMode || !tocFile || !spineFile) {
    Serial.printf("[%lu] [BMC] createTocEntry called but not in build mode\n", millis());
    return;
  }

  int16_t spineIndex = -1;

  if (useSpineHrefIndex) {
    uint64_t targetHash = fnvHash64(href);
    uint16_t targetLen = static_cast<uint16_t>(href.size());

    auto it =
        std::lower_bound(spineHrefIndex.begin(), spineHrefIndex.end(), SpineHrefIndexEntry{targetHash, targetLen, 0},
                         [](const SpineHrefIndexEntry& a, const SpineHrefIndexEntry& b) {
                           return a.hrefHash < b.hrefHash || (a.hrefHash == b.hrefHash && a.hrefLen < b.hrefLen);
                         });

    while (it != spineHrefIndex.end() && it->hrefHash == targetHash && it->hrefLen == targetLen) {
      spineIndex = it->spineIndex;
      break;
    }

    if (spineIndex == -1) {
      Serial.printf("[%lu] [BMC] createTocEntry: Could not find spine item for TOC href %s\n", millis(), href.c_str());
    }
  } else {
    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      auto spineEntry = readSpineEntry(spineFile);
      if (spineEntry.href == href) {
        spineIndex = static_cast<int16_t>(i);
        break;
      }
    }
    if (spineIndex == -1) {
      Serial.printf("[%lu] [BMC] createTocEntry: Could not find spine item for TOC href %s\n", millis(), href.c_str());
    }
  }

  const TocEntry entry(title, href, anchor, level, spineIndex);
  writeTocEntry(tocFile, entry);
  tocCount++;
}


void BookMetadataCache::createCssEntry(const std::string& path, const std::string& content) {
  if (!buildMode || !cssFile) {
    Serial.printf("[%lu] [BMC] createCssEntry called but not in build mode\n", millis());
    return;
  }
  
  
  if (content.size() > kMaxParsedCssBytes) {
    Serial.printf("[%lu] [BMC] CSS file too large: %s (%d bytes, max %d)\n", 
                  millis(), path.c_str(), (int)content.size(), static_cast<int>(kMaxParsedCssBytes));
    return;
  }
  
  const CssEntry entry{path, content, static_cast<uint32_t>(content.size())};
  writeCssEntry(cssFile, entry);
  cssCount++;
}


bool BookMetadataCache::extractAndCacheCssFiles(const std::string& epubPath) {
  if (!buildMode) {
    Serial.printf("[%lu] [BMC] extractAndCacheCssFiles called but not in build mode\n", millis());
    return false;
  }
  
  ZipFile zip(epubPath);
  if (!zip.open()) {
    Serial.printf("[%lu] [BMC] Could not open EPUB zip for CSS extraction\n", millis());
    return false;
  }
  
  
  std::string containerPath = "META-INF/container.xml";
  size_t containerSize;
  if (!zip.getInflatedFileSize(containerPath.c_str(), &containerSize)) {
    Serial.printf("[%lu] [BMC] Could not find container.xml\n", millis());
    zip.close();
    return false;
  }
  
  
  std::string tempContainerPath = cachePath + "/.container.tmp";
  FsFile tempFile;
  if (!SdMan.openFileForWrite("BMC", tempContainerPath, tempFile)) {
    zip.close();
    return false;
  }
  
  if (!zip.readFileToStream(containerPath.c_str(), tempFile, 512)) {
    tempFile.close();
    SdMan.remove(tempContainerPath.c_str());
    zip.close();
    return false;
  }
  
  tempFile.close();
  
  
  if (!SdMan.openFileForRead("BMC", tempContainerPath, tempFile)) {
    SdMan.remove(tempContainerPath.c_str());
    zip.close();
    return false;
  }
  
  std::string containerContent;
  containerContent.reserve(tempFile.size());
  uint8_t buf[256];
  while (tempFile.available()) {
    size_t len = tempFile.read(buf, sizeof(buf));
    containerContent.append(reinterpret_cast<char*>(buf), len);
  }
  tempFile.close();
  SdMan.remove(tempContainerPath.c_str());
  
  
  std::string opfPath;
  size_t hrefPos = containerContent.find("full-path=\"");
  if (hrefPos == std::string::npos) {
    hrefPos = containerContent.find("full-path='");
  }
  if (hrefPos != std::string::npos) {
    hrefPos += 11; 
    size_t hrefEnd = containerContent.find('"', hrefPos);
    if (hrefEnd == std::string::npos) {
      hrefEnd = containerContent.find('\'', hrefPos);
    }
    if (hrefEnd != std::string::npos) {
      opfPath = containerContent.substr(hrefPos, hrefEnd - hrefPos);
    }
  }
  
  if (opfPath.empty()) {
    Serial.printf("[%lu] [BMC] Could not find OPF path in container.xml\n", millis());
    zip.close();
    return false;
  }
  
  Serial.printf("[%lu] [BMC] Found OPF: %s\n", millis(), opfPath.c_str());
  
  
  size_t opfSize;
  if (!zip.getInflatedFileSize(opfPath.c_str(), &opfSize)) {
    Serial.printf("[%lu] [BMC] Could not get OPF size\n", millis());
    zip.close();
    return false;
  }
  
  std::string tempOpfPath = cachePath + "/.opf.tmp";
  if (!SdMan.openFileForWrite("BMC", tempOpfPath, tempFile)) {
    zip.close();
    return false;
  }
  
  if (!zip.readFileToStream(opfPath.c_str(), tempFile, 1024)) {
    tempFile.close();
    SdMan.remove(tempOpfPath.c_str());
    zip.close();
    return false;
  }
  
  tempFile.close();
  
  
  if (!SdMan.openFileForRead("BMC", tempOpfPath, tempFile)) {
    SdMan.remove(tempOpfPath.c_str());
    zip.close();
    return false;
  }
  
  std::string opfContent;
  opfContent.reserve(tempFile.size());
  while (tempFile.available()) {
    size_t len = tempFile.read(buf, sizeof(buf));
    opfContent.append(reinterpret_cast<char*>(buf), len);
  }
  tempFile.close();
  SdMan.remove(tempOpfPath.c_str());
  
  
  size_t searchPos = 0;
  int cssFound = 0;
  
  while (true) {
    
    size_t itemPos = opfContent.find("<item", searchPos);
    if (itemPos == std::string::npos) break;
    
    
    size_t tagEnd = opfContent.find("/>", itemPos);
    if (tagEnd == std::string::npos) {
      tagEnd = opfContent.find(">", itemPos);
    }
    if (tagEnd == std::string::npos) {
      searchPos = itemPos + 5;
      continue;
    }
    
    
    size_t mediaPos = opfContent.find("media-type", itemPos);
    if (mediaPos == std::string::npos || mediaPos > tagEnd) {
      searchPos = itemPos + 5;
      continue;
    }
    
    
    bool isCss = false;
    size_t cssTypePos = opfContent.find("text/css", mediaPos);
    if (cssTypePos != std::string::npos && cssTypePos < tagEnd) {
      isCss = true;
    }
    if (!isCss) {
      cssTypePos = opfContent.find("application/x-css", mediaPos);
      if (cssTypePos != std::string::npos && cssTypePos < tagEnd) {
        isCss = true;
      }
    }
    
    if (isCss) {
      
      size_t hrefPos = opfContent.find("href=\"", itemPos);
      if (hrefPos == std::string::npos) {
        hrefPos = opfContent.find("href='", itemPos);
      }
      
      if (hrefPos != std::string::npos && hrefPos < tagEnd) {
        hrefPos += 6; 
        size_t hrefEnd = opfContent.find('"', hrefPos);
        if (hrefEnd == std::string::npos) {
          hrefEnd = opfContent.find('\'', hrefPos);
        }
        
        if (hrefEnd != std::string::npos && hrefEnd < tagEnd) {
          std::string cssHref = opfContent.substr(hrefPos, hrefEnd - hrefPos);
          
          
          std::string basePath = opfPath;
          size_t lastSlash = basePath.find_last_of('/');
          if (lastSlash != std::string::npos) {
            basePath = basePath.substr(0, lastSlash + 1);
          } else {
            basePath = "";
          }
          
          
          std::string fullCssPath = basePath + cssHref;
          
          Serial.printf("[%lu] [BMC] Found CSS file in manifest: %s\n", millis(), fullCssPath.c_str());
          
          
          size_t cssSize;
          if (zip.getInflatedFileSize(fullCssPath.c_str(), &cssSize)) {
            if (cssSize > kMaxParsedCssBytes) {
              Serial.printf("[%lu] [BMC] Skipping large CSS file: %s (%u bytes)\n", millis(), fullCssPath.c_str(),
                            static_cast<unsigned>(cssSize));
              searchPos = itemPos + 5;
              continue;
            }
            std::string tempCssPath = cachePath + "/.css.tmp";
            FsFile cssTempFile;
            if (SdMan.openFileForWrite("BMC", tempCssPath, cssTempFile)) {
              const bool cssStreamOk = zip.readFileToStream(fullCssPath.c_str(), cssTempFile, 1024);
              if (cssStreamOk) {
                cssTempFile.close();
                
                
                if (SdMan.openFileForRead("BMC", tempCssPath, cssTempFile)) {
                  if (cssTempFile.size() > kMaxParsedCssBytes) {
                    Serial.printf("[%lu] [BMC] Skipping large CSS temp file: %s (%u bytes)\n", millis(),
                                  fullCssPath.c_str(), static_cast<unsigned>(cssTempFile.size()));
                    cssTempFile.close();
                    SdMan.remove(tempCssPath.c_str());
                    searchPos = itemPos + 5;
                    continue;
                  }
                  std::string cssContent;
                  cssContent.reserve(cssTempFile.size());
                  uint8_t cssBuf[1024];
                  while (cssTempFile.available()) {
                    size_t len = cssTempFile.read(cssBuf, sizeof(cssBuf));
                    cssContent.append(reinterpret_cast<char*>(cssBuf), len);
                  }
                  cssTempFile.close();
                  
                  createCssEntry(fullCssPath, cssContent);
                  cssFound++;
                }
              } else {
                cssTempFile.close();
              }
              SdMan.remove(tempCssPath.c_str());
            }
          } else {
            Serial.printf("[%lu] [BMC] Could not get CSS file size: %s\n", millis(), fullCssPath.c_str());
          }
        }
      }
    }
    
    searchPos = itemPos + 5;
  }
  
  zip.close();
  Serial.printf("[%lu] [BMC] Extracted %d CSS files from EPUB manifest\n", millis(), cssFound);
  return true;
}



bool BookMetadataCache::load() {
  if (!SdMan.openFileForRead("BMC", cachePath + bookBinFile, bookFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(bookFile, version);
  if (version != BOOK_CACHE_VERSION) {
    Serial.printf("[%lu] [BMC] Cache version mismatch: expected %d, got %d\n", millis(), BOOK_CACHE_VERSION, version);
    bookFile.close();
    return false;
  }

  serialization::readPod(bookFile, lutOffset);
  serialization::readPod(bookFile, spineCount);
  serialization::readPod(bookFile, tocCount);
  serialization::readPod(bookFile, cssCount);

  serialization::readString(bookFile, coreMetadata.title);
  serialization::readString(bookFile, coreMetadata.author);
  serialization::readString(bookFile, coreMetadata.language);
  serialization::readString(bookFile, coreMetadata.coverItemHref);
  serialization::readString(bookFile, coreMetadata.textReferenceHref);

  loaded = true;
  Serial.printf("[%lu] [BMC] Loaded cache data: %d spine, %d TOC, %d CSS entries\n", millis(), spineCount, tocCount, cssCount);
  return true;
}

BookMetadataCache::SpineEntry BookMetadataCache::getSpineEntry(const int index) {
  if (!loaded) {
    Serial.printf("[%lu] [BMC] getSpineEntry called but cache not loaded\n", millis());
    return {};
  }

  if (index < 0 || index >= static_cast<int>(spineCount)) {
    Serial.printf("[%lu] [BMC] getSpineEntry index %d out of range\n", millis(), index);
    return {};
  }

  
  bookFile.seek(lutOffset + sizeof(uint32_t) * index);
  uint32_t spineEntryPos;
  serialization::readPod(bookFile, spineEntryPos);
  bookFile.seek(spineEntryPos);
  return readSpineEntry(bookFile);
}

BookMetadataCache::TocEntry BookMetadataCache::getTocEntry(const int index) {
  if (!loaded) {
    Serial.printf("[%lu] [BMC] getTocEntry called but cache not loaded\n", millis());
    return {};
  }

  if (index < 0 || index >= static_cast<int>(tocCount)) {
    Serial.printf("[%lu] [BMC] getTocEntry index %d out of range\n", millis(), index);
    return {};
  }

  
  bookFile.seek(lutOffset + sizeof(uint32_t) * spineCount + sizeof(uint32_t) * index);
  uint32_t tocEntryPos;
  serialization::readPod(bookFile, tocEntryPos);
  bookFile.seek(tocEntryPos);
  return readTocEntry(bookFile);
}


BookMetadataCache::CssEntry BookMetadataCache::getCssEntry(const int index) {
  if (!loaded) {
    Serial.printf("[%lu] [BMC] getCssEntry called but cache not loaded\n", millis());
    return {};
  }

  if (index < 0 || index >= static_cast<int>(cssCount)) {
    Serial.printf("[%lu] [BMC] getCssEntry index %d out of range\n", millis(), index);
    return {};
  }

  
  bookFile.seek(lutOffset + sizeof(uint32_t) * spineCount + sizeof(uint32_t) * tocCount + sizeof(uint32_t) * index);
  uint32_t cssEntryPos;
  serialization::readPod(bookFile, cssEntryPos);
  bookFile.seek(cssEntryPos);
  return readCssEntry(bookFile);
}


std::string BookMetadataCache::getCssContent(const std::string& cssPath) {
  if (!loaded) {
    Serial.printf("[%lu] [BMC] getCssContent called but cache not loaded\n", millis());
    return "";
  }
  
  
  for (int i = 0; i < static_cast<int>(cssCount); i++) {
    auto cssEntry = getCssEntry(i);
    if (cssEntry.path == cssPath) {
      return cssEntry.content;
    }
  }
  
  Serial.printf("[%lu] [BMC] CSS file not found: %s\n", millis(), cssPath.c_str());
  return "";
}


std::vector<std::string> BookMetadataCache::getAllCssPaths() {
  std::vector<std::string> paths;
  if (!loaded) {
    Serial.printf("[%lu] [BMC] getAllCssPaths called but cache not loaded\n", millis());
    return paths;
  }
  
  paths.reserve(cssCount);
  for (int i = 0; i < static_cast<int>(cssCount); i++) {
    auto cssEntry = getCssEntry(i);
    paths.push_back(cssEntry.path);
  }
  
  return paths;
}

BookMetadataCache::SpineEntry BookMetadataCache::readSpineEntry(FsFile& file) const {
  SpineEntry entry;
  serialization::readString(file, entry.href);
  serialization::readPod(file, entry.cumulativeSize);
  serialization::readPod(file, entry.tocIndex);
  return entry;
}

BookMetadataCache::TocEntry BookMetadataCache::readTocEntry(FsFile& file) const {
  TocEntry entry;
  serialization::readString(file, entry.title);
  serialization::readString(file, entry.href);
  serialization::readString(file, entry.anchor);
  serialization::readPod(file, entry.level);
  serialization::readPod(file, entry.spineIndex);
  return entry;
}


BookMetadataCache::CssEntry BookMetadataCache::readCssEntry(FsFile& file) const {
  CssEntry entry;
  serialization::readString(file, entry.path);
  serialization::readString(file, entry.content);
  serialization::readPod(file, entry.size);
  return entry;
}
