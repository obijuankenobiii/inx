/**
 * @file Section.cpp
 * @brief Definitions for Section.
 */

#include "Section.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <exception>
#include <new>

#include "Page.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 61;  // 61: apply CSS font-style/font-weight to text runs
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(float) + sizeof(bool) +
                                 sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) +
                                 sizeof(bool) + sizeof(uint16_t) + sizeof(uint32_t);
constexpr uint16_t MAX_CACHED_PAGE_OFFSETS = 2048;
}  // namespace

Section::~Section() {
  if (file) {
    file.close();
  }
}

/**
 * Handles completion of a page during section creation.
 * Serializes the page to the section file and increments the page count.
 *
 * @param page Unique pointer to the completed page
 * @return The file position where the page was written, or 0 on failure
 */
uint32_t Section::onPageComplete(std::unique_ptr<Page> page, const std::function<void(Page&, uint16_t)>& pageBuiltFn) {
  if (!file) {
    Serial.printf("[%lu] [SCT] File not open for writing page %d\n", millis(), pageCount);
    return 0;
  }
  const uint32_t position = file.position();
  if (pageBuiltFn) {
    pageBuiltFn(*page, pageCount);
  }
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
void Section::writeSectionFileHeader(const int fontId, const float lineCompression, const float wordSpacing,
                                     const bool extraParagraphSpacing, const uint8_t paragraphAlignment,
                                     const uint16_t viewportWidth, const uint16_t viewportHeight,
                                     const bool hyphenationEnabled, const bool respectCssParagraphIndent,
                                     const bool bionicReadingEnabled) {
  if (!file) return;
  serialization::writePod(file, SECTION_FILE_VERSION);
  serialization::writePod(file, fontId);
  serialization::writePod(file, lineCompression);
  serialization::writePod(file, wordSpacing);
  serialization::writePod(file, extraParagraphSpacing);
  serialization::writePod(file, paragraphAlignment);
  serialization::writePod(file, viewportWidth);
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, hyphenationEnabled);
  serialization::writePod(file, respectCssParagraphIndent);
  serialization::writePod(file, bionicReadingEnabled);
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
bool Section::loadSectionFile(const int fontId, const float lineCompression, const float wordSpacing,
                              const bool extraParagraphSpacing, const uint8_t paragraphAlignment,
                              const uint16_t viewportWidth, const uint16_t viewportHeight,
                              const bool hyphenationEnabled, const bool respectCssParagraphIndent,
                              const bool bionicReadingEnabled) {
  if (file) {
    file.close();
  }
  lutOffset = 0;
  pageOffsets.clear();

  if (!SdMan.openFileForRead("SCT", filePath, file)) return false;

  uint8_t version;
  serialization::readPod(file, version);
  if (version != SECTION_FILE_VERSION) {
    file.close();
    clearCache();
    return false;
  }

  int storedFontId;
  float storedLineCompression;
  float storedWordSpacing = 1.0f;
  bool storedExtraParagraphSpacing;
  uint8_t storedParagraphAlignment;
  uint16_t storedViewportWidth;
  uint16_t storedViewportHeight;
  bool storedHyphenationEnabled;
  bool storedRespectCssIndent = false;
  bool storedBionicReadingEnabled = false;
  uint16_t storedPageCount;
  uint32_t storedLutOffset;

  serialization::readPod(file, storedFontId);
  serialization::readPod(file, storedLineCompression);
  serialization::readPod(file, storedWordSpacing);
  serialization::readPod(file, storedExtraParagraphSpacing);
  serialization::readPod(file, storedParagraphAlignment);
  serialization::readPod(file, storedViewportWidth);
  serialization::readPod(file, storedViewportHeight);
  serialization::readPod(file, storedHyphenationEnabled);
  serialization::readPod(file, storedRespectCssIndent);
  serialization::readPod(file, storedBionicReadingEnabled);
  serialization::readPod(file, storedPageCount);
  serialization::readPod(file, storedLutOffset);

  bool settingsMatch = true;
  settingsMatch &= (storedFontId == fontId);
  settingsMatch &= (abs(storedLineCompression - lineCompression) < 0.001f);
  settingsMatch &= (abs(storedWordSpacing - wordSpacing) < 0.001f);
  settingsMatch &= (storedExtraParagraphSpacing == extraParagraphSpacing);
  settingsMatch &= (storedParagraphAlignment == paragraphAlignment);
  settingsMatch &= (storedViewportWidth == viewportWidth);
  settingsMatch &= (storedViewportHeight == viewportHeight);
  settingsMatch &= (storedHyphenationEnabled == hyphenationEnabled);
  settingsMatch &= (storedRespectCssIndent == respectCssParagraphIndent);
  settingsMatch &= (storedBionicReadingEnabled == bionicReadingEnabled);

  if (!settingsMatch) {
    file.close();
    clearCache();
    return false;
  }

  pageCount = storedPageCount;
  lutOffset = storedLutOffset;

  if (pageCount == 0 || lutOffset == 0) {
    Serial.printf("[%lu] [SCT] loadSectionFile: invalid empty section cache spine=%d pages=%u lut=%lu file=%s\n",
                  millis(), spineIndex, static_cast<unsigned>(pageCount), static_cast<unsigned long>(lutOffset),
                  filePath.c_str());
    file.close();
    clearCache();
    return false;
  }

  if (pageCount > 0 && pageCount <= MAX_CACHED_PAGE_OFFSETS && lutOffset > 0) {
    try {
      pageOffsets.resize(pageCount);
      file.seek(lutOffset);
      for (uint16_t i = 0; i < pageCount; ++i) {
        serialization::readPod(file, pageOffsets[i]);
      }
    } catch (...) {
      pageOffsets.clear();
    }
  }

  return true;
}

bool Section::loadSectionFileForPreview(int* outFontId) {
  if (file) {
    file.close();
  }
  lutOffset = 0;
  pageOffsets.clear();
  pageCount = 0;

  if (!SdMan.openFileForRead("SCT", filePath, file)) return false;

  uint8_t version = 0;
  serialization::readPod(file, version);
  if (version != SECTION_FILE_VERSION) {
    file.close();
    return false;
  }

  int storedFontId = 0;
  float storedLineCompression = 1.0f;
  float storedWordSpacing = 1.0f;
  bool storedExtraParagraphSpacing = false;
  uint8_t storedParagraphAlignment = 0;
  uint16_t storedViewportWidth = 0;
  uint16_t storedViewportHeight = 0;
  bool storedHyphenationEnabled = false;
  bool storedRespectCssIndent = false;
  bool storedBionicReadingEnabled = false;
  uint16_t storedPageCount = 0;
  uint32_t storedLutOffset = 0;

  serialization::readPod(file, storedFontId);
  serialization::readPod(file, storedLineCompression);
  serialization::readPod(file, storedWordSpacing);
  serialization::readPod(file, storedExtraParagraphSpacing);
  serialization::readPod(file, storedParagraphAlignment);
  serialization::readPod(file, storedViewportWidth);
  serialization::readPod(file, storedViewportHeight);
  serialization::readPod(file, storedHyphenationEnabled);
  serialization::readPod(file, storedRespectCssIndent);
  serialization::readPod(file, storedBionicReadingEnabled);
  serialization::readPod(file, storedPageCount);
  serialization::readPod(file, storedLutOffset);

  pageCount = storedPageCount;
  lutOffset = storedLutOffset;
  if (outFontId != nullptr) {
    *outFontId = storedFontId;
  }
  return pageCount > 0 && lutOffset != 0;
}

std::unique_ptr<Page> Section::loadCachedPage(const std::string& cachePath, const int spineIndex,
                                              const int pageNumber) {
  if (spineIndex < 0 || pageNumber < 0) {
    return nullptr;
  }

  FsFile sectionFile;
  const std::string path = cachePath + "/sections/" + std::to_string(spineIndex) + ".bin";
  if (!SdMan.openFileForRead("SCT", path, sectionFile)) {
    return nullptr;
  }

  uint8_t version = 0;
  serialization::readPod(sectionFile, version);
  if (version != SECTION_FILE_VERSION) {
    sectionFile.close();
    return nullptr;
  }

  int storedFontId = 0;
  float storedLineCompression = 1.0f;
  float storedWordSpacing = 1.0f;
  bool storedExtraParagraphSpacing = false;
  uint8_t storedParagraphAlignment = 0;
  uint16_t storedViewportWidth = 0;
  uint16_t storedViewportHeight = 0;
  bool storedHyphenationEnabled = false;
  bool storedRespectCssIndent = false;
  bool storedBionicReadingEnabled = false;
  uint16_t storedPageCount = 0;
  uint32_t storedLutOffset = 0;

  serialization::readPod(sectionFile, storedFontId);
  serialization::readPod(sectionFile, storedLineCompression);
  serialization::readPod(sectionFile, storedWordSpacing);
  serialization::readPod(sectionFile, storedExtraParagraphSpacing);
  serialization::readPod(sectionFile, storedParagraphAlignment);
  serialization::readPod(sectionFile, storedViewportWidth);
  serialization::readPod(sectionFile, storedViewportHeight);
  serialization::readPod(sectionFile, storedHyphenationEnabled);
  serialization::readPod(sectionFile, storedRespectCssIndent);
  serialization::readPod(sectionFile, storedBionicReadingEnabled);
  serialization::readPod(sectionFile, storedPageCount);
  serialization::readPod(sectionFile, storedLutOffset);

  if (storedPageCount == 0 || storedLutOffset == 0 || pageNumber >= static_cast<int>(storedPageCount)) {
    sectionFile.close();
    return nullptr;
  }

  uint32_t pagePos = 0;
  sectionFile.seek(storedLutOffset + sizeof(uint32_t) * pageNumber);
  serialization::readPod(sectionFile, pagePos);
  if (pagePos == 0) {
    sectionFile.close();
    return nullptr;
  }

  sectionFile.seek(pagePos);
  auto page = Page::deserialize(sectionFile);
  sectionFile.close();
  return page;
}

/**
 * Creates a new section file by parsing the HTML content and building pages.
 * Streams chapter HTML from the EPUB to a temp file (same retry/chunk pattern as Crosspoint),
 * then parses once: images are converted during layout unless skipImages is true.
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
bool Section::createSectionFile(const int fontId, const int headerFontId, const int maxFontId,
                                const float lineCompression, const float wordSpacing, const bool extraParagraphSpacing,
                                const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                const uint16_t viewportHeight, const bool hyphenationEnabled,
                                const bool respectCssParagraphIndent, const bool bionicReadingEnabled,
                                const std::function<void()>& popupFn, bool skipImages,
                                const std::function<void(Page&, uint16_t)>& pageBuiltFn,
                                const bool warmImageDisplayCache, const ImageRenderMode warmImageRenderMode,
                                const bool warmImageQuality, const int warmImageYOffset) {
  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto tmpHtmlPath = epub->getCachePath() + "/.tmp_" + std::to_string(spineIndex) + ".html";

  std::string contentBasePath = "";
  size_t lastSlash = localPath.find_last_of('/');
  if (lastSlash != std::string::npos) {
    contentBasePath = localPath.substr(0, lastSlash);
  }

  SdMan.mkdir((epub->getCachePath() + "/sections").c_str());

  bool success = false;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    if (attempt > 0) {
      delay(50);
    }
    if (SdMan.exists(tmpHtmlPath.c_str())) {
      SdMan.remove(tmpHtmlPath.c_str());
    }
    FsFile tmpHtml;
    if (!SdMan.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
      Serial.printf(
          "[%lu] [SCT] createSectionFile: failed to open temp HTML for write attempt=%d spine=%d href=%s tmp=%s "
          "book=%s heap=%u\n",
          millis(), attempt + 1, spineIndex, localPath.c_str(), tmpHtmlPath.c_str(), epub->getPath().c_str(),
          static_cast<unsigned>(ESP.getFreeHeap()));
      continue;
    }
    success = epub->readItemContentsToStream(localPath, tmpHtml, 1024);
    const uint32_t tmpSize = tmpHtml.position();
    tmpHtml.close();
    Serial.printf(
        "[%lu] [SCT] createSectionFile: temp HTML extract attempt=%d ok=%d bytes=%lu spine=%d href=%s tmp=%s "
        "book=%s title=%s heap=%u\n",
        millis(), attempt + 1, success ? 1 : 0, static_cast<unsigned long>(tmpSize), spineIndex, localPath.c_str(),
        tmpHtmlPath.c_str(), epub->getPath().c_str(), epub->getTitle().c_str(),
        static_cast<unsigned>(ESP.getFreeHeap()));
    if (!success && SdMan.exists(tmpHtmlPath.c_str())) {
      SdMan.remove(tmpHtmlPath.c_str());
    }
  }
  if (!success) {
    Serial.printf(
        "[%lu] [SCT] createSectionFile: failed to extract chapter after retries spine=%d href=%s book=%s "
        "title=%s heap=%u\n",
        millis(), spineIndex, localPath.c_str(), epub->getPath().c_str(), epub->getTitle().c_str(),
        static_cast<unsigned>(ESP.getFreeHeap()));
    return false;
  }

  std::vector<uint32_t> lut;

  ChapterHtmlSlimParser visitor(
      tmpHtmlPath, *epub, epub->getCachePath(), contentBasePath, renderer, fontId, headerFontId, maxFontId,
      lineCompression, wordSpacing, extraParagraphSpacing, paragraphAlignment, viewportWidth, viewportHeight,
      hyphenationEnabled, respectCssParagraphIndent, bionicReadingEnabled,
      [this, &lut, &pageBuiltFn](std::unique_ptr<Page> page) {
        lut.emplace_back(this->onPageComplete(std::move(page), pageBuiltFn));
      },
      warmImageDisplayCache, warmImageRenderMode, warmImageQuality, warmImageYOffset, popupFn);

  visitor.internalPath = localPath;

  Hyphenator::setPreferredLanguage(epub->getLanguage());

  if (!SdMan.openFileForWrite("SCT", filePath, file)) return false;

  writeSectionFileHeader(fontId, lineCompression, wordSpacing, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                         viewportHeight, hyphenationEnabled, respectCssParagraphIndent, bionicReadingEnabled);

  try {
    success = visitor.parseAndBuildPages(skipImages);
  } catch (const std::bad_alloc& e) {
    Serial.printf("[%lu] [SCT] createSectionFile: OOM while parsing spine=%d href=%s (%s)\n", millis(), spineIndex,
                  localPath.c_str(), e.what());
    success = false;
  } catch (const std::exception& e) {
    Serial.printf("[%lu] [SCT] createSectionFile: exception while parsing spine=%d href=%s (%s)\n", millis(),
                  spineIndex, localPath.c_str(), e.what());
    success = false;
  } catch (...) {
    Serial.printf("[%lu] [SCT] createSectionFile: unknown exception while parsing spine=%d href=%s\n", millis(),
                  spineIndex, localPath.c_str());
    success = false;
  }

  SdMan.remove(tmpHtmlPath.c_str());

  if (!success) {
    Serial.printf(
        "[%lu] [SCT] createSectionFile: parser returned false spine=%d href=%s file=%s book=%s title=%s pages=%u "
        "heap=%u\n",
        millis(), spineIndex, localPath.c_str(), filePath.c_str(), epub->getPath().c_str(), epub->getTitle().c_str(),
        static_cast<unsigned>(pageCount), static_cast<unsigned>(ESP.getFreeHeap()));
    file.close();
    SdMan.remove(filePath.c_str());
    return false;
  }

  if (pageCount == 0) {
    Serial.printf("[%lu] [SCT] createSectionFile: zero-page section spine=%d href=%s book=%s title=%s\n", millis(),
                  spineIndex, localPath.c_str(), epub->getPath().c_str(), epub->getTitle().c_str());
    file.close();
    SdMan.remove(filePath.c_str());
    return false;
  }

  for (const uint32_t& pos : lut) {
    if (pos == 0) {
      Serial.printf("[%lu] [SCT] createSectionFile: invalid LUT entry (page offset 0) spine=%d — discarding section\n",
                    millis(), spineIndex);
      file.close();
      SdMan.remove(filePath.c_str());
      return false;
    }
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
  if (currentPage < 0 || currentPage >= pageCount) {
    return nullptr;
  }

  if (!file && !SdMan.openFileForRead("SCT", filePath, file)) {
    return nullptr;
  }

  uint32_t pagePos = 0;
  if (currentPage < static_cast<int>(pageOffsets.size())) {
    pagePos = pageOffsets[currentPage];
  } else {
    if (lutOffset == 0) {
      file.seek(HEADER_SIZE - sizeof(uint32_t));
      serialization::readPod(file, lutOffset);
    }
    file.seek(lutOffset + sizeof(uint32_t) * currentPage);
    serialization::readPod(file, pagePos);
  }

  if (pagePos == 0) {
    return nullptr;
  }

  file.seek(pagePos);
  auto page = Page::deserialize(file);

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
