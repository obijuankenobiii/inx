#pragma once

/**
 * @file ChapterHtmlSlimParser.h
 * @brief Public interface and types for ChapterHtmlSlimParser.
 */

#include <ImageRenderMode.h>
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
class PageCssBorderBox;

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
  int superscriptUntilDepth = INT_MAX;
  int subscriptUntilDepth = INT_MAX;
  // Depths of every currently-open <ul>/<ol> (a stack, so nested lists close out correctly). While non-empty,
  // cancels any first-line text-indent - CSS text-indent or the reader's own default paragraph indent - on
  // the <li> itself or anything nested inside it (e.g. <li><p>...</p></li>). List items must always line up
  // regardless of the reader's Indent setting or the book's CSS.
  std::vector<int> listNoIndentDepths_;

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
  bool nextWordJoinsPrevious = false;
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;
  int currentTextBlockContentX = 0;
  int currentTextBlockContentWidth = 1;

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
  bool warmImageDisplayCache = false;
  ImageRenderMode warmImageRenderMode = ImageRenderMode::OneBit;
  bool warmImageQuality = false;
  int warmImageYOffset = 0;

  /** After cold image extract, yield occasionally so heap can consolidate (ZIP + converters). */
  unsigned imageExtractCountForYield_ = 0;

  CssParser cssParser_;
  const CssParser* sharedCssParser = nullptr;
  CssParser::UsageFilter cssUsageFilter_;
  bool cssLoaded;
  std::vector<TextBlock::Style> cssAlignmentStack;
  // Parallel to cssAlignmentStack: true once some element in this ancestry chain (this one or an ancestor)
  // had a real CSS text-align of its own - as opposed to cssAlignmentStack just holding the document's
  // default fallback style with no actual CSS behind it. Headers use this to tell "inherit the ancestor's
  // real text-align" apart from "no CSS alignment info detected anywhere, use the header's own centered
  // default" (many books rely on that default because their h1/h2 rule is a descendant selector like
  // ".chapter-title h2" that the simplified CSS matcher's own-element check doesn't detect).
  std::vector<bool> cssAlignmentExplicitStack;
  // Element depth that pushed each cssAlignmentStack entry, so endElement only pops the level it pushed.
  // Tags that early-return in startElement (img, hr, table cells, skipped tags) never push; without this an
  // unconditional pop would drop an ancestor's alignment and break inheritance for later siblings.
  std::vector<int> cssAlignmentDepths;
  std::vector<int> cssDisplayBlockDepths;
  // Whether <li> items directly inside the innermost open <ul> should draw a bullet marker, per its own
  // list-style/list-style-type CSS (defaults to visible, matching the browser default of list-style: disc).
  std::vector<bool> ulBulletVisibleStack;
  std::vector<int> ulBulletVisibleDepths;
  // Set when a <li> that should show a bullet opens; consumed as a standalone marker element (not a text
  // word - see pendingListMarkerX_) in characterData() right before the first real character of the item's
  // text, wherever that text ends up nesting.
  bool pendingListMarker_ = false;
  // Left edge (px) where the pending marker itself should be drawn - the <li>'s margin before its hanging
  // indent was added.
  int16_t pendingListMarkerX_ = 0;
  // Hanging-indent width applied to the current <li> (0 if it has no marker), so it can be undone at </li>.
  int listMarkerIndentPx_ = 0;
  struct CssFontStyleScope {
    int depth = 0;
    bool bold = false;
    bool italic = false;
  };
  std::vector<CssFontStyleScope> cssFontStyleStack;
  std::vector<bool> smallCapsStack;
  std::vector<int> smallCapsDepths;
  struct InlineXOffsetScope {
    int depth = 0;
    int offset = 0;
  };
  std::vector<InlineXOffsetScope> inlineXOffsetStack;
  int currentInlineXOffsetPx = 0;
  struct CssHorizontalInsetScope {
    int depth = 0;
    int left = 0;
    int right = 0;
    std::string classAttr;
  };
  struct CssBorderBoxScope {
    int depth = 0;
    PageCssBorderBox* elem = nullptr;
    int16_t x = 0;
    int16_t y = 0;
    int16_t width = 0;
    int borderTop = 0;
    int borderRight = 0;
    int paddingBottom = 0;
    int borderBottom = 0;
    int borderLeft = 0;
    uint8_t borderTopStyle = 0;
    uint8_t borderRightStyle = 0;
    int marginBottom = 0;
    uint8_t borderBottomStyle = 0;
    uint8_t borderLeftStyle = 0;
    int horizontalChrome = 0;
    bool shrinkToContent = false;
    bool finalized = false;
    // Set once a nested child's own beginCssBlockBox() call overwrites the shared currentBlock* fields with
    // its own values while this box is still open - a leaf box (e.g. a single-line <p> bubble) never sees
    // this, so its own content can still be flushed normally (non-deferred) at its own close, including the
    // shrink-to-content width narrowing that only runs on that normal path.
    bool stale = false;
  };
  // A block-like element's own closing spacing (captured right after its beginCssBlockBox() call) so it
  // survives nested children - a header, a bordered <span>, or a plain nested <div> - that call
  // beginCssBlockBox() themselves and overwrite the shared currentBlock* fields with their own (different)
  // values before this element gets to close. Pushed for headers, block tags, and custom-display-block
  // elements alike (any beginCssBlockBox() caller with real CSS spacing of its own).
  struct BlockClosingScope {
    int depth = 0;
    int marginBottom = 0;
    int paddingBottom = 0;
    int borderBottom = 0;
    uint8_t borderBottomStyle = 0;
    bool usesBorderBox = false;
    int minHeight = 0;
    int16_t contentStartY = 0;
    // Set once a nested child's own beginCssBlockBox() call overwrites the shared currentBlock* fields with
    // its own values - signals that this element's preserved values (not the live fields) must be re-applied
    // when it closes.
    bool stale = false;
  };
  std::vector<CssHorizontalInsetScope> cssHorizontalInsetStack;
  std::vector<CssBorderBoxScope> cssBorderBoxStack;
  std::vector<BlockClosingScope> blockClosingStack;
  /** Pushes a BlockClosingScope for the element that just called beginCssBlockBox(), if it has any CSS
   *  closing spacing of its own worth preserving against nested-child clobbering. */
  void pushBlockClosingScopeIfNeeded();
  int currentCssInsetLeftPx = 0;
  int currentCssInsetRightPx = 0;
  int currentBlockBottomSpacingPx = 0;
  bool currentBlockSpacingFromCss = false;
  int currentBlockMarginBottomPx = 0;
  int currentBlockPaddingBottomPx = 0;
  int currentBlockBorderTopPx = 0;
  int currentBlockBorderBottomPx = 0;
  int currentBlockBorderLeftPx = 0;
  int currentBlockBorderRightPx = 0;
  /** CSS border-style code (PageCssBorderLine::Style) for the pending bottom border. */
  uint8_t currentBlockBorderTopStyle = 0;
  uint8_t currentBlockBorderBottomStyle = 0;
  uint8_t currentBlockBorderLeftStyle = 0;
  uint8_t currentBlockBorderRightStyle = 0;
  bool currentBlockUsesBorderBox = false;
  bool currentBlockShrinkBorderBoxToContent = false;
  int currentBlockHorizontalChromePx = 0;
  int16_t currentBlockBorderBoxX = 0;
  int16_t currentBlockBorderBoxY = 0;
  int16_t currentBlockBorderBoxW = 0;
  /** CSS min-height for the current block (px); content is padded out to this if shorter. 0 = none. */
  int currentBlockMinHeightPx = 0;
  /** Font id override for the current block when its CSS font-size is large (e.g. a big centered title <p>).
   *  -1 = no override (use header/body font as usual). */
  int currentBlockFontId = -1;
  /** Y where the current block's content started (after top margin/border/padding), for min-height. */
  int16_t currentBlockContentStartY = 0;
  /** Top border rule of the current block, deferred so its width can be set to the text width after layout. */
  PageCssBorderLine* pendingTopBorderElem_ = nullptr;
  /** Full CSS border box for blocks that have left/right borders; height is finalized after text layout. */
  PageCssBorderBox* pendingBorderBoxElem_ = nullptr;

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
   * @param deferClosingSpacingToCaller Lay out text only — skip this block's own trailing margin/padding/border
   *        spacing (and border-box geometry finalization). Used when flushing the last child of a border/padded
   *        box right before the box's own (authoritative) closing spacing is applied by the caller, so the
   *        child's trailing spacing doesn't stack with the box's.
   */
  void makePages(bool deferClosingSpacingToCaller = false);

  /**
   * Adds a single text line to the current page.
   */
  void addLineToPage(TextBlock&& line);
  void completeCurrentPage();
  void finalizeOpenBorderBoxesForPageBreak();
  void restartOpenBorderBoxesAfterPageBreak();
  void addCenteredDivider(const char* text);
  void addHorizontalRule(const std::string& tagLower = "hr", const std::string& classAttr = "",
                         const std::string& idAttr = "", const std::string& styleAttr = "");
  /** Emits a horizontal border rule (full content width placeholder) and returns it so its width can be
   *  narrowed to the text content width once the block is laid out. */
  PageCssBorderLine* addCssBorderLine(int thicknessPx, uint8_t style = 0);
  /** Narrows a border rule to the block's text content width + 2%, centered or left-aligned to the text. */
  void finalizeBorderWidth(PageCssBorderLine* elem, int contentWidth, bool center) const;
  /** Default breathing room between a CSS border rule and the block's text when no padding is specified. */
  int cssBorderInnerGapPx() const;
  /** Removes the first line's glyph top leading after a padded top border so the visible gap equals the CSS
   *  padding (not padding + leading). Capped at the padding, so zero-padding blocks are unaffected. */
  void tightenAfterTopBorder(int borderTop, int paddingTop);
  void tightenBeforeBottomBorder(int borderBottom, int paddingBottom);
  /** Applies a CSS block's box model at its start: emits the top margin/border/padding, records the matching
   *  bottom edges + min-height for makePages() to apply, and marks the block's spacing as CSS-driven. Shared by
   *  the header, block, and custom-display-block element branches in startElement(). */
  void beginCssBlockBox(const std::string& tagLower, const std::string& classAttr, const std::string& idAttr,
                        const std::string& styleAttr);
  /** Current text layout width after inherited CSS margin/padding-left/right. */
  int activeBlockContentWidth() const;
  /** Current text x offset after inherited CSS margin/padding-left. */
  int activeBlockContentX() const;
  /** Captures the current CSS horizontal inset for the active text block. */
  void captureCurrentTextBlockBox();
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
  void addImageToPage(const std::string& bmpPath, int imgW, int imgH, int reservedHeight = -1);

  /**
   * Ensures an image is cached as BMP format.
   */
  bool ensureImageCached(const std::string& internalPath, const std::string& cacheImgPath, int* w, int* h);
  bool ensureImageFileAvailable(const std::string& internalPath, const std::string& cacheImgPath);

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
  explicit ChapterHtmlSlimParser(
      const std::string& filepath, const Epub& epub, const std::string& cachePath, const std::string& contentBasePath,
      GfxRenderer& renderer, const int fontId, const int headerFontId, const int maxFontId, const float lineCompression,
      const float wordSpacingFactor, const bool extraParagraphSpacing, const uint8_t paragraphAlignment,
      const uint16_t viewportWidth, const uint16_t viewportHeight, const bool hyphenationEnabled,
      const bool respectCssParagraphIndent, const bool bionicReadingEnabled,
      const std::function<void(std::unique_ptr<Page>)>& completePageFn, const bool warmImageDisplayCache = false,
      const ImageRenderMode warmImageRenderMode = ImageRenderMode::OneBit, const bool warmImageQuality = false,
      const int warmImageYOffset = 0, const std::function<void()>& popupFn = nullptr)
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
        warmImageDisplayCache(warmImageDisplayCache),
        warmImageRenderMode(warmImageRenderMode),
        warmImageQuality(warmImageQuality),
        warmImageYOffset(warmImageYOffset),
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
