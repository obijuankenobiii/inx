#pragma once

#include <expat.h>

#include <climits>
#include <functional>
#include <memory>
#include <string>

#include "../../Epub.h"
#include "../ParsedText.h"
#include "../blocks/TextBlock.h"

class Page;
class GfxRenderer;

#define MAX_WORD_SIZE 200

/**
 * Parser for HTML chapter files that builds pages with text and images.
 * Handles XML parsing, text layout, and image processing for EPUB chapters.
 */
class ChapterHtmlSlimParser {
 private:
  const std::string& filepath;
  const Epub& epub;
  const std::string cachePath;
  const std::string contentBasePath;

  GfxRenderer& renderer;
  std::function<void(std::unique_ptr<Page>)> completePageFn;
  std::function<void()> popupFn;

  int depth = 0;
  int skipUntilDepth = INT_MAX;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;

  // Font tracking
  int fontId;
  int headerFontId;
  int maxFontId;

  // Header tracking
  bool inHeader = false;

  // Drop Cap tracking
  bool inDropCap = false;
  int dropCapDepth = INT_MAX;

  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;

  float lineCompression;
  bool extraParagraphSpacing;
  uint8_t paragraphAlignment;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  bool hyphenationEnabled;

  bool skipImages = false;

  /**
   * Creates a new text block with the specified style.
   */
  void startNewTextBlock(TextBlock::Style style);

  /**
   * Flushes the accumulated word buffer.
   * Uses headerFontId if inDropCap is true.
   */
  void flushPartWordBuffer();

  /**
   * Converts the current text block into page lines.
   */
  void makePages();

  /**
   * Adds a single text line to the current page.
   */
  void addLineToPage(std::shared_ptr<TextBlock> line);

  /**
   * Adds an image to the current page layout.
   */
  void addImageToPage(const std::string& bmpPath, int imgW, int imgH);

  /**
   * Ensures an image is cached as BMP format.
   */
  bool ensureImageCached(const std::string& internalPath, const std::string& cacheImgPath, int* w, int* h);

  /**
   * Reads BMP dimensions from a cached image file.
   */
  bool getBmpDimensions(const std::string& path, int* w, int* h);

  // XML parser callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  static void XMLCALL endElement(void* userData, const XML_Char* name);

 public:
  std::string internalPath;

  /**
   * Constructs a new HTML parser for a chapter.
   * Note: headerFontId is used for both <h> tags and drop cap <span> tags.
   */
  explicit ChapterHtmlSlimParser(const std::string& filepath, const Epub& epub, const std::string& cachePath,
                                 const std::string& contentBasePath, GfxRenderer& renderer, const int fontId,
                                 const int headerFontId,const int maxFontId, const float lineCompression, const bool extraParagraphSpacing,
                                 const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                 const uint16_t viewportHeight, const bool hyphenationEnabled,
                                 const std::function<void(std::unique_ptr<Page>)>& completePageFn,
                                 const std::function<void()>& popupFn = nullptr)
      : filepath(filepath),
        epub(epub),
        cachePath(cachePath),
        contentBasePath(contentBasePath),
        renderer(renderer),
        fontId(fontId),
        headerFontId(headerFontId),
        maxFontId(maxFontId),
        lineCompression(lineCompression),
        extraParagraphSpacing(extraParagraphSpacing),
        paragraphAlignment(paragraphAlignment),
        viewportWidth(viewportWidth),
        viewportHeight(viewportHeight),
        hyphenationEnabled(hyphenationEnabled),
        completePageFn(completePageFn),
        popupFn(popupFn) {}

  ~ChapterHtmlSlimParser() = default;

  /**
   * Parses the HTML file and builds pages.
   */
  bool parseAndBuildPages(bool skipImageProcessing = false);
};