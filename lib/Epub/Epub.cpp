/**
 * @file Epub.cpp
 * @brief Definitions for Epub.
 */

#include "Epub.h"

#include <Arduino.h>

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

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {

bool spineHrefLooksLikeRenderableHtml(const std::string& href) {
  if (href.empty()) {
    return false;
  }
  std::string h = href;
  const size_t hash = h.find('#');
  if (hash != std::string::npos) {
    h = h.substr(0, hash);
  }
  for (char& c : h) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  static const char* kImageExt[] = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".svg", ".webp"};
  for (const char* ext : kImageExt) {
    const size_t n = std::strlen(ext);
    if (h.size() >= n && std::memcmp(h.c_str() + h.size() - n, ext, n) == 0) {
      return false;
    }
  }
  static const char* kHtmlExt[] = {".xhtml", ".html", ".htm", ".xht"};
  for (const char* ext : kHtmlExt) {
    const size_t n = std::strlen(ext);
    if (h.size() >= n && std::memcmp(h.c_str() + h.size() - n, ext, n) == 0) {
      return true;
    }
  }
  return false;
}

}  

/**
 * @brief Checks file type.
 */
static bool isFileType(const std::string& path, const std::string& extension) {
  size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return false;
  std::string ext = path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == extension;
}

/**
 * @brief Checks file type is png.
 */
static bool isPngFile(const std::string& path) { return isFileType(path, ".png"); }

/**
 * @brief Checks file type is jpeg.
 */
static bool isJpegFile(const std::string& path) { return isFileType(path, ".jpg") || isFileType(path, ".jpeg"); }

/**
 * @brief Checks file type is bmp.
 */
static bool isBmpFile(const std::string& path) { return isFileType(path, ".bmp"); }

/**
 * @brief Creates the cache directory structure for this EPUB.
 *
 * Creates both the main cache directory and the images subdirectory
 * if they don't already exist on the SD card.
 */
void Epub::setupCacheDir() const {
  if (!SdMan.exists(cachePath.c_str())) {
    SdMan.mkdir(cachePath.c_str());
  }

  std::string imagesPath = cachePath + "/images";
  if (!SdMan.exists(imagesPath.c_str())) {
    SdMan.mkdir(imagesPath.c_str());
  }
}

/**
 * @brief Generates a cache file path for an internal image reference.
 *
 * @param internalHref Internal EPUB path to the image file
 * @return Full filesystem path where the converted BMP should be cached
 */
std::string Epub::getCacheImgPath(const std::string& internalHref) const {
  size_t lastSlash = internalHref.find_last_of('/');
  std::string fileName = (lastSlash == std::string::npos) ? internalHref : internalHref.substr(lastSlash + 1);

  std::string ext;
  const size_t dotExt = fileName.find_last_of('.');
  if (dotExt != std::string::npos) {
    ext = fileName.substr(dotExt);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  }

  size_t dot = fileName.find_last_of('.');
  if (dot != std::string::npos) {
    fileName = fileName.substr(0, dot);
  }
  if (ext == ".jpg" || ext == ".jpeg") {
    return cachePath + "/images/" + fileName + ".jpg";
  }
  return cachePath + "/images/" + fileName + ".bmp";
}

bool Epub::extractItemToPath(const std::string& itemHref, const std::string& outPath, const size_t chunkSize) const {
  FsFile out;
  if (!SdMan.openFileForWrite("EBP", outPath, out)) {
    return false;
  }
  const bool ok = readItemContentsToStream(itemHref, out, chunkSize);
  out.sync();
  out.close();
  return ok;
}

/**
 * @brief Reads the contents of an EPUB internal file to an output stream.
 *
 * @param itemHref Internal path to the file within the EPUB
 * @param out Output stream to write the file contents to
 * @param chunkSize Size of chunks to read at a time
 * @return true if the file was successfully read, false otherwise
 */
bool Epub::readItemContentsToStream(const std::string& itemHref, Print& out, const size_t chunkSize) const {
  if (itemHref.empty()) return false;

  std::string path = itemHref;
  if (path.length() > 0 && path[0] == '/') path.erase(0, 1);

  Serial.printf("[EBP] Zip Request: %s\n", path.c_str());

  return ZipFile(filepath).readFileToStream(path.c_str(), out, chunkSize);
}

/**
 * @brief Extracts an image from the EPUB and converts it for in-body rendering.
 *
 * BMP sources are copied as-is. PNG/JPEG use the same 2-bit Floyd–Steinberg pipeline as the web
 * Files page (contain within 500×820, BT.601 rounded luma, palette 0/85/170/255, pack thresholds
 * 42/127/212). Thumbnails, covers, and full-screen extraction use other entry points.
 *
 * @param itemHref Internal path to the image file
 * @param outBmpPath Output path for the converted BMP file
 * @param targetW Ignored for PNG/JPEG (kept for API compatibility with callers)
 * @param targetH Ignored for PNG/JPEG
 * @return true if extraction and conversion succeeded, false otherwise
 */
bool Epub::extractAndConvertImage(const std::string& itemHref, const std::string& outBmpPath, int targetW,
                                  int targetH) const {
  Serial.printf("[%lu] [EBP-IMG] extract start href=%s out=%s\n", static_cast<unsigned long>(millis()), itemHref.c_str(),
                outBmpPath.c_str());

  const std::string tempPath = cachePath + "/.extract.tmp";
  FsFile tempFile;

  if (!SdMan.openFileForWrite("EBP", tempPath, tempFile)) {
    Serial.printf("[%lu] [EBP-IMG] open temp write fail: %s\n", static_cast<unsigned long>(millis()), tempPath.c_str());
    return false;
  }

  bool extracted = readItemContentsToStream(itemHref, tempFile, 4096);
  tempFile.flush();
  tempFile.sync();
  tempFile.close();

  if (!extracted) {
    Serial.printf("[%lu] [EBP-IMG] zip read failed href=%s\n", static_cast<unsigned long>(millis()),
                  itemHref.c_str());
    SdMan.remove(tempPath.c_str());
    return false;
  }

  FsFile sourceFile, destFile;
  if (!SdMan.openFileForRead("EBP", tempPath, sourceFile)) {
    Serial.printf("[%lu] [EBP-IMG] open temp read fail: %s\n", static_cast<unsigned long>(millis()), tempPath.c_str());
    SdMan.remove(tempPath.c_str());
    return false;
  }

  sourceFile.seek(0);
  if (!SdMan.openFileForWrite("EBP", outBmpPath, destFile)) {
    Serial.printf("[%lu] [EBP-IMG] open out bmp write fail: %s\n", static_cast<unsigned long>(millis()),
                  outBmpPath.c_str());
    sourceFile.close();
    SdMan.remove(tempPath.c_str());
    return false;
  }

  bool success = false;

  if (isBmpFile(itemHref)) {
    Serial.printf("[%lu] [EBP-IMG] BMP copy: %s\n", static_cast<unsigned long>(millis()), itemHref.c_str());
    uint8_t buf[2048];
    while (sourceFile.available()) {
      size_t r = sourceFile.read(buf, sizeof(buf));
      destFile.write(buf, r);
    }
    success = true;
  } else if (isPngFile(itemHref)) {
    (void)targetW;
    (void)targetH;
    Serial.printf("[%lu] [EBP-IMG] PNG convert: %s\n", static_cast<unsigned long>(millis()), itemHref.c_str());
    success = PngToBmpConverter::pngFileToEpubWebStyle2BitBmpStream(sourceFile, destFile);
    if (!success) {
      Serial.printf("[%lu] [EBP-IMG] PNG pipeline failed: %s\n", static_cast<unsigned long>(millis()),
                    itemHref.c_str());
    }
  } else {
    (void)targetW;
    (void)targetH;
    Serial.printf("[%lu] [EBP-IMG] JPEG convert: %s\n", static_cast<unsigned long>(millis()), itemHref.c_str());
    success = JpegToBmpConverter::jpegFileToEpubWebStyle2BitBmpStream(sourceFile, destFile);
    if (!success) {
      Serial.printf("[%lu] [EBP-IMG] JPEG pipeline failed: %s\n", static_cast<unsigned long>(millis()),
                    itemHref.c_str());
    }
  }

  sourceFile.close();
  destFile.close();
  SdMan.remove(tempPath.c_str());

  if (success) {
    Serial.printf("[%lu] [EBP-IMG] extract ok -> %s\n", static_cast<unsigned long>(millis()), outBmpPath.c_str());
  } else {
    Serial.printf("[%lu] [EBP-IMG] extract failed (after convert) href=%s\n", static_cast<unsigned long>(millis()),
                  itemHref.c_str());
    SdMan.remove(outBmpPath.c_str());
  }

  return success;
}

/**
 * @brief Extracts an image and centers it on a full-screen canvas.
 *
 * For BMP sources, the file is copied directly without conversion.
 * For PNG and JPEG sources, the image is centered on the target canvas.
 *
 * @param itemHref Internal path to the image file
 * @param outBmpPath Output path for the converted BMP file
 * @param targetW Target canvas width (max width for contain mode)
 * @param targetH Target canvas height (max height for contain mode)
 * @param cropToFill true = cover (fill target size, center crop); false = contain (whole image, fit in target)
 * @return true if extraction and conversion succeeded, false otherwise
 */
bool Epub::extractAndConvertImageFullScreen(const std::string& itemHref, const std::string& outBmpPath, int targetW,
                                            int targetH, bool cropToFill) const {
  if (itemHref.empty()) {
    return false;
  }

  const std::string tempPath = cachePath + "/.extract.tmp";
  FsFile tempFile;

  if (!SdMan.openFileForWrite("EBP", tempPath, tempFile)) {
    return false;
  }

  bool extracted = readItemContentsToStream(itemHref, tempFile, 2048);
  tempFile.sync();
  tempFile.close();

  if (!extracted) {
    SdMan.remove(tempPath.c_str());
    return false;
  }

  FsFile sourceFile, destFile;
  if (!SdMan.openFileForRead("EBP", tempPath, sourceFile)) {
    SdMan.remove(tempPath.c_str());
    return false;
  }

  if (!SdMan.openFileForWrite("EBP", outBmpPath, destFile)) {
    sourceFile.close();
    SdMan.remove(tempPath.c_str());
    return false;
  }

  bool success = false;

  if (isBmpFile(itemHref)) {
    
    if (targetW > 0 && targetH > 0) {
      success = JpegToBmpConverter::resizeBitmap(sourceFile, destFile, targetW, targetH);
      if (!success) {
        sourceFile.seek(0);
        destFile.close();
        SdMan.remove(outBmpPath.c_str());
        if (!SdMan.openFileForWrite("EBP", outBmpPath, destFile)) {
          sourceFile.close();
          SdMan.remove(tempPath.c_str());
          return false;
        }
        Serial.printf("[EBP] BMP resize failed, copying raw cover: %s\n", itemHref.c_str());
        uint8_t buf[2048];
        while (sourceFile.available()) {
          size_t r = sourceFile.read(buf, sizeof(buf));
          destFile.write(buf, r);
        }
        success = true;
      }
    } else {
      Serial.printf("[EBP] Source is already BMP for cover, copying directly: %s\n", itemHref.c_str());
      uint8_t buf[2048];
      while (sourceFile.available()) {
        size_t r = sourceFile.read(buf, sizeof(buf));
        destFile.write(buf, r);
      }
      success = true;
    }
  } else if (isPngFile(itemHref)) {
    success = PngToBmpConverter::pngFileTo1BitBmpStreamCentered(sourceFile, destFile, targetW, targetH, cropToFill);
  } else {
    success = JpegToBmpConverter::jpegFileTo1BitBmpStreamCentered(sourceFile, destFile, targetW, targetH, cropToFill);
  }

  sourceFile.close();
  destFile.close();
  SdMan.remove(tempPath.c_str());

  return success;
}

/**
 * @brief Generates the cover image as a BMP file.
 *
 * @param cropped If true, generates a cropped cover; if false, full cover
 * @return true if cover generation succeeded, false otherwise
 */
bool Epub::generateCoverBmp(bool cropped) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded() || bookMetadataCache->coreMetadata.coverItemHref.empty()) {
    return false;
  }
  return extractAndConvertImageFullScreen(bookMetadataCache->coreMetadata.coverItemHref, getCoverBmpPath(cropped), 480,
                                          800, cropped);
}

/**
 * @brief Generates a thumbnail image from the cover.
 * 
 * For BMP sources, the image is resized to thumbnail dimensions.
 * For PNG and JPEG sources, the image is converted and resized.
 * 
 * @return true if thumbnail generation succeeded, false otherwise
 */
bool Epub::generateThumbBmp() const {
  const std::string& coverHref = bookMetadataCache->coreMetadata.coverItemHref;
    
  const std::string tempPath = cachePath + "/.thumb_extract.tmp";
  FsFile tempFile;

  if (!SdMan.openFileForWrite("EBP", tempPath, tempFile)) {
    return false;
  }

  bool extracted = readItemContentsToStream(coverHref, tempFile, 2048);
  tempFile.sync();
  tempFile.close();

  if (!extracted) {
    SdMan.remove(tempPath.c_str());
    return false;
  }

  FsFile sourceFile;
  if (!SdMan.openFileForRead("EBP", tempPath, sourceFile)) {
    SdMan.remove(tempPath.c_str());
    return false;
  }

  bool success = true;

  FsFile destFile;
  if (!SdMan.openFileForWrite("EBP", getThumbBmpPath(), destFile)) {
    success = false;
  } else {
    bool thumbSuccess = false;

    if (isBmpFile(coverHref)) {
      Serial.printf("[EBP] Source is BMP, resizing for thumbnail: %s\n", coverHref.c_str());
      thumbSuccess = JpegToBmpConverter::resizeBitmap(sourceFile, destFile, 225, 340);
    } else if (isPngFile(coverHref)) {
      thumbSuccess = PngToBmpConverter::pngFileTo2BitBmpStreamWithSize(sourceFile, destFile, 225, 340, false);
    } else {
      JpegToBmpConverter converter;
      thumbSuccess = converter.jpegFileToThumbnailBmp(sourceFile, destFile, 225, 340);
    }

    if (!thumbSuccess && !isBmpFile(coverHref)) {
      sourceFile.seek(0);
      destFile.close();
      SdMan.remove(getThumbBmpPath().c_str());
      if (!SdMan.openFileForWrite("EBP", getThumbBmpPath(), destFile)) {
        success = false;
        thumbSuccess = false;
      } else {
        if (isPngFile(coverHref)) {
          thumbSuccess = PngToBmpConverter::pngFileTo2BitBmpStreamWithSize(sourceFile, destFile, 225, 340, false);
        } else {
          thumbSuccess =
              JpegToBmpConverter::jpegFileToBmpStreamCentered(sourceFile, destFile, 225, 340, false);
        }
      }
    }

    destFile.close();
    success = success && thumbSuccess;
  }

  sourceFile.close();
  SdMan.remove(tempPath.c_str());

  return success;
}

/**
 * @brief Finds and parses the container.xml file to locate the OPF file.
 *
 * @param contentOpfFile Output parameter for the OPF file path
 * @return true if the container.xml was found and parsed successfully
 */
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

/**
 * @brief Parses the OPF file to extract book metadata.
 *
 * @param bookMetadata Reference to store the parsed metadata
 * @return true if parsing succeeded, false otherwise
 */
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

/**
 * @brief Parses the NCX table of contents file.
 *
 * @return true if parsing succeeded, false otherwise
 */
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

/**
 * @brief Parses the NAV table of contents file (EPUB3).
 *
 * @return true if parsing succeeded, false otherwise
 */
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

/**
 * @brief Loads the EPUB book and builds metadata cache if needed.
 *
 * @param buildIfMissing If true, builds the cache when not present
 * @return true if the book was successfully loaded, false otherwise
 */
bool Epub::load(const bool buildIfMissing) {
  setupCacheDir();

  bookMetadataCache.reset(new BookMetadataCache(cachePath));

  if (bookMetadataCache->load()) {
    
    return true;
  }

  if (!buildIfMissing) {
    return false;
  }

  if (!bookMetadataCache->beginWrite()) {
    return false;
  }

  BookMetadataCache::BookMetadata meta;
  bookMetadataCache->beginContentOpfPass();

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
  
  
  bookMetadataCache->beginCssPass();
  if (!bookMetadataCache->extractAndCacheCssFiles(filepath)) {
    Serial.printf("[EBP] Warning: Failed to extract CSS files\n");
  }
  bookMetadataCache->endCssPass();
  
  bookMetadataCache->beginTocPass();
  bool tocParsed = (!tocNavItem.empty()) ? parseTocNavFile() : false;
  if (!tocParsed && !tocNcxItem.empty()) tocParsed = parseTocNcxFile();
  bookMetadataCache->appendSyntheticTocFromSpineIfEmpty();
  bookMetadataCache->endTocPass();

  bookMetadataCache->endWrite();
  bookMetadataCache->buildBookBin(filepath, meta);
  bookMetadataCache->cleanupTmpFiles();
  bookMetadataCache.reset(new BookMetadataCache(cachePath));
  return bookMetadataCache->load();
}

/**
 * @brief Clears all cached data for this EPUB.
 *
 * @return true if the cache was successfully cleared, false otherwise
 */
bool Epub::clearCache() {
  if (bookMetadataCache) {
    bookMetadataCache.reset();
  }

  if (SdMan.exists(cachePath.c_str())) {
    return SdMan.removeDir(cachePath.c_str());
  }
  return true;
}

/**
 * @brief Gets the cache directory path for this EPUB.
 *
 * @return Full filesystem path to the cache directory
 */
const std::string& Epub::getCachePath() const { return cachePath; }

/**
 * @brief Gets the original EPUB file path.
 *
 * @return Full filesystem path to the EPUB file
 */
const std::string& Epub::getPath() const { return filepath; }

/**
 * @brief Gets the book title.
 *
 * @return Book title string, empty if not loaded
 */
const std::string& Epub::getTitle() const {
  static std::string s;
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->coreMetadata.title : s;
}

/**
 * @brief Gets the book author.
 *
 * @return Author name string, empty if not loaded
 */
const std::string& Epub::getAuthor() const {
  static std::string s;
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->coreMetadata.author : s;
}

/**
 * @brief Gets the book language.
 *
 * @return Language code string, empty if not loaded
 */
const std::string& Epub::getLanguage() const {
  static std::string s;
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->coreMetadata.language : s;
}

/**
 * @brief Gets the filesystem path for the cover BMP.
 *
 * @param cropped If true, returns path for cropped cover; if false, full cover
 * @return Full filesystem path to the cover BMP file
 */
std::string Epub::getCoverBmpPath(bool cropped) const {
  return cachePath + (cropped ? "/cover_crop.bmp" : "/cover.bmp");
}

std::string Epub::getCoverItemHref() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return "";
  }
  return bookMetadataCache->coreMetadata.coverItemHref;
}

bool Epub::extractCoverItemToPath(const std::string& outPath) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return false;
  }
  const std::string& href = bookMetadataCache->coreMetadata.coverItemHref;
  if (href.empty()) {
    return false;
  }
  FsFile out;
  if (!SdMan.openFileForWrite("EBP", outPath, out)) {
    return false;
  }
  const bool ok = readItemContentsToStream(href, out, 2048);
  out.sync();
  out.close();
  return ok;
}

/**
 * @brief Gets the filesystem path for the thumbnail BMP.
 *
 * @return Full filesystem path to the thumbnail BMP file
 */
std::string Epub::getThumbBmpPath() const { return cachePath + "/thumb.bmp"; }

/**
 * @brief Gets the filesystem path for the small thumbnail BMP.
 *
 * @return Full filesystem path to the small thumbnail BMP file
 */
std::string Epub::getSmallThumbBmpPath() const { return cachePath + "/small_thumb.bmp"; }

/**
 * @brief Retrieves the size of an internal EPUB file.
 *
 * @param href Internal path to the file
 * @param size Output parameter for the file size
 * @return true if the size was successfully retrieved, false otherwise
 */
bool Epub::getItemSize(const std::string& href, size_t* size) const {
  return ZipFile(filepath).getInflatedFileSize(FsHelpers::normalisePath(href).c_str(), size);
}

/**
 * @brief Gets the number of spine items in the book.
 *
 * @return Number of spine items, or 0 if no book is loaded
 */
int Epub::getSpineItemsCount() const {
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->getSpineCount() : 0;
}

/**
 * @brief Retrieves a spine item by index.
 *
 * @param spineIndex Index of the spine item to retrieve
 * @return Spine entry containing the item details
 */
BookMetadataCache::SpineEntry Epub::getSpineItem(int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return {};
  return bookMetadataCache->getSpineEntry(spineIndex);
}

/**
 * @brief Gets the number of TOC items in the book.
 *
 * @return Number of TOC items, or 0 if no book is loaded
 */
int Epub::getTocItemsCount() const {
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->getTocCount() : 0;
}

/**
 * @brief Retrieves a TOC item by index.
 *
 * @param tocIndex Index of the TOC item to retrieve
 * @return TOC entry containing the item details
 */
BookMetadataCache::TocEntry Epub::getTocItem(int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return {};
  return bookMetadataCache->getTocEntry(tocIndex);
}

/**
 * @brief Gets the number of CSS files in the book.
 *
 * @return Number of CSS files, or 0 if no book is loaded
 */
int Epub::getCssItemsCount() const {
  return (bookMetadataCache && bookMetadataCache->isLoaded()) ? bookMetadataCache->getCssCount() : 0;
}

/**
 * @brief Retrieves a CSS entry by index.
 *
 * @param cssIndex Index of the CSS entry to retrieve
 * @return CSS entry containing the file details and content
 */
BookMetadataCache::CssEntry Epub::getCssItem(int cssIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return {};
  return bookMetadataCache->getCssEntry(cssIndex);
}

/**
 * @brief Gets CSS content by file path.
 *
 * @param cssPath Internal path to the CSS file
 * @return CSS content as string, empty if not found
 */
std::string Epub::getCssContent(const std::string& cssPath) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return "";
  return bookMetadataCache->getCssContent(cssPath);
}

/**
 * @brief Gets all CSS file paths in the book.
 *
 * @return Vector of CSS file paths
 */
std::vector<std::string> Epub::getAllCssPaths() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return {};
  return bookMetadataCache->getAllCssPaths();
}

/**
 * @brief Gets combined CSS content from all CSS files.
 *
 * @return Combined CSS content as a single string
 */
std::string Epub::getCombinedCss() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return "";
  
  std::string combined;
  auto paths = getAllCssPaths();
  
  for (const auto& path : paths) {
    std::string cssContent = getCssContent(path);
    if (!cssContent.empty()) {
      if (!combined.empty()) {
        combined += "\n\n/* === " + path + " === */\n\n";
      }
      combined += cssContent;
    }
  }
  
  return combined;
}

/**
 * @brief Gets the spine index for a given TOC index.
 *
 * @param tocIndex TOC index to look up
 * @return Corresponding spine index, or 0 if not found
 */
int Epub::getSpineIndexForTocIndex(int tocIndex) const { return getTocItem(tocIndex).spineIndex; }

/**
 * @brief Gets the TOC index for a given spine index.
 *
 * @param spineIndex Spine index to look up
 * @return Corresponding TOC index, or 0 if not found
 */
int Epub::getTocIndexForSpineIndex(int spineIndex) const { return getSpineItem(spineIndex).tocIndex; }

/**
 * @brief Gets the cumulative size up to a specific spine item.
 *
 * @param spineIndex Spine index to get cumulative size for
 * @return Total size in bytes up to and including the specified spine item
 */
size_t Epub::getCumulativeSpineItemSize(int spineIndex) const { return getSpineItem(spineIndex).cumulativeSize; }

/**
 * @brief Finds the spine index for the text reference href.
 *
 * @return Spine index of the text reference, or 0 if not found
 */
int Epub::getSpineIndexForTextReference() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return 0;
  const std::string& ref = bookMetadataCache->coreMetadata.textReferenceHref;
  if (ref.empty()) return 0;
  for (int i = 0; i < getSpineItemsCount(); i++) {
    if (getSpineItem(i).href == ref) return i;
  }
  return 0;
}

int Epub::getSpineIndexForInitialOpen() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded() || getSpineItemsCount() <= 0) {
    return 0;
  }

  const std::string& ref = bookMetadataCache->coreMetadata.textReferenceHref;
  if (!ref.empty()) {
    const int tr = getSpineIndexForTextReference();
    if (tr >= 0 && tr < getSpineItemsCount()) {
      const std::string& sh = getSpineItem(tr).href;
      if (spineHrefLooksLikeRenderableHtml(sh)) {
        return tr;
      }
    }
  }

  for (int ti = 0; ti < getTocItemsCount(); ++ti) {
    const int sp = getTocItem(ti).spineIndex;
    if (sp < 0 || sp >= getSpineItemsCount()) {
      continue;
    }
    if (spineHrefLooksLikeRenderableHtml(getSpineItem(sp).href)) {
      return sp;
    }
  }

  for (int i = 0; i < getSpineItemsCount(); ++i) {
    if (spineHrefLooksLikeRenderableHtml(getSpineItem(i).href)) {
      return i;
    }
  }

  return 0;
}

/**
 * @brief Calculates the total size of the book in bytes.
 *
 * @return Total size of all spine items combined
 */
size_t Epub::getBookSize() const {
  int count = getSpineItemsCount();
  return (count > 0) ? getCumulativeSpineItemSize(count - 1) : 0;
}

/**
 * @brief Calculates the reading progress percentage.
 *
 * @param currentSpineIndex Current spine item index
 * @param currentSpineRead Progress within the current spine item (0.0 to 1.0)
 * @return Progress value between 0.0 and 1.0
 */
float Epub::calculateProgress(int currentSpineIndex, float currentSpineRead) const {
  size_t total = getBookSize();
  if (total == 0) return 0.0f;
  size_t prev = (currentSpineIndex > 0) ? getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
  size_t current = getCumulativeSpineItemSize(currentSpineIndex) - prev;
  float progressed = static_cast<float>(prev) + (currentSpineRead * current);
  return progressed / static_cast<float>(total);
}