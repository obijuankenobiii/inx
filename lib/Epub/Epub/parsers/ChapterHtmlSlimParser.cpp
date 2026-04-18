#include "ChapterHtmlSlimParser.h"

#include <algorithm>
#include <cctype>
#include <cstring>

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

const char* BLOCK_TAGS[] = {"p", "li", "div", "br", "blockquote", "tr", "table"};
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
bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

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
 * Loads all CSS rules from the EPUB cache using CssParser
 */
void ChapterHtmlSlimParser::loadCssRules() {
  if (cssLoaded) return;

  cssParser.clear();

  // Get all CSS files from the EPUB
  int cssCount = epub.getCssItemsCount();
  if (cssCount > 0) {
    Serial.printf("[EHP] Loading %d CSS files (image width/height only)\n", cssCount);

    const size_t MAX_TOTAL_CSS_SIZE = 14 * 1024;
    size_t totalCssSize = 0;

    constexpr size_t kCssZipCap = 8 * 1024;
    for (int i = 0; i < cssCount && totalCssSize < MAX_TOTAL_CSS_SIZE; i++) {
      const auto cssEntry = epub.getCssItem(i);
      if (cssEntry.path.empty()) {
        continue;
      }

      std::string cssBlob;
      if (!epub.readInternalTextCapped(cssEntry.path, cssBlob, kCssZipCap)) {
        Serial.printf("[EHP] Could not read CSS from zip: %s\n", cssEntry.path.c_str());
        continue;
      }

      totalCssSize += cssBlob.size();
      cssParser.parse(cssBlob);
      cssBlob.clear();
      cssBlob.shrink_to_fit();

      Serial.printf("[EHP] Parsed CSS from zip: %s (cap %zu, total bytes: %d)\n", cssEntry.path.c_str(), kCssZipCap,
                    (int)totalCssSize);
    }

    Serial.printf("[EHP] Loaded %zu CSS rules from %d bytes\n", cssParser.getRuleCount(), (int)totalCssSize);
    cssParser.shrinkStorage();
  }

  cssLoaded = true;
}

/**
 * Processes an img element with CSS class support
 */
void ChapterHtmlSlimParser::processImageElement(const char** atts) {
  std::string src = "";
  std::string classAttr = "";
  std::string styleAttr = "";
  std::string idAttr = "";
  int explicitWidth = 0;
  int explicitHeight = 0;

  // Parse attributes
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      std::string attrName = atts[i];
      std::string attrValue = atts[i + 1];

      if (attrName == "src" || attrName == "href" || attrName == "xlink:href") {
        src = attrValue;
      } else if (attrName == "class") {
        classAttr = attrValue;
      } else if (attrName == "style") {
        styleAttr = attrValue;
      } else if (attrName == "id") {
        idAttr = attrValue;
      } else if (attrName == "width") {
        explicitWidth = cssParser.parseCssLength(attrValue, viewportWidth, viewportHeight, true);
      } else if (attrName == "height") {
        explicitHeight = cssParser.parseCssLength(attrValue, viewportWidth, viewportHeight, false);
      }
    }
  }

  if (src.empty()) return;

  // Load CSS rules if not already loaded
  loadCssRules();

  // Determine dimensions (priority: explicit > inline style > CSS class)
  int imgWidth = explicitWidth;
  int imgHeight = explicitHeight;

  bool widthIsPercentage = false;
  bool heightIsPercentage = false;

  // Check inline style and CSS if explicit dimensions not provided
  if (imgWidth == 0 || imgHeight == 0) {
    // Check for percentage values in inline style
    if (!styleAttr.empty()) {
      // Check if width is percentage
      size_t widthPos = styleAttr.find("width:");
      if (widthPos != std::string::npos) {
        size_t percentPos = styleAttr.find("%", widthPos);
        if (percentPos != std::string::npos) {
          widthIsPercentage = true;
        }
      }

      // Check if height is percentage
      size_t heightPos = styleAttr.find("height:");
      if (heightPos != std::string::npos) {
        size_t percentPos = styleAttr.find("%", heightPos);
        if (percentPos != std::string::npos) {
          heightIsPercentage = true;
        }
      }
    }

    // Use CssParser to get dimensions from inline style and CSS rules
    if (imgWidth == 0) {
      int cssWidth = cssParser.getWidth(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
      // If CSS returned 0 but we have a percentage flag, keep it as 0 to trigger aspect ratio
      if (cssWidth == 0 && !widthIsPercentage) {
        imgWidth = cssWidth;
      } else if (cssWidth > 0) {
        imgWidth = cssWidth;
      }
    }

    if (imgHeight == 0) {
      int cssHeight = cssParser.getHeight(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
      if (cssHeight == 0 && !heightIsPercentage) {
        imgHeight = cssHeight;
      } else if (cssHeight > 0) {
        imgHeight = cssHeight;
      }
    }
  }

  // Resolve full path and cache image
  std::string base = internalPath.empty() ? filepath : internalPath;
  std::string fullInternalPath = FsHelpers::resolveRelativePath(base, src);
  std::string cacheImgPath = epub.getCacheImgPath(fullInternalPath);

  int actualW = 0, actualH = 0;
  if (ensureImageCached(fullInternalPath, cacheImgPath, &actualW, &actualH)) {
    const int cssMaxW = cssParser.getMaxWidth(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
    const int cssMinW = cssParser.getMinWidth(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
    const int cssMaxH = cssParser.getMaxHeight(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
    const int cssMinH = cssParser.getMinHeight(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);

    if (imgWidth == 0 && cssMaxW > 0) {
      imgWidth = std::min(actualW, cssMaxW);
      imgHeight = (actualH * imgWidth) / std::max(1, actualW);
    }
    if (imgHeight == 0 && cssMaxH > 0) {
      imgHeight = std::min(actualH, cssMaxH);
      imgWidth = (actualW * imgHeight) / std::max(1, actualH);
    }

    // Apply dimension constraints and maintain aspect ratio
    if (widthIsPercentage || heightIsPercentage) {
      // Handle percentage-based dimensions
      if (widthIsPercentage && heightIsPercentage) {
        // Both are percentages - use actual image size as fallback
        if (imgWidth == 0 && imgHeight == 0) {
          imgWidth = actualW;
          imgHeight = actualH;
        } else if (imgWidth == 0 && imgHeight > 0) {
          imgWidth = (actualW * imgHeight) / actualH;
        } else if (imgHeight == 0 && imgWidth > 0) {
          imgHeight = (actualH * imgWidth) / actualW;
        }
      } else if (widthIsPercentage && imgWidth == 0) {
        // Width is percentage, use height to determine width
        if (imgHeight > 0) {
          imgWidth = (actualW * imgHeight) / actualH;
        } else {
          imgWidth = actualW;
          imgHeight = actualH;
        }
      } else if (heightIsPercentage && imgHeight == 0) {
        // Height is percentage, use width to determine height
        if (imgWidth > 0) {
          imgHeight = (actualH * imgWidth) / actualW;
        } else {
          imgWidth = actualW;
          imgHeight = actualH;
        }
      }
    } else {
      // Handle fixed dimensions
      if (imgWidth > 0 && imgHeight == 0) {
        // Height not specified, maintain aspect ratio based on width
        imgHeight = (actualH * imgWidth) / std::max(1, actualW);
      } else if (imgHeight > 0 && imgWidth == 0) {
        // Width not specified, maintain aspect ratio based on height
        imgWidth = (actualW * imgHeight) / std::max(1, actualH);
      } else if (imgWidth == 0 && imgHeight == 0) {
        // No dimensions specified, use actual image size
        imgWidth = actualW;
        imgHeight = actualH;
      }
    }

    if (cssMaxW > 0 && imgWidth > cssMaxW) {
      imgHeight = (imgHeight * cssMaxW) / std::max(1, imgWidth);
      imgWidth = cssMaxW;
    }
    if (cssMaxH > 0 && imgHeight > cssMaxH) {
      imgWidth = (imgWidth * cssMaxH) / std::max(1, imgHeight);
      imgHeight = cssMaxH;
    }
    if (cssMinW > 0 && imgWidth < cssMinW) {
      imgHeight = (imgHeight * cssMinW) / std::max(1, imgWidth);
      imgWidth = cssMinW;
    }
    if (cssMinH > 0 && imgHeight < cssMinH) {
      imgWidth = (imgWidth * cssMinH) / std::max(1, imgHeight);
      imgHeight = cssMinH;
    }

    // Cap at viewport size to prevent overflow
    if (imgWidth > viewportWidth) {
      imgHeight = (imgHeight * viewportWidth) / imgWidth;
      imgWidth = viewportWidth;
    }

    if (imgHeight > viewportHeight) {
      imgWidth = (imgWidth * viewportHeight) / imgHeight;
      imgHeight = viewportHeight;
    }

    if (imgWidth < 1) imgWidth = 1;
    if (imgHeight < 1) imgHeight = 1;

    Serial.printf("[EHP] Image %s - CSS: %s, Final: %dx%d (actual: %dx%d, percent: w=%d h=%d)\n", src.c_str(),
                  styleAttr.c_str(), imgWidth, imgHeight, actualW, actualH, widthIsPercentage, heightIsPercentage);

    addImageToPage(cacheImgPath, imgWidth, imgHeight);
  }
}

/**
 * Flushes the current word buffer to the active text block.
 * Determines the appropriate font style based on current bold/italic state.
 */
void ChapterHtmlSlimParser::flushPartWordBuffer() {
  if (partWordBufferIndex == 0) return;
  partWordBuffer[partWordBufferIndex] = '\0';

  if (inDropCap) {
    if (!currentPage) currentPage.reset(new Page());

    auto dropCapElem = std::make_shared<PageDropCap>(partWordBuffer, 0, currentPageNextY, maxFontId);
    currentPage->elements.push_back(dropCapElem);

    const int gutter = std::max(renderer.getSpaceWidth(fontId), 10);
    int dropCapWidth = renderer.getTextWidth(maxFontId, partWordBuffer, EpdFontFamily::BOLD) + gutter;

    if (currentTextBlock) {
      currentTextBlock->setLeftIndent(dropCapWidth, 3);
    }

    partWordBufferIndex = 0;
    inDropCap = false;
    return;
  }

  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if (boldUntilDepth < depth && italicUntilDepth < depth) {
    fontStyle = EpdFontFamily::BOLD_ITALIC;
  } else if (boldUntilDepth < depth) {
    fontStyle = EpdFontFamily::BOLD;
  } else if (italicUntilDepth < depth) {
    fontStyle = EpdFontFamily::ITALIC;
  }

  currentTextBlock->addWord(partWordBuffer, fontStyle);
  partWordBufferIndex = 0;
}

/**
 * Creates a new text block with the specified style.
 * If there is an existing non-empty text block, it is first converted to pages.
 *
 * @param style The alignment style for the new text block
 */
TextBlock::Style ChapterHtmlSlimParser::resolveTextAlignFromAttributes(const XML_Char* elementName,
                                                                       const XML_Char** atts) const {
  std::string classAttr;
  std::string idAttr;
  std::string styleAttr;
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "class") == 0) {
        classAttr = atts[i + 1];
      } else if (strcmp(atts[i], "id") == 0) {
        idAttr = atts[i + 1];
      } else if (strcmp(atts[i], "style") == 0) {
        styleAttr = atts[i + 1];
      }
    }
  }
  std::string tagLower;
  if (elementName != nullptr) {
    for (const XML_Char* p = elementName; *p; ++p) {
      tagLower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
  }
  return static_cast<TextBlock::Style>(cssParser.computeParagraphAlignment(classAttr, idAttr, styleAttr, tagLower));
}

void ChapterHtmlSlimParser::startNewTextBlock(TextBlock::Style style) {
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
 * @param userData Pointer to the parser instance
 * @param name Element name
 * @param atts Element attributes
 */
void XMLCALL ChapterHtmlSlimParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Check for dropcap class
  if ((strcmp(name, "span") == 0 || strcmp(name, "p") == 0) && atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "class") == 0 && strstr(atts[i + 1], "dropcap") != nullptr) {
        self->flushPartWordBuffer();
        self->inDropCap = true;
        self->dropCapDepth = self->depth;
        break;
      }
    }
  }

  // Handle image tags with CSS dimension extraction
  if (matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
    self->processImageElement(atts);
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
  } else if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
    if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
      self->currentTextBlock->addWord("\xe2\x80\x83\xe2\x80\x83", EpdFontFamily::REGULAR);
    }

    if (strcmp(name, "br") == 0) {
      self->flushPartWordBuffer();
      if (self->currentTextBlock) self->startNewTextBlock(self->currentTextBlock->getStyle());
    } else {
      TextBlock::Style blockStyle;
      std::string tagLower;
      if (name != nullptr) {
        for (const XML_Char* p = name; *p; ++p) {
          tagLower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
        }
      }
      if (self->paragraphAlignment == EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS) {
        blockStyle = self->resolveTextAlignFromAttributes(name, atts);
      } else {
        blockStyle = static_cast<TextBlock::Style>(self->paragraphAlignment);
      }
      self->startNewTextBlock(blockStyle);
      if (self->paragraphAlignment == EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS && self->currentTextBlock) {
        std::string classAttr;
        std::string idAttr;
        std::string styleAttr;
        if (atts != nullptr) {
          for (int i = 0; atts[i]; i += 2) {
            if (strcmp(atts[i], "class") == 0) {
              classAttr = atts[i + 1];
            } else if (strcmp(atts[i], "id") == 0) {
              idAttr = atts[i + 1];
            } else if (strcmp(atts[i], "style") == 0) {
              styleAttr = atts[i + 1];
            }
          }
        }
        if (self->cssParser.hasTextIndentSpecified(tagLower, classAttr, idAttr, styleAttr)) {
          const int tip = self->cssParser.getTextIndentPx(tagLower, classAttr, idAttr, styleAttr, self->viewportWidth,
                                                          self->viewportHeight);
          self->currentTextBlock->setCssTextIndentFromCascade(tip);
        }
      }
    }
  }

  if (matches(name, SKIP_TAGS, NUM_SKIP_TAGS)) {
    self->skipUntilDepth = self->depth;
  }

  self->depth += 1;
}

/**
 * XML parser callback for character data.
 * @param userData Pointer to the parser instance
 * @param s Character data
 * @param len Length of character data
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

    if (self->inDropCap) self->flushPartWordBuffer();
  }

  if (self->currentTextBlock && self->currentTextBlock->size() > 750) {
    self->currentTextBlock->layoutAndExtractLines(
        self->renderer, self->fontId, self->viewportWidth,
        [self](const std::shared_ptr<TextBlock>& textBlock) { self->addLineToPage(textBlock); }, false);
  }
}

/**
 * XML parser callback for closing element tags.
 * @param userData Pointer to the parser instance
 * @param name Element name
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

  if (self->inDropCap && self->dropCapDepth == self->depth) {
    self->inDropCap = false;
    self->dropCapDepth = INT_MAX;
  }
}

/**
 * Reads BMP dimensions from a cached image file.
 * @param path Path to the BMP file
 * @param w Output parameter for width
 * @param h Output parameter for height
 * @return true if dimensions were successfully read
 */
bool ChapterHtmlSlimParser::getBmpDimensions(const std::string& path, int* w, int* h) {
  FsFile file;
  if (!SdMan.openFileForRead("EHP", path, file)) return false;

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() == BmpReaderError::Ok) {
    *w = bitmap.getWidth();
    *h = bitmap.getHeight();
  }

  file.close();
  return (*w > 0 && *h > 0);
}

/**
 * Adds a single text line to the current page.
 * Handles page breaking when the line exceeds available space.
 * @param line The text block line to add
 */
void ChapterHtmlSlimParser::addLineToPage(std::shared_ptr<TextBlock> line) {
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;

  if (!line || line->isEmpty()) return;

  if (currentPageNextY + lineHeight > viewportHeight) {
    if (currentPage && !currentPage->elements.empty()) completePageFn(std::move(currentPage));
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  if (!currentPage) currentPage.reset(new Page());

  if (inHeader) {
    currentPage->elements.push_back(std::make_shared<PageHeader>(line, 0, currentPageNextY, headerFontId));
  } else {
    currentPage->elements.push_back(std::make_shared<PageLine>(line, 0, currentPageNextY));
  }

  currentPageNextY += lineHeight;
}

/**
 * Converts the current text block into page lines.
 * Extracts lines based on viewport width and adds them to the current page.
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
      [this](const std::shared_ptr<TextBlock>& textBlock) { addLineToPage(textBlock); });

  if (extraParagraphSpacing) {
    currentPageNextY += lineHeight / 2;
  }
}

/**
 * Ensures an image is cached as BMP format.
 * If skipImages is true, only returns true for already-cached images.
 * @param internalPath Original image path within EPUB
 * @param cacheImgPath Target path for cached BMP
 * @param w Output parameter for image width
 * @param h Output parameter for image height
 * @return true if image is available in cache
 */
bool ChapterHtmlSlimParser::ensureImageCached(const std::string& internalPath, const std::string& cacheImgPath, int* w,
                                              int* h) {
  if (SdMan.exists(cacheImgPath.c_str())) {
    return getBmpDimensions(cacheImgPath, w, h);
  }

  if (skipImages) return false;

  bool result = epub.extractAndConvertImage(internalPath, cacheImgPath, viewportWidth, 0);

  if (result) return getBmpDimensions(cacheImgPath, w, h);

  return false;
}

/**
 * Adds an image to the current page layout.
 * Handles scaling, centering, and special handling for extra-large images.
 * @param bmpPath Path to the cached BMP image
 * @param imgW Original image width
 * @param imgH Original image height
 */
void ChapterHtmlSlimParser::addImageToPage(const std::string& bmpPath, int imgW, int imgH) {
  bool isExtraLarge = (imgW >= viewportWidth * 0.95 && imgH >= viewportHeight * 0.65);

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
    currentPage->elements.push_back(std::make_shared<PageImage>(bmpPath, imgW, imgH, 0, 0));

    currentPageNextY = imgH + (renderer.getLineHeight(fontId) / 2);
    int remainingSpace = viewportHeight - currentPageNextY;
    int minTextHeight = renderer.getLineHeight(fontId) * lineCompression * 2;

    if (remainingSpace < minTextHeight) {
      completePageFn(std::move(currentPage));
      currentPage.reset(new Page());
      currentPageNextY = 0;
    }

    return;
  }

  if (currentPageNextY + imgH > viewportHeight) {
    if (currentPage && !currentPage->elements.empty()) {
      completePageFn(std::move(currentPage));
    }
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  if (!currentPage) {
    currentPage.reset(new Page());
  }

  int xPos = (imgW < viewportWidth) ? (viewportWidth - imgW) / 2 : 0;
  currentPage->elements.push_back(std::make_shared<PageImage>(bmpPath, imgW, imgH, xPos, currentPageNextY));

  currentPageNextY += imgH + (renderer.getLineHeight(fontId) / 2);
}

/**
 * Parses the HTML file and builds pages.
 * When skipImageProcessing is true, only processes text and uses existing cached images
 * without converting new ones. Images that aren't already cached will be skipped.
 * @param skipImageProcessing If true, skip converting new images and only process text
 * @return true if parsing was successful, false otherwise
 */
bool ChapterHtmlSlimParser::parseAndBuildPages(bool skipImageProcessing) {
  skipImages = skipImageProcessing;
  inDropCap = false;
  dropCapDepth = INT_MAX;
  cssLoaded = false;  // Reset CSS loaded flag for new chapter

  loadCssRules();

  TextBlock::Style initialBlockStyle = TextBlock::LEFT_ALIGN;
  if (paragraphAlignment <= 3) {
    initialBlockStyle = static_cast<TextBlock::Style>(paragraphAlignment);
  } else if (paragraphAlignment == EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS) {
    initialBlockStyle = static_cast<TextBlock::Style>(cssParser.computeParagraphAlignment("", "", "", "body"));
  }
  startNewTextBlock(initialBlockStyle);
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