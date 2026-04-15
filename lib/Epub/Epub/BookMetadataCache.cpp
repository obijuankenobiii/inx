#include "BookMetadataCache.h"

#include <HardwareSerial.h>
#include <Serialization.h>
#include <ZipFile.h>

#include <vector>
#include <algorithm>
#include <cstring>

#include "FsHelpers.h"

namespace {
constexpr uint8_t BOOK_CACHE_VERSION = 6;  // Incremented version for CSS support
constexpr char bookBinFile[] = "/book.bin";
constexpr char tmpSpineBinFile[] = "/spine.bin.tmp";
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";
constexpr char tmpCssBinFile[] = "/css.bin.tmp";  // New temporary file for CSS
}  // namespace

/* ============= WRITING / BUILDING FUNCTIONS ================ */

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

  // Open spine file for writing
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

bool BookMetadataCache::endTocPass() {
  tocFile.close();
  spineFile.close();

  spineHrefIndex.clear();
  spineHrefIndex.shrink_to_fit();
  useSpineHrefIndex = false;

  return true;
}

// New method: Begin CSS pass
bool BookMetadataCache::beginCssPass() {
  if (!buildMode) {
    Serial.printf("[%lu] [BMC] beginCssPass called but not in build mode\n", millis());
    return false;
  }
  
  Serial.printf("[%lu] [BMC] Beginning CSS extraction pass\n", millis());
  
  cssCount = 0;
  
  // Open CSS file for writing
  return SdMan.openFileForWrite("BMC", cachePath + tmpCssBinFile, cssFile);
}

// New method: End CSS pass
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
  // Open all files, writing to meta, reading from spine, toc, and css
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

  // Open CSS temp file if it exists
  bool hasCss = false;
  if (SdMan.exists((cachePath + tmpCssBinFile).c_str())) {
    if (!SdMan.openFileForRead("BMC", cachePath + tmpCssBinFile, cssFile)) {
      Serial.printf("[%lu] [BMC] Warning: Could not open CSS temp file\n", millis());
    } else {
      hasCss = true;
      // Read CSS entries into memory for counting
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
      sizeof(BOOK_CACHE_VERSION) + /* LUT Offset */ sizeof(uint32_t) + sizeof(spineCount) + sizeof(tocCount) + sizeof(cssCount);
  const uint32_t metadataSize = metadata.title.size() + metadata.author.size() + metadata.language.size() +
                                metadata.coverItemHref.size() + metadata.textReferenceHref.size() +
                                sizeof(uint32_t) * 5;
  const uint32_t lutSize = sizeof(uint32_t) * spineCount + sizeof(uint32_t) * tocCount + sizeof(uint32_t) * cssCount;
  const uint32_t lutOffset = headerASize + metadataSize;

  // Header A
  serialization::writePod(bookFile, BOOK_CACHE_VERSION);
  serialization::writePod(bookFile, lutOffset);
  serialization::writePod(bookFile, spineCount);
  serialization::writePod(bookFile, tocCount);
  serialization::writePod(bookFile, cssCount);
  // Metadata
  serialization::writeString(bookFile, metadata.title);
  serialization::writeString(bookFile, metadata.author);
  serialization::writeString(bookFile, metadata.language);
  serialization::writeString(bookFile, metadata.coverItemHref);
  serialization::writeString(bookFile, metadata.textReferenceHref);

  // Loop through spine entries, writing LUT positions
  spineFile.seek(0);
  for (int i = 0; i < spineCount; i++) {
    uint32_t pos = spineFile.position();
    auto spineEntry = readSpineEntry(spineFile);
    serialization::writePod(bookFile, pos + lutOffset + lutSize);
  }

  // Loop through toc entries, writing LUT positions
  tocFile.seek(0);
  for (int i = 0; i < tocCount; i++) {
    uint32_t pos = tocFile.position();
    auto tocEntry = readTocEntry(tocFile);
    serialization::writePod(bookFile, pos + lutOffset + lutSize + static_cast<uint32_t>(spineFile.position()));
  }

  // Loop through CSS entries, writing LUT positions
  if (hasCss) {
    cssFile.seek(0);
    uint32_t cssOffset = lutOffset + lutSize + static_cast<uint32_t>(spineFile.position()) + static_cast<uint32_t>(tocFile.position());
    for (int i = 0; i < cssCount; i++) {
      uint32_t pos = cssFile.position();
      auto cssEntry = readCssEntry(cssFile);
      serialization::writePod(bookFile, pos + cssOffset);
    }
  } else {
    // Write placeholder LUT entries for CSS
    for (int i = 0; i < cssCount; i++) {
      serialization::writePod(bookFile, static_cast<uint32_t>(0));
    }
  }

  // LUTs complete
  // Loop through spines from spine file matching up TOC indexes, calculating cumulative size and writing to book.bin

  // Build spineIndex->tocIndex mapping in one pass (O(n) instead of O(n*m))
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
  // Pre-open zip file to speed up size calculations
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

    // Write out spine data to book.bin
    writeSpineEntry(bookFile, spineEntry);
  }

  // Loop through toc entries from toc file writing to book.bin
  tocFile.seek(0);
  for (int i = 0; i < tocCount; i++) {
    auto tocEntry = readTocEntry(tocFile);
    writeTocEntry(bookFile, tocEntry);
  }

  // Loop through CSS entries writing to book.bin
  if (hasCss) {
    cssFile.seek(0);
    for (int i = 0; i < cssCount; i++) {
      auto cssEntry = readCssEntry(cssFile);
      writeCssEntry(bookFile, cssEntry);
    }
    cssFile.close();
  }

  // Close opened zip file
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

// New method: Write CSS entry
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

// New method: Create CSS entry
void BookMetadataCache::createCssEntry(const std::string& path, const std::string& content) {
  if (!buildMode || !cssFile) {
    Serial.printf("[%lu] [BMC] createCssEntry called but not in build mode\n", millis());
    return;
  }
  
  // Check size limit
  if (content.size() > MAX_CSS_SIZE) {
    Serial.printf("[%lu] [BMC] CSS file too large: %s (%d bytes, max %d)\n", 
                  millis(), path.c_str(), (int)content.size(), MAX_CSS_SIZE);
    return;
  }
  
  const CssEntry entry{path, content, static_cast<uint32_t>(content.size())};
  writeCssEntry(cssFile, entry);
  cssCount++;
}

// New method: Extract CSS files from EPUB by reading the OPF manifest
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
  
  // First, find the OPF file by reading container.xml
  std::string containerPath = "META-INF/container.xml";
  size_t containerSize;
  if (!zip.getInflatedFileSize(containerPath.c_str(), &containerSize)) {
    Serial.printf("[%lu] [BMC] Could not find container.xml\n", millis());
    zip.close();
    return false;
  }
  
  // Read container.xml into a string using a temporary file
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
  
  // Read the temp file into a string
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
  
  // Parse container.xml to find OPF path
  std::string opfPath;
  size_t hrefPos = containerContent.find("full-path=\"");
  if (hrefPos == std::string::npos) {
    hrefPos = containerContent.find("full-path='");
  }
  if (hrefPos != std::string::npos) {
    hrefPos += 11; // length of 'full-path="'
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
  
  // Read OPF file
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
  
  // Read the temp file into a string
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
  
  // Parse OPF to find all CSS files
  size_t searchPos = 0;
  int cssFound = 0;
  
  while (true) {
    // Find item tag
    size_t itemPos = opfContent.find("<item", searchPos);
    if (itemPos == std::string::npos) break;
    
    // Find the end of this item tag
    size_t tagEnd = opfContent.find("/>", itemPos);
    if (tagEnd == std::string::npos) {
      tagEnd = opfContent.find(">", itemPos);
    }
    if (tagEnd == std::string::npos) {
      searchPos = itemPos + 5;
      continue;
    }
    
    // Look for media-type attribute within this tag
    size_t mediaPos = opfContent.find("media-type", itemPos);
    if (mediaPos == std::string::npos || mediaPos > tagEnd) {
      searchPos = itemPos + 5;
      continue;
    }
    
    // Check if it's a CSS file
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
      // Extract href attribute
      size_t hrefPos = opfContent.find("href=\"", itemPos);
      if (hrefPos == std::string::npos) {
        hrefPos = opfContent.find("href='", itemPos);
      }
      
      if (hrefPos != std::string::npos && hrefPos < tagEnd) {
        hrefPos += 6; // length of 'href="'
        size_t hrefEnd = opfContent.find('"', hrefPos);
        if (hrefEnd == std::string::npos) {
          hrefEnd = opfContent.find('\'', hrefPos);
        }
        
        if (hrefEnd != std::string::npos && hrefEnd < tagEnd) {
          std::string cssHref = opfContent.substr(hrefPos, hrefEnd - hrefPos);
          
          // Get base path for OPF
          std::string basePath = opfPath;
          size_t lastSlash = basePath.find_last_of('/');
          if (lastSlash != std::string::npos) {
            basePath = basePath.substr(0, lastSlash + 1);
          } else {
            basePath = "";
          }
          
          // Resolve full path
          std::string fullCssPath = basePath + cssHref;
          
          Serial.printf("[%lu] [BMC] Found CSS file in manifest: %s\n", millis(), fullCssPath.c_str());
          
          // Extract CSS content using the same method as other files
          size_t cssSize;
          if (zip.getInflatedFileSize(fullCssPath.c_str(), &cssSize)) {
            std::string tempCssPath = cachePath + "/.css.tmp";
            FsFile cssTempFile;
            if (SdMan.openFileForWrite("BMC", tempCssPath, cssTempFile)) {
              if (zip.readFileToStream(fullCssPath.c_str(), cssTempFile, 1024)) {
                cssTempFile.close();
                
                // Read the CSS content
                if (SdMan.openFileForRead("BMC", tempCssPath, cssTempFile)) {
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

/* ============= READING / LOADING FUNCTIONS ================ */

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

  // Seek to spine LUT item, read from LUT and get out data
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

  // Seek to TOC LUT item, read from LUT and get out data
  bookFile.seek(lutOffset + sizeof(uint32_t) * spineCount + sizeof(uint32_t) * index);
  uint32_t tocEntryPos;
  serialization::readPod(bookFile, tocEntryPos);
  bookFile.seek(tocEntryPos);
  return readTocEntry(bookFile);
}

// New method: Get CSS entry by index
BookMetadataCache::CssEntry BookMetadataCache::getCssEntry(const int index) {
  if (!loaded) {
    Serial.printf("[%lu] [BMC] getCssEntry called but cache not loaded\n", millis());
    return {};
  }

  if (index < 0 || index >= static_cast<int>(cssCount)) {
    Serial.printf("[%lu] [BMC] getCssEntry index %d out of range\n", millis(), index);
    return {};
  }

  // Seek to CSS LUT item, read from LUT and get out data
  bookFile.seek(lutOffset + sizeof(uint32_t) * spineCount + sizeof(uint32_t) * tocCount + sizeof(uint32_t) * index);
  uint32_t cssEntryPos;
  serialization::readPod(bookFile, cssEntryPos);
  bookFile.seek(cssEntryPos);
  return readCssEntry(bookFile);
}

// New method: Get CSS content by path
std::string BookMetadataCache::getCssContent(const std::string& cssPath) {
  if (!loaded) {
    Serial.printf("[%lu] [BMC] getCssContent called but cache not loaded\n", millis());
    return "";
  }
  
  // Linear search through CSS entries
  for (int i = 0; i < static_cast<int>(cssCount); i++) {
    auto cssEntry = getCssEntry(i);
    if (cssEntry.path == cssPath) {
      return cssEntry.content;
    }
  }
  
  Serial.printf("[%lu] [BMC] CSS file not found: %s\n", millis(), cssPath.c_str());
  return "";
}

// New method: Get all CSS paths
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

// New method: Read CSS entry
BookMetadataCache::CssEntry BookMetadataCache::readCssEntry(FsFile& file) const {
  CssEntry entry;
  serialization::readString(file, entry.path);
  serialization::readString(file, entry.content);
  serialization::readPod(file, entry.size);
  return entry;
}