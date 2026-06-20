#pragma once

/**
 * @file ParsedText.h
 * @brief Public interface and types for ParsedText.
 */

#include <EpdFontFamily.h>

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "blocks/TextBlock.h"

class GfxRenderer;

class ParsedText {
  std::list<std::string> words;
  std::list<EpdFontFamily::Style> wordStyles;
  std::list<uint8_t> bionicPrefixBytes;
  std::list<uint8_t> wordSmallCaps;
  std::list<uint8_t> wordUnderline;
  TextBlock::Style style;
  bool extraParagraphSpacing;
  bool hyphenationEnabled;
  bool bionicReadingEnabled;
  /** Reader "Indent" / book setting: legacy first-line em and em-based CSS indent simulation. */
  bool respectParagraphIndent_ = true;
  /** Word-spacing multiplier (textSpace/100); scales the inter-word space used for layout. */
  float wordSpacingFactor_ = 1.0f;

  int cssTextIndentPx = -1;

  
  uint16_t leftIndentWidth = 0;
  uint16_t leftIndentLineCount = 0;
  void applyParagraphIndent(const GfxRenderer& renderer, int fontId);
  std::vector<size_t> computeLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth, int spaceWidth,
                                        std::vector<uint16_t>& wordWidths, int dropIndentW, int dropIndentLines);
  std::vector<size_t> computeHyphenatedLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                                  int spaceWidth, std::vector<uint16_t>& wordWidths, int dropIndentW,
                                                  int dropIndentLines);
  bool hyphenateWordAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                            std::vector<uint16_t>& wordWidths, bool allowFallbackBreaks);
  void extractLine(size_t breakIndex, int pageWidth, int spaceWidth, const std::vector<uint16_t>& wordWidths,
                   const std::vector<size_t>& lineBreakIndices,
                   const std::function<void(std::shared_ptr<TextBlock>)>& processLine);
  std::vector<uint16_t> calculateWordWidths(const GfxRenderer& renderer, int fontId);

 public:
  explicit ParsedText(const TextBlock::Style style, const bool extraParagraphSpacing, const bool hyphenationEnabled,
                      const bool respectParagraphIndent = true, const bool bionicReadingEnabled = false,
                      const float wordSpacingFactor = 1.0f)
      : style(style),
        extraParagraphSpacing(extraParagraphSpacing),
        hyphenationEnabled(hyphenationEnabled),
        bionicReadingEnabled(bionicReadingEnabled),
        respectParagraphIndent_(respectParagraphIndent),
        wordSpacingFactor_(wordSpacingFactor) {}
  ~ParsedText() = default;

  void addWord(std::string word, EpdFontFamily::Style fontStyle, bool smallCaps = false, bool underline = false);
  void setStyle(const TextBlock::Style style) { this->style = style; }
  void setRespectParagraphIndent(bool v) { respectParagraphIndent_ = v; }
  void resetParagraphLayoutHints() {
    cssTextIndentPx = -1;
    leftIndentWidth = 0;
    leftIndentLineCount = 0;
  }
  
  
  void setLeftIndent(uint16_t width, uint16_t lineCount) {
    leftIndentWidth = width;
    leftIndentLineCount = lineCount;
  }

  /** Called when CSS declares text-indent (including 0); disables legacy first-line em otherwise used for left/justify. */
  void setCssTextIndentFromCascade(int resolvedPx) { cssTextIndentPx = (resolvedPx > 0) ? resolvedPx : 0; }

  TextBlock::Style getStyle() const { return style; }
  size_t size() const { return words.size(); }
  bool isEmpty() const { return words.empty(); }
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             bool includeLastLine = true);
};
