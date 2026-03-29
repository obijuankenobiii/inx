#pragma once

#include <expat.h>
#include <climits>
#include <functional>
#include <memory>
#include <string>

#include "../ParsedText.h"
#include "../blocks/TextBlock.h"
#include "../../Epub.h"

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
  
  // Header tracking
  bool inHeader = false;
  int headerFontId;  // Font ID to use for headers
  
  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;
  
  int fontId;
  float lineCompression;
  bool extraParagraphSpacing;
  uint8_t paragraphAlignment;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  bool hyphenationEnabled;
  
  bool skipImages = false;

  /**
   * Creates a new text block with the specified style.
   * Converts existing text block to pages if it contains content.
   * 
   * @param style The alignment style for the new text block
   */
  void startNewTextBlock(TextBlock::Style style);

  /**
   * Flushes the accumulated word buffer to the current text block.
   * Applies appropriate font style based on current formatting state.
   */
  void flushPartWordBuffer();

  /**
   * Converts the current text block into page lines.
   * Extracts lines based on viewport width and adds them to the current page.
   */
  void makePages();

  /**
   * Adds a single text line to the current page.
   * Handles page breaking when the line exceeds available space.
   * 
   * @param line The text block line to add
   */
  void addLineToPage(std::shared_ptr<TextBlock> line);
  
  /**
   * Adds an image to the current page layout.
   * Handles scaling, centering, and special handling for extra-large images.
   * 
   * @param bmpPath Path to the cached BMP image
   * @param imgW Original image width
   * @param imgH Original image height
   */
  void addImageToPage(const std::string& bmpPath, int imgW, int imgH);

  /**
   * Ensures an image is cached as BMP format.
   * If skipImages is true, only returns true for already-cached images.
   * 
   * @param internalPath Original image path within EPUB
   * @param cacheImgPath Target path for cached BMP
   * @param w Output parameter for image width
   * @param h Output parameter for image height
   * @return true if image is available in cache
   */
  bool ensureImageCached(const std::string& internalPath, const std::string& cacheImgPath, int* w, int* h);

  /**
   * Reads BMP dimensions from a cached image file.
   * 
   * @param path Path to the BMP file
   * @param w Output parameter for width
   * @param h Output parameter for height
   * @return true if dimensions were successfully read
   */
  bool getBmpDimensions(const std::string& path, int* w, int* h);

  /**
   * XML parser callback for opening element tags.
   * 
   * @param userData Pointer to the parser instance
   * @param name Element name
   * @param atts Element attributes
   */
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);

  /**
   * XML parser callback for character data.
   * 
   * @param userData Pointer to the parser instance
   * @param s Character data
   * @param len Length of character data
   */
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);

  /**
   * XML parser callback for closing element tags.
   * 
   * @param userData Pointer to the parser instance
   * @param name Element name
   */
  static void XMLCALL endElement(void* userData, const XML_Char* name);

 public:
  std::string internalPath;

  /**
   * Constructs a new HTML parser for a chapter.
   * 
   * @param filepath Path to the HTML file
   * @param epub Reference to the EPUB container
   * @param cachePath Path for cached files
   * @param contentBasePath Base path for content resolution
   * @param renderer Graphics renderer for layout calculations
   * @param fontId Font identifier for text rendering
   * @param headerFontId Font identifier for header rendering
   * @param lineCompression Line spacing factor
   * @param extraParagraphSpacing Whether to add extra spacing between paragraphs
   * @param paragraphAlignment Default paragraph alignment
   * @param viewportWidth Available width for layout
   * @param viewportHeight Available height for layout
   * @param hyphenationEnabled Whether hyphenation is enabled
   * @param completePageFn Callback for completed pages
   * @param popupFn Optional callback for progress popups
   */
  explicit ChapterHtmlSlimParser(const std::string& filepath, const Epub& epub, 
                                 const std::string& cachePath, const std::string& contentBasePath,
                                 GfxRenderer& renderer, const int fontId, const int headerFontId,
                                 const float lineCompression, const bool extraParagraphSpacing,
                                 const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                 const uint16_t viewportHeight, const bool hyphenationEnabled,
                                 const std::function<void(std::unique_ptr<Page>)>& completePageFn,
                                 const std::function<void()>& popupFn = nullptr)
      : filepath(filepath), epub(epub), cachePath(cachePath), contentBasePath(contentBasePath),
        renderer(renderer), fontId(fontId), headerFontId(headerFontId), lineCompression(lineCompression),
        extraParagraphSpacing(extraParagraphSpacing), paragraphAlignment(paragraphAlignment),
        viewportWidth(viewportWidth), viewportHeight(viewportHeight),
        hyphenationEnabled(hyphenationEnabled), completePageFn(completePageFn),
        popupFn(popupFn) {}
        
  ~ChapterHtmlSlimParser() = default;

  /**
   * Parses the HTML file and builds pages.
   * When skipImageProcessing is true, only processes text and uses existing cached images
   * without converting new ones. Images that aren't already cached will be skipped.
   * 
   * @param skipImageProcessing If true, skip converting new images and only process text
   * @return true if parsing was successful, false otherwise
   */
  bool parseAndBuildPages(bool skipImageProcessing = false);
};