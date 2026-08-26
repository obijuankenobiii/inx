#pragma once

#include <Epub/Page.h>

#include <cstddef>
#include <string>
#include <vector>

class GfxRenderer;

/** One laid-out word on a reader page (for highlights / annotations). */
struct PageWordHit {
  size_t elementIndex = 0;
  size_t wordIndexInElement = 0;
  int screenX = 0;
  int screenY = 0;
  int screenW = 0;
  int screenH = 0;
  /** Font used to render this word (baseline/ascender match highlight box). */
  int fontId = 0;
  std::string text;
  bool isDropCap = false;
  /** Line-wrap hyphenation: this token ends with '-' and the next token is the rest of the same word. */
  bool hyphenJoinNext = false;
  /** This token continues a previous hyphen-broken token. */
  bool hyphenJoinPrev = false;
};

/**
 * Flattens PageLine / PageHeader / PageDropCap words in reading order.
 * @param lineStartsOut optional: index into @p out for the first word of each text line (element).
 */
void buildPageWordIndex(const Page& page, GfxRenderer& renderer, int bodyFontId, int headerFontId, int marginLeft,
                        int marginTop, std::vector<PageWordHit>& out, std::vector<size_t>* lineStartsOut = nullptr,
                        bool omitStoredWordStrings = false);

/** True when @p text looks like a hyphenation prefix at a line wrap (letter, then ASCII '-'). */
bool isLineBreakHyphenPrefix(const std::string& text);

/** Sets hyphenJoinNext / hyphenJoinPrev for same-page line wraps. Last-word prefixes also get
 *  hyphenJoinNext so a following page can continue the word. */
void markHyphenJoins(std::vector<PageWordHit>& words);

/** Inclusive token range around @p index that belongs to the same hyphen-broken word. */
void expandHyphenJoinRange(const std::vector<PageWordHit>& words, size_t index, size_t& lo, size_t& hi);

/** Joins tokens in [lo, hi]. Line-break hyphens are dropped unless @p keepLineBreakHyphen. */
std::string joinedHyphenRangeText(const std::vector<PageWordHit>& words, size_t lo, size_t hi,
                                  bool keepLineBreakHyphen);
