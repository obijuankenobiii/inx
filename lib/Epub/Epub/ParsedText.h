#pragma once

#include <EpdFontFamily.h>

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
  TextBlock::Style style;
  bool extraParagraphSpacing;
  bool hyphenationEnabled;

  // First-line indent from CSS text-indent (FOLLOW_CSS). -1 = not specified (legacy em-space may apply).
  int cssTextIndentPx = -1;

  // Indent state for drop caps
  uint16_t leftIndentWidth = 0;
  uint16_t leftIndentLineCount = 0;

  void applyParagraphIndent(const GfxRenderer& renderer, int fontId);
  std::vector<size_t> computeLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth, int spaceWidth,
                                        std::vector<uint16_t>& wordWidths);
  std::vector<size_t> computeHyphenatedLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                                  int spaceWidth, std::vector<uint16_t>& wordWidths);
  bool hyphenateWordAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                            std::vector<uint16_t>& wordWidths, bool allowFallbackBreaks);
  void extractLine(size_t breakIndex, int pageWidth, int spaceWidth, const std::vector<uint16_t>& wordWidths,
                   const std::vector<size_t>& lineBreakIndices,
                   const std::function<void(std::shared_ptr<TextBlock>)>& processLine);
  std::vector<uint16_t> calculateWordWidths(const GfxRenderer& renderer, int fontId);

 public:
  explicit ParsedText(const TextBlock::Style style, const bool extraParagraphSpacing,
                      const bool hyphenationEnabled = false)
      : style(style), extraParagraphSpacing(extraParagraphSpacing), hyphenationEnabled(hyphenationEnabled) {}
  ~ParsedText() = default;

  void addWord(std::string word, EpdFontFamily::Style fontStyle);
  void setStyle(const TextBlock::Style style) { this->style = style; }
  
  // Setter for drop cap indentation
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