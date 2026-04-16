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

  size_t dot = fileName.find_last_of('.');
  if (dot != std::string::npos) {
    fileName = fileName.substr(0, dot);
  }

  return cachePath + "/images/" + fileName + ".bmp";
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
 * @brief Extracts an image from the EPUB and converts it to 1-bit BMP.
 *
 * For BMP sources, the file is copied directly without conversion.
 * For PNG and JPEG sources, the image is converted to 1-bit BMP format.
 *
 * @param itemHref Internal path to the image file
 * @param outBmpPath Output path for the converted BMP file
 * @param targetW Target width for resizing (0 for no resize)
 * @param targetH Target height for resizing (0 for no resize)
 * @return true if extraction and conversion succeeded, false otherwise
 */
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

  if (isBmpFile(itemHref)) {
    Serial.printf("[EBP] Source is already BMP, copying directly: %s\n", itemHref.c_str());
    uint8_t buf[2048];
    while (sourceFile.available()) {
      size_t r = sourceFile.read(buf, sizeof(buf));
      destFile.write(buf, r);
    }
    success = true;
  } else if (isPngFile(itemHref)) {
    Serial.printf("[EBP] Converting PNG: %s\n", itemHref.c_str());
    if (targetW > 0 && targetH > 0) {
      success = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(sourceFile, destFile, targetW, targetH);
    } else {
      success = PngToBmpConverter::pngFileTo1BitBmpStream(sourceFile, destFile);
    }
  } else {
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
    Serial.printf("[EBP] Source is already BMP for cover, copying directly: %s\n", itemHref.c_str());
    uint8_t buf[2048];
    while (sourceFile.available()) {
      size_t r = sourceFile.read(buf, sizeof(buf));
      destFile.write(buf, r);
    }
    success = true;
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
      thumbSuccess = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(sourceFile, destFile, 225, 340);
    } else {
      JpegToBmpConverter converter;
      thumbSuccess = converter.jpegFileToThumbnailBmp(sourceFile, destFile, 225, 340);
    }

    if (!thumbSuccess && !isBmpFile(coverHref)) {
      destFile.seek(0);
      if (isPngFile(coverHref)) {
        thumbSuccess = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(sourceFile, destFile, 225, 340);
      } else {
        JpegToBmpConverter converter;
        thumbSuccess = converter.jpegFileTo1BitThumbnailBmp(sourceFile, destFile, 225, 340);
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

  if (!bookMetadataCache->load()) {
    if (!bookMetadataCache->beginWrite()) return false;
  }

  if (!buildIfMissing) return false;

  if (!bookMetadataCache->beginWrite()) return false;

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
  
  // Extract and cache CSS files
  bookMetadataCache->beginCssPass();
  if (!bookMetadataCache->extractAndCacheCssFiles(filepath)) {
    Serial.printf("[EBP] Warning: Failed to extract CSS files\n");
  }
  bookMetadataCache->endCssPass();
  
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