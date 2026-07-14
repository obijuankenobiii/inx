#pragma once

/**
 * @file ChapterHtmlSlimParser.h
 * @brief Public interface and types for ChapterHtmlSlimParser.
 */

#include <expat.h>

#include <climits>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../../Epub.h"
#include "../ParsedText.h"
#include "../blocks/TextBlock.h"
#include "CssParser.h"

class Page;
class GfxRenderer;
class PageCssBorderLine;

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
  int underlineUntilDepth = INT_MAX;

  int fontId;
  int headerFontId;
  int maxFontId;

  bool inHeader = false;

  bool inDropCap = false;
  int dropCapDepth = INT_MAX;
  bool dropCapConsumeWholeContainer = false;
  uint8_t dropCapLineCount = 3;

  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;

  float lineCompression;
  float wordSpacingFactor;
  bool extraParagraphSpacing;
  uint8_t paragraphAlignment;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  bool hyphenationEnabled;
  bool bionicReadingEnabled = false;
  /** Book/global "Indent": honor CSS `text-indent` when true (from paragraphCssIndentEnabled). */
  bool respectCssParagraphIndent = false;

  bool skipImages = false;

  /** After cold image extract, yield occasionally so heap can consolidate (ZIP + converters). */
  unsigned imageExtractCountForYield_ = 0;

  CssParser cssParser_;
  const CssParser* sharedCssParser = nullptr;
  bool cssLoaded;
  std::vector<TextBlock::Style> cssAlignmentStack;
  // Element depth that pushed each cssAlignmentStack entry, so endElement only pops the level it pushed.
  // Tags that early-return in startElement (img, hr, table cells, skipped tags) never push; without this an
  // unconditional pop would drop an ancestor's alignment and break inheritance for later siblings.
  std::vector<int> cssAlignmentDepths;
  std::vector<bool> smallCapsStack;
  std::vector<int> smallCapsDepths;
  int currentBlockBottomSpacingPx = 0;
  bool currentBlockSpacingFromCss = false;
  int currentBlockMarginBottomPx = 0;
  int currentBlockPaddingBottomPx = 0;
  int currentBlockBorderBottomPx = 0;
  /** CSS border-style code (PageCssBorderLine::Style) for the pending bottom border. */
  uint8_t currentBlockBorderBottomStyle = 0;
  /** CSS min-height for the current block (px); content is padded out to this if shorter. 0 = none. */
  int currentBlockMinHeightPx = 0;
  /** Font id override for the current block when its CSS font-size is large (e.g. a big centered title <p>).
   *  -1 = no override (use header/body font as usual). */
  int currentBlockFontId = -1;
  /** Y where the current block's content started (after top margin/border/padding), for min-height. */
  int16_t currentBlockContentStartY = 0;
  /** Top border rule of the current block, deferred so its width can be set to the text width after layout. */
  std::shared_ptr<PageCssBorderLine> pendingTopBorderElem_;

  /** When true, Expat callbacks only walk the tree for depth/skip and prefetch images (no text layout). */
  bool imagePrefetchPassOnly_ = false;

  struct TableCellCapture {
    bool header = false;
    int colspan = 1;
    std::string text;
  };
  bool inTable_ = false;
  bool tableShowBorders_ = false;
  int tableDepth_ = INT_MAX;
  int tableRowDepth_ = INT_MAX;
  int tableCellDepth_ = INT_MAX;
  bool tableLastWasSpace_ = true;
  std::vector<std::vector<TableCellCapture>> tableRows_;
  std::vector<TableCellCapture> currentTableRow_;
  std::unique_ptr<TableCellCapture> currentTableCell_;

  void resetStructuralStateForParsePass();

  void prefetchImageFromImgAttributes(const XML_Char** atts);

  bool parseHtmlThroughExpat(bool callProgressPopup);

  /**
   * Creates a new text block with the specified style.
   */
  void startNewTextBlock(TextBlock::Style style);
  void applyVerticalSpacing(int px);
  void flushCurrentTableCell();
  void flushCurrentTableRow();
  void appendTableText(const XML_Char* s, int len);
  void addTableToPage();

  /** Handles an opening tag during the image-prefetch pre-pass (extract images, track skipped subtrees). */
  void handlePrefetchPassElement(const XML_Char* name, const XML_Char** atts);
  /** Starts drop-cap capture if this element carries a drop-cap class/id or a ::first-letter drop-cap rule. */
  void applyDropCapHint(const XML_Char* name, const std::string& tagLower, const std::string& classAttr,
                        const std::string& idAttr, const std::string& styleAttr);
  /** Marks bold/italic/anchor-underline runs active for this element's subtree (until its end depth). */
  void applyInlineFormattingTags(const XML_Char* name);
  /** Captures <table>/<tr>/<td>/<th> structure during the main pass. Returns true if the tag was consumed
   *  (the caller should then return), false if it is not table-related. */
  bool handleTableStartElement(const XML_Char* name, const XML_Char** atts, const std::string& tagLower,
                               const std::string& classAttr, const std::string& idAttr, const std::string& styleAttr);

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
  void addCenteredDivider(const char* text);
  void addHorizontalRule(const std::string& tagLower = "hr", const std::string& classAttr = "",
                         const std::string& idAttr = "", const std::string& styleAttr = "");
  /** Emits a horizontal border rule (full content width placeholder) and returns it so its width can be
   *  narrowed to the text content width once the block is laid out. */
  std::shared_ptr<PageCssBorderLine> addCssBorderLine(int thicknessPx, uint8_t style = 0);
  /** Narrows a border rule to the block's text content width + 2%, centered or left-aligned to the text. */
  void finalizeBorderWidth(const std::shared_ptr<PageCssBorderLine>& elem, int contentWidth, bool center) const;
  /** Default breathing room between a CSS border rule and the block's text when no padding is specified. */
  int cssBorderInnerGapPx() const;
  /** Removes the first line's glyph top leading after a padded top border so the visible gap equals the CSS
   *  padding (not padding + leading). Capped at the padding, so zero-padding blocks are unaffected. */
  void tightenAfterTopBorder(int borderTop, int paddingTop);
  /** Applies a CSS block's box model at its start: emits the top margin/border/padding, records the matching
   *  bottom edges + min-height for makePages() to apply, and marks the block's spacing as CSS-driven. Shared by
   *  the header, block, and custom-display-block element branches in startElement(). */
  void beginCssBlockBox(const std::string& tagLower, const std::string& classAttr, const std::string& idAttr,
                        const std::string& styleAttr);
  /** Pads the current block's content down to its CSS min-height (if the content was shorter). Call after the
   *  block's lines are laid out and before the bottom padding/border/margin. */
  void applyMinHeightPadding();
  /** Font id to lay out / render the current block with: the CSS-font-size override, else header/body font. */
  int activeBlockFontId() const {
    return currentBlockFontId >= 0 ? currentBlockFontId : (inHeader ? headerFontId : fontId);
  }
  /** Maps a CSS font-size em multiplier to a larger reader font id, or -1 to keep the default. */
  int blockFontIdForEm(float em) const {
    if (em >= 1.5f) return maxFontId;
    if (em >= 1.2f) return headerFontId;
    return -1;
  }

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
  const CssParser& css() const { return sharedCssParser ? *sharedCssParser : cssParser_; }

  /** Resolves text-align for the current block element when paragraph alignment is FOLLOW_CSS. */
  TextBlock::Style resolveTextAlignFromAttributes(const XML_Char* elementName, const XML_Char** atts,
                                                  TextBlock::Style inheritedStyle) const;

  /** Picks a block element's paragraph alignment: in FOLLOW_CSS mode the element's own text-align (else
   *  justified); otherwise the user's fixed alignment, with an explicit element text-align still honored. */
  TextBlock::Style resolveBlockStyle(const XML_Char* elementName, const XML_Char** atts,
                                     bool elementHasExplicitTextAlign, TextBlock::Style elementCssStyle,
                                     TextBlock::Style inheritedCssStyle) const;

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
                                 const float wordSpacingFactor, const bool extraParagraphSpacing,
                                 const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                 const uint16_t viewportHeight, const bool hyphenationEnabled,
                                 const bool respectCssParagraphIndent, const bool bionicReadingEnabled,
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
        wordSpacingFactor(wordSpacingFactor),
        extraParagraphSpacing(extraParagraphSpacing),
        paragraphAlignment(paragraphAlignment),
        viewportWidth(viewportWidth),
        viewportHeight(viewportHeight),
        hyphenationEnabled(hyphenationEnabled),
        bionicReadingEnabled(bionicReadingEnabled),
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
