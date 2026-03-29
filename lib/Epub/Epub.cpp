#include "Epub.h"

#include <FsHelpers.h>
#include <HardwareSerial.h>
#include <JpegToBmpConverter.h>
#include <PngToBmpConverter.h>
#include <SDCardManager.h>
#include <ZipFile.h>

#include "Epub/parsers/ContainerParser.h"
#include "Epub/parsers/ContentOpfParser.h"
#include "Epub/parsers/TocNavParser.h"
#include "Epub/parsers/TocNcxParser.h"

// Helper function to detect file type
static bool isPngFile(const std::string& path) {
  size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return false;
  std::string ext = path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == ".png";
}

static bool isJpegFile(const std::string& path) {
  size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return false;
  std::string ext = path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == ".jpg" || ext == ".jpeg";
}

void Epub::setupCacheDir() const {
  // Create main cache directory if it doesn't exist
  if (!SdMan.exists(cachePath.c_str())) {
    SdMan.mkdir(cachePath.c_str());
  }

  // Create images subfolder if it doesn't exist
  std::string imagesPath = cachePath + "/images";
  if (!SdMan.exists(imagesPath.c_str())) {
    SdMan.mkdir(imagesPath.c_str());
  }
}

std::string Epub::getCacheImgPath(const std::string& internalHref) const {
  size_t lastSlash = internalHref.find_last_of('/');
  std::string fileName = (lastSlash == std::string::npos) ? internalHref : internalHref.substr(lastSlash + 1);

  size_t dot = fileName.find_last_of('.');
  if (dot != std::string::npos) {
    fileName = fileName.substr(0, dot);
  }

  return cachePath + "/images/" + fileName + ".bmp";
}

bool Epub::readItemContentsToStream(const std::string& itemHref, Print& out, const size_t chunkSize) const {
  if (itemHref.empty()) return false;

  std::string path = itemHref;
  if (path.length() > 0 && path[0] == '/') path.erase(0, 1);

  Serial.printf("[EBP] Zip Request: %s\n", path.c_str());

  return ZipFile(filepath).readFileToStream(path.c_str(), out, chunkSize);
}

bool Epub::extractAndConvertImage(const std::string& itemHref, const std::string& outBmpPath, int targetW,
                                  int targetH) const {
  const std::string tempPath = cachePath + "/.extract.tmp";
  FsFile tempFile;

  if (!SdMan.openFileForWrite("EBP", tempPath, tempFile)) return false;

  bool extracted = readItemContentsToStream(itemHref, tempFile, 2048);
  tempFile.flush();
  tempFile.sync();
  tempFile.close();

  if (!extracted) {
    Serial.printf("[EBP] Failed to extract: %s\n", itemHref.c_str());
    SdMan.remove(tempPath.c_str());
    return false;
  }

  FsFile sourceFile, destFile;
  if (!SdMan.openFileForRead("EBP", tempPath, sourceFile)) return false;

  sourceFile.seek(0);
  if (!SdMan.openFileForWrite("EBP", outBmpPath, destFile)) {
    sourceFile.close();
    return false;
  }

  bool success = false;

  // Check file type and use appropriate converter
  if (isPngFile(itemHref)) {
    Serial.printf("[EBP] Converting PNG: %s\n", itemHref.c_str());
    if (targetW > 0 && targetH > 0) {
      success = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(sourceFile, destFile, targetW, targetH);
    } else {
      success = PngToBmpConverter::pngFileTo1BitBmpStream(sourceFile, destFile);
    }
  } else {
    // Default to JPEG for everything else
    Serial.printf("[EBP] Converting as JPEG: %s\n", itemHref.c_str());
    if (targetW > 0 && targetH > 0) {
      success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(sourceFile, destFile, targetW, targetH);
    } else {
      success = JpegToBmpConverter::jpegFileToBmpStream(sourceFile, destFile);
    }
  }

  sourceFile.close();
  destFile.close();
  SdMan.remove(tempPath.c_str());

  return success;
}

bool Epub::findContentOpfFile(std::string* contentOpfFile) const {
  const auto containerPath = "META-INF/container.xml";
  size_t containerSize;
  if (!getItemSize(containerPath, &containerSize)) return false;
  ContainerParser containerParser(containerSize);
  if (!containerParser.setup() || !readItemContentsToStream(containerPath, containerParser, 512)) return false;
  if (containerParser.fullPath.empty()) return false;
  *contentOpfFile = std::move(containerParser.fullPath);
  return true;
}

bool Epub::parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata) {
  std::string opfPath;
  if (!findContentOpfFile(&opfPath)) return false;
  contentBasePath = opfPath.substr(0, opfPath.find_last_of('/') + 1);

  size_t opfSize;
  if (!getItemSize(opfPath, &opfSize)) return false;

  ContentOpfParser opfParser(getCachePath(), getBasePath(), opfSize, bookMetadataCache.get());
  if (!opfParser.setup() || !readItemContentsToStream(opfPath, opfParser, 1024)) return false;

  bookMetadata.title = opfParser.title;
  bookMetadata.author = opfParser.author;
  bookMetadata.language = opfParser.language;
  bookMetadata.coverItemHref = opfParser.coverItemHref;
  bookMetadata.textReferenceHref = opfParser.textReferenceHref;
  if (!opfParser.tocNcxPath.empty()) tocNcxItem = opfParser.tocNcxPath;
  if (!opfParser.tocNavPath.empty()) tocNavItem = opfParser.tocNavPath;

  return true;
}

bool Epub::parseTocNcxFile() const {
  if (tocNcxItem.empty()) return false;
  const auto tmp = cachePath + "/toc.ncx";
  FsFile f;
  if (!SdMan.openFileForWrite("EBP", tmp, f)) return false;
  readItemContentsToStream(tocNcxItem, f, 1024);
  f.close();
  if (!SdMan.openFileForRead("EBP", tmp, f)) return false;
  TocNcxParser parser(contentBasePath, f.size(), bookMetadataCache.get());
  if (!parser.setup()) {
    f.close();
    return false;
  }
  uint8_t buf[1024];
  while (f.available()) {
    size_t r = f.read(buf, 1024);
    if (parser.write(buf, r) != r) break;
  }
  f.close();
  SdMan.remove(tmp.c_str());
  return true;
}

bool Epub::parseTocNavFile() const {
  if (tocNavItem.empty()) return false;
  const auto tmp = cachePath + "/toc.nav";
  FsFile f;
  if (!SdMan.openFileForWrite("EBP", tmp, f)) return false;
  readItemContentsToStream(tocNavItem, f, 1024);
  f.close();
  if (!SdMan.openFileForRead("EBP", tmp, f)) return false;
  const std::string navBase = tocNavItem.substr(0, tocNavItem.find_last_of('/') + 1);
  TocNavParser parser(navBase, f.size(), bookMetadataCache.get());
  if (!parser.setup()) {
    f.close();
    return false;
  }
  uint8_t buf[1024];
  while (f.available()) {
    size_t r = f.read(buf, 1024);
    if (parser.write(buf, r) != r) break;
  }
  f.close();
  SdMan.remove(tmp.c_str());
  return true;
}

bool Epub::load(const bool buildIfMissing) {
  setupCacheDir();  // creates "/.metadata/epub/<hash>" and "/images"

  // create cache object
  bookMetadataCache.reset(new BookMetadataCache(cachePath));

  // only now do load/write
  if (!bookMetadataCache->load()) {
    if (!bookMetadataCache->beginWrite()) return false;
  }

  if (!buildIfMissing) return false;

  if (!bookMetadataCache->beginWrite()) return false;

  BookMetadataCache::BookMetadata meta;
  bookMetadataCache->beginContentOpfPass();

  // Parse metadata
  std::string opfPath;
  if (!findContentOpfFile(&opfPath)) return false;
  contentBasePath = opfPath.substr(0, opfPath.find_last_of('/') + 1);

  size_t opfSize;
  if (!getItemSize(opfPath, &opfSize)) return false;

  ContentOpfParser opfParser(cachePath, getBasePath(), opfSize, bookMetadataCache.get());
  if (!opfParser.setup() || !readItemContentsToStream(opfPath, opfParser, 1024)) return false;

  meta.title = opfParser.title;
  meta.author = opfParser.author;
  meta.language = opfParser.language;
  meta.coverItemHref = opfParser.coverItemHref;
  meta.textReferenceHref = opfParser.textReferenceHref;
  if (!opfParser.tocNcxPath.empty()) tocNcxItem = opfParser.tocNcxPath;
  if (!opfParser.tocNavPath.empty()) tocNavItem = opfParser.tocNavPath;

  bookMetadataCache->endContentOpfPass();
  bookMetadataCache->beginTocPass();
  bool tocParsed = (!tocNavItem.empty()) ? parseTocNavFile() : false;
  if (!tocParsed && !tocNcxItem.empty()) tocParsed = parseTocNcxFile();
  bookMetadataCache->endTocPass();

  bookMetadataCache->endWrite();
  bookMetadataCache->buildBookBin(filepath, meta);
  bookMetadataCache->cleanupTmpFiles();
  bookMetadataCache.reset(new BookMetadataCache(cachePath));
  return bookMetadataCache->load();
}

bool Epub::clearCache() {
  if (bookMetadataCache) {
    bookMetadataCache.reset();
  }

  if (SdMan.exists(cachePath.c_str())) {
    return SdMan.removeDir(cachePath.c_str());
  }
  return true;
}

bool Epub::generateCoverBmp(bool cropped) const {
  // Use full-screen centered converter for covers
  return extractAndConvertImageFullScreen(bookMetadataCache->coreMetadata.coverItemHref, getCoverBmpPath(cropped), 480,
                                          800);
}

bool Epub::generateThumbBmp() const {
  // Generate both regular and small thumbnails
  const std::string tempPath = cachePath + "/.thumb_extract.tmp";
  FsFile tempFile;

  if (!SdMan.openFileForWrite("EBP", tempPath, tempFile)) {
    Serial.println("[EBP] Failed to create temp file for thumb");
    return false;
  }

  bool extracted = readItemContentsToStream(bookMetadataCache->coreMetadata.coverItemHref, tempFile, 2048);
  tempFile.sync();
  tempFile.close();

  if (!extracted) {
    Serial.println("[EBP] Failed to extract cover for thumb");
    SdMan.remove(tempPath.c_str());
    return false;
  }

  FsFile sourceFile;
  if (!SdMan.openFileForRead("EBP", tempPath, sourceFile)) {
    SdMan.remove(tempPath.c_str());
    return false;
  }

  bool success = true;
  const std::string& coverHref = bookMetadataCache->coreMetadata.coverItemHref;
  bool isPng = isPngFile(coverHref);

  FsFile destFile;
  if (!SdMan.openFileForWrite("EBP", getThumbBmpPath(), destFile)) {
    Serial.println("[EBP] Failed to create regular thumb file");
    success = false;
  } else {
    bool thumbSuccess = false;

    if (isPng) {
      thumbSuccess = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(sourceFile, destFile, 225, 340);
    } else {
      JpegToBmpConverter converter;
      thumbSuccess = converter.jpegFileToThumbnailBmp(sourceFile, destFile, 225, 340);
    }

    // If fails, fall back to 1-bit
    if (!thumbSuccess) {
      Serial.println("[EBP] 2-bit conversion failed for regular thumb, trying 1-bit...");
      destFile.seek(0);
      if (isPng) {
        thumbSuccess = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(sourceFile, destFile, 225, 340);
      } else {
        JpegToBmpConverter converter;
        thumbSuccess = converter.jpegFileTo1BitThumbnailBmp(sourceFile, destFile, 225, 340);
      }
    }

    destFile.close();

    if (thumbSuccess) {
      Serial.println("[EBP] Regular thumbnail generated successfully");
    } else {
      Serial.println("[EBP] Failed to generate regular thumbnail");
      success = false;
    }
  }

  sourceFile.close();
  SdMan.remove(tempPath.c_str());

  return success;
}

const std::string& Epub::getCachePath() const { return cachePath; }
const std::string& Epub::getPath() const { return filepath; }

const std::string& Epub::getTitle() const {
  static std::string s;
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->coreMetadata.title : s;
}
const std::string& Epub::getAuthor() const {
  static std::string s;
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->coreMetadata.author : s;
}
const std::string& Epub::getLanguage() const {
  static std::string s;
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->coreMetadata.language : s;
}

std::string Epub::getCoverBmpPath(bool cropped) const {
  return cachePath + (cropped ? "/cover_crop.bmp" : "/cover.bmp");
}

std::string Epub::getThumbBmpPath() const { return cachePath + "/thumb.bmp"; }

std::string Epub::getSmallThumbBmpPath() const { return cachePath + "/small_thumb.bmp"; }

bool Epub::getItemSize(const std::string& href, size_t* size) const {
  return ZipFile(filepath).getInflatedFileSize(FsHelpers::normalisePath(href).c_str(), size);
}

int Epub::getSpineItemsCount() const {
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->getSpineCount() : 0;
}

BookMetadataCache::SpineEntry Epub::getSpineItem(int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return {};
  return bookMetadataCache->getSpineEntry(spineIndex);
}

int Epub::getTocItemsCount() const {
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->getTocCount() : 0;
}

BookMetadataCache::TocEntry Epub::getTocItem(int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return {};
  return bookMetadataCache->getTocEntry(tocIndex);
}

int Epub::getSpineIndexForTocIndex(int tocIndex) const { return getTocItem(tocIndex).spineIndex; }

int Epub::getTocIndexForSpineIndex(int spineIndex) const { return getSpineItem(spineIndex).tocIndex; }

size_t Epub::getCumulativeSpineItemSize(int spineIndex) const { return getSpineItem(spineIndex).cumulativeSize; }

int Epub::getSpineIndexForTextReference() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return 0;
  const std::string& ref = bookMetadataCache->coreMetadata.textReferenceHref;
  if (ref.empty()) return 0;
  for (int i = 0; i < getSpineItemsCount(); i++) {
    if (getSpineItem(i).href == ref) return i;
  }
  return 0;
}

size_t Epub::getBookSize() const {
  int count = getSpineItemsCount();
  return (count > 0) ? getCumulativeSpineItemSize(count - 1) : 0;
}

float Epub::calculateProgress(int currentSpineIndex, float currentSpineRead) const {
  size_t total = getBookSize();
  if (total == 0) return 0.0f;
  size_t prev = (currentSpineIndex > 0) ? getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
  size_t current = getCumulativeSpineItemSize(currentSpineIndex) - prev;
  float progressed = static_cast<float>(prev) + (currentSpineRead * current);
  return progressed / static_cast<float>(total);
}

bool Epub::extractAndConvertImageFullScreen(const std::string& itemHref, const std::string& outBmpPath, int targetW,
                                            int targetH) const {
  const std::string tempPath = cachePath + "/.extract.tmp";
  FsFile tempFile;

  if (!SdMan.openFileForWrite("EBP", tempPath, tempFile)) {
    Serial.printf("[EBP] Failed to create temp file for: %s\n", itemHref.c_str());
    return false;
  }

  bool extracted = readItemContentsToStream(itemHref, tempFile, 2048);
  tempFile.sync();
  tempFile.close();

  if (!extracted) {
    Serial.printf("[EBP] Failed to extract: %s\n", itemHref.c_str());
    SdMan.remove(tempPath.c_str());
    return false;
  }

  FsFile sourceFile, destFile;
  if (!SdMan.openFileForRead("EBP", tempPath, sourceFile)) {
    Serial.printf("[EBP] Failed to open temp file for reading: %s\n", tempPath.c_str());
    SdMan.remove(tempPath.c_str());
    return false;
  }

  if (!SdMan.openFileForWrite("EBP", outBmpPath, destFile)) {
    Serial.printf("[EBP] Failed to create output file: %s\n", outBmpPath.c_str());
    sourceFile.close();
    SdMan.remove(tempPath.c_str());
    return false;
  }

  bool success = false;

  // Check file type and use appropriate converter for full screen
  if (isPngFile(itemHref)) {
    Serial.printf("[EBP] Converting PNG full screen: %s\n", itemHref.c_str());
    success = PngToBmpConverter::pngFileTo1BitBmpStreamCentered(sourceFile, destFile, targetW, targetH);
  } else {
    Serial.printf("[EBP] Converting JPEG full screen: %s\n", itemHref.c_str());
    success = JpegToBmpConverter::jpegFileTo1BitBmpStreamCentered(sourceFile, destFile, targetW, targetH);
  }

  sourceFile.close();
  destFile.close();
  SdMan.remove(tempPath.c_str());

  return success;
}