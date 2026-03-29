#include "ChapterHtmlSlimParser.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <expat.h>

#include "../Page.h"
#include "JpegToBmpConverter.h"

const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr int NUM_HEADER_TAGS = sizeof(HEADER_TAGS) / sizeof(HEADER_TAGS[0]);

constexpr size_t MIN_SIZE_FOR_POPUP = 30 * 1024;

const char* BLOCK_TAGS[] = {"p", "li", "div", "br", "blockquote"};
constexpr int NUM_BLOCK_TAGS = sizeof(BLOCK_TAGS) / sizeof(BLOCK_TAGS[0]);

const char* BOLD_TAGS[] = {"b", "strong"};
constexpr int NUM_BOLD_TAGS = sizeof(BOLD_TAGS) / sizeof(BOLD_TAGS[0]);

const char* ITALIC_TAGS[] = {"i", "em"};
constexpr int NUM_ITALIC_TAGS = sizeof(ITALIC_TAGS) / sizeof(ITALIC_TAGS[0]);

const char* IMAGE_TAGS[] = {"img"};
constexpr int NUM_IMAGE_TAGS = sizeof(IMAGE_TAGS) / sizeof(IMAGE_TAGS[0]);

const char* SKIP_TAGS[] = {"head"};
constexpr int NUM_SKIP_TAGS = sizeof(SKIP_TAGS) / sizeof(SKIP_TAGS[0]);

/**
 * Determines if a character is whitespace.
 * 
 * @param c The character to check
 * @return true if the character is space, carriage return, newline, or tab
 */
bool isWhitespace(const char c) {
  return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

/**
 * Checks if a tag name matches any tag in a list of possible tags.
 * 
 * @param tag_name The tag name to check
 * @param possible_tags Array of possible tag names
 * @param possible_tag_count Number of tags in the array
 * @return true if the tag name matches any tag in the list
 */
bool matches(const char* tag_name, const char* possible_tags[], const int possible_tag_count) {
  for (int i = 0; i < possible_tag_count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) return true;
  }
  return false;
}

/**
 * Flushes the current word buffer to the active text block.
 * Determines the appropriate font style based on current bold/italic state.
 */
void ChapterHtmlSlimParser::flushPartWordBuffer() {
  if (partWordBufferIndex == 0) return;

  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if (boldUntilDepth < depth && italicUntilDepth < depth) {
    fontStyle = EpdFontFamily::BOLD_ITALIC;
  } else if (boldUntilDepth < depth) {
    fontStyle = EpdFontFamily::BOLD;
  } else if (italicUntilDepth < depth) {
    fontStyle = EpdFontFamily::ITALIC;
  }

  partWordBuffer[partWordBufferIndex] = '\0';
  currentTextBlock->addWord(partWordBuffer, fontStyle);
  partWordBufferIndex = 0;
}

/**
 * Creates a new text block with the specified style.
 * If there is an existing non-empty text block, it is first converted to pages.
 * 
 * @param style The alignment style for the new text block
 */
void ChapterHtmlSlimParser::startNewTextBlock(const TextBlock::Style style) {
  if (currentTextBlock) {
    if (currentTextBlock->isEmpty()) {
      currentTextBlock->setStyle(style);
      return;
    }
    makePages();
  }
  currentTextBlock.reset(new ParsedText(style, extraParagraphSpacing, hyphenationEnabled));
}

/**
 * XML parser callback for opening element tags.
 * Handles images, headers, block elements, and skip tags.
 * 
 * @param userData Pointer to the ChapterHtmlSlimParser instance
 * @param name The element name
 * @param atts The element attributes
 */
void XMLCALL ChapterHtmlSlimParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  if (matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
    std::string src = "";
    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "src") == 0 || strcmp(atts[i], "href") == 0 || strcmp(atts[i], "xlink:href") == 0) {
          src = atts[i + 1];
          break;
        }
      }
    }

    if (!src.empty()) {
      self->flushPartWordBuffer();

      if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
        self->makePages();
      }

      std::string base = self->internalPath.empty() ? self->filepath : self->internalPath;
      std::string fullInternalPath = FsHelpers::resolveRelativePath(base, src);
      std::string cacheImgPath = self->epub.getCacheImgPath(fullInternalPath);

      int w = 0, h = 0;
      if (self->ensureImageCached(fullInternalPath, cacheImgPath, &w, &h)) {
        self->addImageToPage(cacheImgPath, w, h);
      }
    }
    self->depth += 1;
    return;
  }

  if (self->skipUntilDepth < self->depth) {
    self->depth += 1;
    return;
  }

  if (matches(name, BOLD_TAGS, NUM_BOLD_TAGS)) {
    self->boldUntilDepth = self->depth;
  }
  
  if (matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS)) {
    self->italicUntilDepth = self->depth;
  }

  if (matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
    self->inHeader = true;
    self->startNewTextBlock(TextBlock::CENTER_ALIGN);
  } 
  else if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
    if (strcmp(name, "br") == 0) {
      self->flushPartWordBuffer();
      if (self->currentTextBlock) self->startNewTextBlock(self->currentTextBlock->getStyle());
    } else {
      self->startNewTextBlock(static_cast<TextBlock::Style>(self->paragraphAlignment));
    }
  }

  if (matches(name, SKIP_TAGS, NUM_SKIP_TAGS)) {
    self->skipUntilDepth = self->depth;
  }

  self->depth += 1;
}

/**
 * XML parser callback for character data within elements.
 * Builds words from character data and triggers layout when text block grows large.
 * 
 * @param userData Pointer to the ChapterHtmlSlimParser instance
 * @param s The character data
 * @param len Length of the character data
 */
void XMLCALL ChapterHtmlSlimParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  if (self->skipUntilDepth < self->depth) return;

  for (int i = 0; i < len; i++) {
    if (isWhitespace(s[i])) {
      self->flushPartWordBuffer();
      continue;
    }

    if (s[i] == (XML_Char)0xEF && i + 2 < len && s[i + 1] == (XML_Char)0xBB && s[i + 2] == (XML_Char)0xBF) {
      i += 2;
      continue;
    }

    if (self->partWordBufferIndex >= MAX_WORD_SIZE) self->flushPartWordBuffer();
    self->partWordBuffer[self->partWordBufferIndex++] = s[i];
  }

  if (self->currentTextBlock && self->currentTextBlock->size() > 750) {
    self->currentTextBlock->layoutAndExtractLines(
        self->renderer, self->fontId, self->viewportWidth,
        [self](const std::shared_ptr<TextBlock>& textBlock) { self->addLineToPage(textBlock); }, false);
  }
}

/**
 * XML parser callback for closing element tags.
 * Flushes word buffer and updates depth tracking for bold, italic, and skip states.
 * 
 * @param userData Pointer to the ChapterHtmlSlimParser instance
 * @param name The element name
 */
void XMLCALL ChapterHtmlSlimParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  
  if (self->partWordBufferIndex > 0) {
    if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS) || matches(name, HEADER_TAGS, NUM_HEADER_TAGS) ||
        matches(name, BOLD_TAGS, NUM_BOLD_TAGS) || matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS) || self->depth == 1) {
      self->flushPartWordBuffer();
    }
  }
  
  if (matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      self->makePages();
    }
    self->inHeader = false;
  }
  
  self->depth -= 1;
  
  if (self->skipUntilDepth == self->depth) self->skipUntilDepth = INT_MAX;
  if (self->boldUntilDepth == self->depth) self->boldUntilDepth = INT_MAX;
  if (self->italicUntilDepth == self->depth) self->italicUntilDepth = INT_MAX;
}

/**
 * Reads BMP dimensions from a cached image file.
 * 
 * @param path Path to the BMP file
 * @param w Output parameter for width
 * @param h Output parameter for height
 * @return true if dimensions were successfully read, false otherwise
 */
bool ChapterHtmlSlimParser::getBmpDimensions(const std::string& path, int* w, int* h) {
  FsFile file;
  if (!SdMan.openFileForRead("EHP", path, file)) return false;
  uint8_t header[26];
  if (file.read(header, 26) < 26) {
    file.close();
    return false;
  }
  *w = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
  *h = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
  if (*h < 0) *h = -(*h);
  file.close();
  return (*w > 0 && *h > 0);
}

/**
 * Adds a text line or header to the current page.
 * Handles page breaking if the line doesn't fit on the current page.
 * Uses PageHeader for header text and PageLine for normal text.
 * 
 * @param line The text block line to add
 */
void ChapterHtmlSlimParser::addLineToPage(std::shared_ptr<TextBlock> line) {
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  
  if (!line || line->isEmpty()) {
    return;
  }
  
  if (currentPageNextY + lineHeight > viewportHeight) {
    if (currentPage && !currentPage->elements.empty()) {
      completePageFn(std::move(currentPage));
    }
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  if (!currentPage) {
    currentPage.reset(new Page());
  }

  // Use PageHeader for header text, PageLine for normal text
  if (inHeader) {
    // Use the header font ID passed from the activity
    currentPage->elements.push_back(std::make_shared<PageHeader>(line, 0, currentPageNextY, headerFontId));
  } else {
    currentPage->elements.push_back(std::make_shared<PageLine>(line, 0, currentPageNextY));
  }
  
  currentPageNextY += lineHeight;
}

/**
 * Converts the current text block into pages by extracting lines
 * and adding them to the page layout.
 */
void ChapterHtmlSlimParser::makePages() {
  if (!currentTextBlock) return;
  
  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }
  
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  
  currentTextBlock->layoutAndExtractLines(
      renderer, inHeader ? headerFontId : fontId, viewportWidth,
      [this](const std::shared_ptr<TextBlock>& textBlock) { 
        addLineToPage(textBlock); 
      });
  
  if (extraParagraphSpacing) {
    currentPageNextY += lineHeight / 2;
  }
}

/**
 * Ensures an image is cached as BMP format and returns its dimensions.
 * If skipImages is true and image is not already cached, returns false without converting.
 * 
 * @param internalPath Path to the original image in the EPUB
 * @param cacheImgPath Path where the cached BMP should be stored
 * @param w Output parameter for image width
 * @param h Output parameter for image height
 * @return true if image is available in cache and dimensions were read, false otherwise
 */
bool ChapterHtmlSlimParser::ensureImageCached(const std::string& internalPath, const std::string& cacheImgPath, int* w,
                                              int* h) {
  if (SdMan.exists(cacheImgPath.c_str())) {
    return getBmpDimensions(cacheImgPath, w, h);
  }
  
  if (skipImages) {
    return false;
  }


  bool result = epub.extractAndConvertImage(internalPath, cacheImgPath, viewportWidth, 0);


  if (result) {
    return getBmpDimensions(cacheImgPath, w, h);
  }

  return false;
}

/**
 * Adds an image to the current page layout.
 * Handles scaling, centering, and special page breaking for extra-large images.
 * 
 * @param bmpPath Path to the cached BMP image
 * @param imgW Original image width
 * @param imgH Original image height
 */
void ChapterHtmlSlimParser::addImageToPage(const std::string& bmpPath, int imgW, int imgH) {
  float scale = (float)viewportWidth / (float)imgW;
  if (scale > 1.0f) scale = 1.0f;

  int dW = (int)(imgW * scale);
  int dH = (int)(imgH * scale);

  bool isExtraLarge = (dW > viewportWidth * 0.9 && dH > viewportHeight * 0.7);

  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    makePages();
  }

  if (isExtraLarge) {
    if (currentPage && !currentPage->elements.empty()) {
      completePageFn(std::move(currentPage));
      currentPageNextY = 0;
    }
    
    currentPage.reset(new Page());
    currentPageNextY = 0;
    
    int xPos = (dW < viewportWidth) ? (viewportWidth - dW) / 2 : 0;
    currentPage->elements.push_back(std::make_shared<PageImage>(bmpPath, dW, dH, xPos, 0));
    
    currentPageNextY = dH + (renderer.getLineHeight(fontId) / 2);
    
    int remainingSpace = viewportHeight - currentPageNextY;
    int minTextHeight = renderer.getLineHeight(fontId) * lineCompression * 2;
    
    if (remainingSpace < minTextHeight) {
      completePageFn(std::move(currentPage));
      currentPage.reset(new Page());
      currentPageNextY = 0;
    }
    
    return;
  }

  if (currentPageNextY + dH > viewportHeight) {
    if (currentPage && !currentPage->elements.empty()) {
      completePageFn(std::move(currentPage));
    }
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  if (!currentPage) {
    currentPage.reset(new Page());
  }

  int xPos = (dW < viewportWidth) ? (viewportWidth - dW) / 2 : 0;

  currentPage->elements.push_back(std::make_shared<PageImage>(bmpPath, dW, dH, xPos, currentPageNextY));
  
  currentPageNextY += dH + (renderer.getLineHeight(fontId) / 2);
}

/**
 * Parses the HTML file and builds pages.
 * When skipImageProcessing is true, only processes text and uses existing cached images
 * without converting new ones. Images that aren't already cached will be skipped.
 * 
 * @param skipImageProcessing If true, skip converting new images and only process text
 * @return true if parsing was successful, false otherwise
 */
bool ChapterHtmlSlimParser::parseAndBuildPages(bool skipImageProcessing) {
  skipImages = skipImageProcessing;
  
  startNewTextBlock((TextBlock::Style)this->paragraphAlignment);
  const XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) return false;

  FsFile file;
  if (!SdMan.openFileForRead("EHP", filepath, file)) {
    XML_ParserFree(parser);
    return false;
  }

  if (popupFn && file.size() >= MIN_SIZE_FOR_POPUP) popupFn();

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);

  int done;
  do {
    void* const buf = XML_GetBuffer(parser, 1024);
    if (!buf) break;
    const size_t len = file.read(buf, 1024);
    done = (len == 0);
    if (XML_ParseBuffer(parser, static_cast<int>(len), done) == XML_STATUS_ERROR) break;
  } while (!done);

  XML_ParserFree(parser);
  file.close();

  flushPartWordBuffer();
  
  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    makePages();
  }
  
  if (currentPage && !currentPage->elements.empty()) {
    completePageFn(std::move(currentPage));
  }
  
  return true;
}