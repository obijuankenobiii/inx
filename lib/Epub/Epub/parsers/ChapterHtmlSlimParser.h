#pragma once

/**
 * @file ChapterHtmlSlimParser.h
 * @brief Public interface and types for ChapterHtmlSlimParser.
 */

#include <expat.h>

#include <climits>
#include <functional>
#include <memory>
#include <string>

#include "../../Epub.h"
#include "../ParsedText.h"
#include "../blocks/TextBlock.h"
#include "CssParser.h"

class Page;
class GfxRenderer;

#define MAX_WORD_SIZE 200

/**
 * Reader paragraph alignment: 0–3 match TextBlock::Style; 4 = follow CSS text-align per block.
 * Must stay in sync with SystemSetting::PARAGRAPH_ALIGNMENT (see src/state/SystemSetting.h).
 */
constexpr uint8_t EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS = 4;

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

  
  int fontId;
  int headerFontId;
  int maxFontId;

  
  bool inHeader = false;

  
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
  /** Book/global "Indent": honor CSS `text-indent` when true (from paragraphCssIndentEnabled). */
  bool respectCssParagraphIndent = false;

  bool skipImages = false;

  /** After cold image extract, yield occasionally so heap can consolidate (ZIP + converters). */
  unsigned imageExtractCountForYield_ = 0;

  CssParser cssParser;
  bool cssLoaded;

  /** When true, Expat callbacks only walk the tree for depth/skip and prefetch images (no text layout). */
  bool imagePrefetchPassOnly_ = false;

  void resetStructuralStateForParsePass();

  void prefetchImageFromImgAttributes(const XML_Char** atts);

  bool parseHtmlThroughExpat(bool callProgressPopup);

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

  /** Reads cached image dimensions (BMP or JPEG). */
  bool getImageDimensions(const std::string& path, int* w, int* h);

  /**
   * Loads all CSS rules from the EPUB cache using CssParser.
   */
  void loadCssRules();

  /** Resolves text-align for the current block element when paragraph alignment is FOLLOW_CSS. */
  TextBlock::Style resolveTextAlignFromAttributes(const XML_Char* elementName, const XML_Char** atts) const;

  /**
   * Processes an img element with CSS class support.
   */
  void processImageElement(const char** atts);

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  /** Expanded default text / entities (e.g. &nbsp;) — forwards to characterData (Crosspoint-style). */
  static void XMLCALL defaultHandlerExpand(void* userData, const XML_Char* s, int len);
  static void XMLCALL endElement(void* userData, const XML_Char* name);

 public:
  std::string internalPath;

  /**
   * Constructs a new HTML parser for a chapter.
   * Note: headerFontId is used for both <h> tags and drop cap <span> tags.
   */
  explicit ChapterHtmlSlimParser(const std::string& filepath, const Epub& epub, const std::string& cachePath,
                                 const std::string& contentBasePath, GfxRenderer& renderer, const int fontId,
                                 const int headerFontId, const int maxFontId, const float lineCompression,
                                 const bool extraParagraphSpacing, const uint8_t paragraphAlignment,
                                 const uint16_t viewportWidth, const uint16_t viewportHeight,
                                 const bool hyphenationEnabled, const bool respectCssParagraphIndent,
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
        respectCssParagraphIndent(respectCssParagraphIndent),
        completePageFn(completePageFn),
        popupFn(popupFn),
        cssLoaded(false) {}

  ~ChapterHtmlSlimParser() = default;

  /**
   * Parses the HTML file and builds pages.
   * When skipImageProcessing is false: unloads SD streaming fonts, runs a lightweight first pass that only
   * extracts & caches images (ZIP/inflate without SD font heap), restores reader fonts via ensureReaderLayoutFonts,
   * then runs the full layout pass (cached BMPs, text, CSS).
   * When skipImageProcessing is true, only one pass runs and new ZIP→BMP work is skipped.
   */
  bool parseAndBuildPages(bool skipImageProcessing = false);
};