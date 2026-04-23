/**
 * @file Section.cpp
 * @brief Definitions for Section.
 */

#include "Section.h"
#include <Arduino.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include "FontManager.h"
#include "Page.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"
#include <FsHelpers.h> 

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 11;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) +
                                 sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) + sizeof(uint16_t) +
                                 sizeof(uint32_t);
}  

namespace {
class ZipExtractHeapScope {
 public:
  explicit ZipExtractHeapScope(const int readerBodyFontId) { FontManager::enterZipExtractHeapScope(readerBodyFontId); }
  ~ZipExtractHeapScope() { FontManager::leaveZipExtractHeapScope(); }
};
}  // namespace

/**
 * Handles completion of a page during section creation.
 * Serializes the page to the section file and increments the page count.
 * 
 * @param page Unique pointer to the completed page
 * @return The file position where the page was written, or 0 on failure
 */
uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    Serial.printf("[%lu] [SCT] File not open for writing page %d\n", millis(), pageCount);
    return 0;
  }
  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    Serial.printf("[%lu] [SCT] Failed to serialize page %d\n", millis(), pageCount);
    return 0;
  }
  pageCount++;
  return position;
}

/**
 * Writes the header information to the section file.
 * Includes version, rendering settings, page count, and LUT offset.
 * 
 * @param fontId Font identifier for text rendering
 * @param lineCompression Line spacing factor
 * @param extraParagraphSpacing Whether to add extra spacing between paragraphs
 * @param paragraphAlignment Default paragraph alignment
 * @param viewportWidth Available width for layout
 * @param viewportHeight Available height for layout
 * @param hyphenationEnabled Whether hyphenation is enabled
 */
void Section::writeSectionFileHeader(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                     const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                     const uint16_t viewportHeight, const bool hyphenationEnabled,
                                     const bool respectCssParagraphIndent) {
  if (!file) return;
  serialization::writePod(file, SECTION_FILE_VERSION);
  serialization::writePod(file, fontId);
  serialization::writePod(file, lineCompression);
  serialization::writePod(file, extraParagraphSpacing);
  serialization::writePod(file, paragraphAlignment);
  serialization::writePod(file, viewportWidth);
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, hyphenationEnabled);
  serialization::writePod(file, respectCssParagraphIndent);
  serialization::writePod(file, pageCount);
  serialization::writePod(file, static_cast<uint32_t>(0));
}

/**
 * Loads and verifies a section file from disk.
 * Checks file version and ensures all rendering settings match the current request.
 * 
 * @param fontId Font identifier to verify against
 * @param lineCompression Line spacing factor to verify against
 * @param extraParagraphSpacing Paragraph spacing setting to verify against
 * @param paragraphAlignment Paragraph alignment to verify against
 * @param viewportWidth Viewport width to verify against
 * @param viewportHeight Viewport height to verify against
 * @param hyphenationEnabled Hyphenation setting to verify against
 * @return true if section file exists and settings match, false otherwise
 */
bool Section::loadSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                              const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                              const uint16_t viewportHeight, const bool hyphenationEnabled,
                              const bool respectCssParagraphIndent) {
  if (!SdMan.openFileForRead("SCT", filePath, file)) return false;
  
  uint8_t version;
  serialization::readPod(file, version);
  if (version != SECTION_FILE_VERSION && version != 10) {
    file.close();
    clearCache();
    return false;
  }
  
  int storedFontId;
  float storedLineCompression;
  bool storedExtraParagraphSpacing;
  uint8_t storedParagraphAlignment;
  uint16_t storedViewportWidth;
  uint16_t storedViewportHeight;
  bool storedHyphenationEnabled;
  bool storedRespectCssIndent = false;
  uint16_t storedPageCount;
  uint32_t storedLutOffset;

  serialization::readPod(file, storedFontId);
  serialization::readPod(file, storedLineCompression);
  serialization::readPod(file, storedExtraParagraphSpacing);
  serialization::readPod(file, storedParagraphAlignment);
  serialization::readPod(file, storedViewportWidth);
  serialization::readPod(file, storedViewportHeight);
  serialization::readPod(file, storedHyphenationEnabled);
  if (version >= 11) {
    serialization::readPod(file, storedRespectCssIndent);
  }
  serialization::readPod(file, storedPageCount);
  serialization::readPod(file, storedLutOffset);

  bool settingsMatch = true;
  settingsMatch &= (storedFontId == fontId);
  settingsMatch &= (abs(storedLineCompression - lineCompression) < 0.001f);
  settingsMatch &= (storedExtraParagraphSpacing == extraParagraphSpacing);
  settingsMatch &= (storedParagraphAlignment == paragraphAlignment);
  settingsMatch &= (storedViewportWidth == viewportWidth);
  settingsMatch &= (storedViewportHeight == viewportHeight);
  settingsMatch &= (storedHyphenationEnabled == hyphenationEnabled);
  settingsMatch &= (storedRespectCssIndent == respectCssParagraphIndent);
  
  if (!settingsMatch) {
    file.close();
    clearCache();
    return false;
  }
  
  pageCount = storedPageCount;
  
  file.close();
  return true;
}

/**
 * Creates a new section file by parsing the HTML content and building pages.
 * Extracts the HTML from the EPUB, runs the parser, and saves the resulting pages.
 * When building images, runs a prefetch pass (ZIP→cache, SD fonts unloaded) before opening
 * the section file and laying out pages (fonts loaded, images from cache only).
 * Can optionally skip image processing to only rebuild text layout.
 * 
 * @param fontId Font identifier for text rendering
 * @param headerFontId Font identifier for header rendering
 * @param lineCompression Line spacing factor
 * @param extraParagraphSpacing Whether to add extra spacing between paragraphs
 * @param paragraphAlignment Default paragraph alignment
 * @param viewportWidth Available width for layout
 * @param viewportHeight Available height for layout
 * @param hyphenationEnabled Whether hyphenation is enabled
 * @param popupFn Optional callback for progress popups during image conversion
 * @param skipImages If true, skip processing new images and only use existing cached images
 * @return true if section file was successfully created, false otherwise
 */
bool Section::createSectionFile(const int fontId, const int headerFontId, const int maxFontId, const float lineCompression,
                                const bool extraParagraphSpacing, const uint8_t paragraphAlignment,
                                const uint16_t viewportWidth, const uint16_t viewportHeight,
                                const bool hyphenationEnabled, const bool respectCssParagraphIndent,
                                const std::function<void()>& popupFn, bool skipImages) {
  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto tmpHtmlPath = epub->getCachePath() + "/.tmp_" + std::to_string(spineIndex) + ".html";

  std::string contentBasePath = "";
  size_t lastSlash = localPath.find_last_of('/');
  if (lastSlash != std::string::npos) {
    contentBasePath = localPath.substr(0, lastSlash);
  }

  SdMan.mkdir((epub->getCachePath() + "/sections").c_str());

  const ZipExtractHeapScope zipExtractHeapScope(fontId);

  bool success = false;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    FsFile tmpHtml;
    if (SdMan.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
      success = epub->readItemContentsToStream(localPath, tmpHtml, 1024);
      tmpHtml.close();
    }
  }
  if (!success) return false;

  std::vector<uint32_t> lut;

  ChapterHtmlSlimParser visitor(
      tmpHtmlPath,
      *epub,
      epub->getCachePath(),
      contentBasePath,
      renderer,
      fontId,
      headerFontId,
      maxFontId,
      lineCompression,
      extraParagraphSpacing,
      paragraphAlignment,
      viewportWidth,
      viewportHeight,
      hyphenationEnabled,
      respectCssParagraphIndent,
      [this, &lut](std::unique_ptr<Page> page) {
        lut.emplace_back(this->onPageComplete(std::move(page)));
      },
      popupFn);

  visitor.internalPath = localPath;

  Hyphenator::setPreferredLanguage(epub->getLanguage());

  if (!skipImages) {
    bool prefetchOk = true;
    FontManager::withSdFontsReleasedForHeapIntensiveWork(fontId, [&]() { prefetchOk = visitor.prefetchChapterImages(); });
    if (!prefetchOk) {
      SdMan.remove(tmpHtmlPath.c_str());
      return false;
    }
    yield();
  }

  if (!SdMan.openFileForWrite("SCT", filePath, file)) return false;

  writeSectionFileHeader(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                         viewportHeight, hyphenationEnabled, respectCssParagraphIndent);

  success = visitor.parseAndBuildPages(true);

  SdMan.remove(tmpHtmlPath.c_str());
  
  if (!success) {
    file.close();
    SdMan.remove(filePath.c_str());
    return false;
  }

  const uint32_t lutOffset = file.position();
  for (const uint32_t& pos : lut) {
    serialization::writePod(file, pos);
  }

  file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount));
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  file.close();
  return true;
}

/**
 * Loads a specific page from the section file.
 * Uses the look-up table to locate and deserialize the requested page.
 * 
 * @return Unique pointer to the loaded page, or nullptr on failure
 */
std::unique_ptr<Page> Section::loadPageFromSectionFile() {
  if (!SdMan.openFileForRead("SCT", filePath, file)) return nullptr;
  
  file.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t lutOffset;
  serialization::readPod(file, lutOffset);
  
  file.seek(lutOffset + sizeof(uint32_t) * currentPage);
  uint32_t pagePos;
  serialization::readPod(file, pagePos);
  
  file.seek(pagePos);
  auto page = Page::deserialize(file);
  
  file.close();
  
  if (page) {
    Serial.printf("[%lu] [SCT] Loaded page %d\n", millis(), currentPage);
  }
  
  return page;
}

/**
 * Removes the section file from the filesystem.
 * 
 * @return true if file was successfully removed or didn't exist, false on error
 */
bool Section::clearCache() const {
  if (SdMan.exists(filePath.c_str())) {
    return SdMan.remove(filePath.c_str());
  }
  return true;
}