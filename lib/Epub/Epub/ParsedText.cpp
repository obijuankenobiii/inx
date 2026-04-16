#include "ParsedText.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <vector>

#include "hyphenation/Hyphenator.h"

constexpr int MAX_COST = std::numeric_limits<int>::max();

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;

bool containsSoftHyphen(const std::string& word) { return word.find(SOFT_HYPHEN_UTF8) != std::string::npos; }

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string& word) {
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
}

// Returns the rendered width for a word while ignoring soft hyphen glyphs and optionally appending a visible hyphen.
uint16_t measureWordWidth(const GfxRenderer& renderer, const int fontId, const std::string& word,
                          const EpdFontFamily::Style style, const bool appendHyphen = false) {
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen) {
    return renderer.getTextWidth(fontId, word.c_str(), style);
  }

  std::string sanitized = word;
  if (hasSoftHyphen) {
    stripSoftHyphensInPlace(sanitized);
  }
  if (appendHyphen) {
    sanitized.push_back('-');
  }
  return renderer.getTextWidth(fontId, sanitized.c_str(), style);
}

}  // namespace

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle) {
  if (word.empty()) return;

  words.push_back(std::move(word));
  wordStyles.push_back(fontStyle);
}

// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                       const bool includeLastLine) {
  if (words.empty()) {
    return;
  }

  // Apply fixed transforms before any per-line layout work.
  applyParagraphIndent(renderer, fontId);

  const int pageWidth = viewportWidth;
  const int spaceWidth = renderer.getSpaceWidth(fontId);
  auto wordWidths = calculateWordWidths(renderer, fontId);
  std::vector<size_t> lineBreakIndices;
  const int dropW = static_cast<int>(leftIndentWidth);
  const int dropL = static_cast<int>(leftIndentLineCount);
  if (hyphenationEnabled) {
    // Use greedy layout that can split words mid-loop when a hyphenated prefix fits.
    lineBreakIndices = computeHyphenatedLineBreaks(renderer, fontId, pageWidth, spaceWidth, wordWidths, dropW, dropL);
  } else {
    lineBreakIndices = computeLineBreaks(renderer, fontId, pageWidth, spaceWidth, wordWidths, dropW, dropL);
  }
  const size_t lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

  for (size_t i = 0; i < lineCount; ++i) {
    extractLine(i, pageWidth, spaceWidth, wordWidths, lineBreakIndices, processLine);
  }
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  const size_t totalWordCount = words.size();

  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(totalWordCount);

  auto wordsIt = words.begin();
  auto wordStylesIt = wordStyles.begin();

  while (wordsIt != words.end()) {
    wordWidths.push_back(measureWordWidth(renderer, fontId, *wordsIt, *wordStylesIt));

    std::advance(wordsIt, 1);
    std::advance(wordStylesIt, 1);
  }

  return wordWidths;
}

std::vector<size_t> ParsedText::computeLineBreaks(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                                  const int spaceWidth, std::vector<uint16_t>& wordWidths,
                                                  int dropIndentW, int dropIndentLines) {
  if (words.empty()) {
    return {};
  }

  // Ensure any word that would overflow even as the first entry on a line is split using fallback hyphenation.
  for (size_t i = 0; i < wordWidths.size(); ++i) {
    while (wordWidths[i] > pageWidth) {
      if (!hyphenateWordAtIndex(i, pageWidth, renderer, fontId, wordWidths, /*allowFallbackBreaks=*/true)) {
        break;
      }
    }
  }

  const int n = static_cast<int>(words.size());

  // No drop cap: keep original single-layer DP (faster for long paragraphs).
  if (dropIndentW <= 0 || dropIndentLines <= 0) {
    const size_t totalWordCount = words.size();
    std::vector<int> dp(totalWordCount);
    std::vector<size_t> ans(totalWordCount);
    dp[totalWordCount - 1] = 0;
    ans[totalWordCount - 1] = totalWordCount - 1;

    for (int i = static_cast<int>(totalWordCount) - 2; i >= 0; --i) {
      int currlen = -spaceWidth;
      dp[static_cast<size_t>(i)] = MAX_COST;

      for (size_t j = static_cast<size_t>(i); j < totalWordCount; ++j) {
        currlen += wordWidths[j] + spaceWidth;
        if (currlen > pageWidth) {
          break;
        }
        int cost;
        if (j == totalWordCount - 1) {
          cost = 0;
        } else {
          const int remainingSpace = pageWidth - currlen;
          const long long cost_ll =
              static_cast<long long>(remainingSpace) * remainingSpace + dp[j + 1];
          cost = (cost_ll > MAX_COST) ? MAX_COST : static_cast<int>(cost_ll);
        }
        if (cost < dp[static_cast<size_t>(i)]) {
          dp[static_cast<size_t>(i)] = cost;
          ans[static_cast<size_t>(i)] = j;
        }
      }
      if (dp[static_cast<size_t>(i)] == MAX_COST) {
        ans[static_cast<size_t>(i)] = static_cast<size_t>(i);
        if (i + 1 < static_cast<int>(totalWordCount)) {
          dp[static_cast<size_t>(i)] = dp[static_cast<size_t>(i + 1)];
        } else {
          dp[static_cast<size_t>(i)] = 0;
        }
      }
    }

    std::vector<size_t> lineBreakIndices;
    size_t currentWordIndex = 0;
    while (currentWordIndex < totalWordCount) {
      size_t nextBreakIndex = ans[currentWordIndex] + 1;
      if (nextBreakIndex <= currentWordIndex) {
        nextBreakIndex = currentWordIndex + 1;
      }
      lineBreakIndices.push_back(nextBreakIndex);
      currentWordIndex = nextBreakIndex;
    }
    return lineBreakIndices;
  }

  const int maxEll = n + 1;

  // dp[i][ell] = min badness for suffix words [i, n) when ell lines have already been placed above this suffix.
  std::vector<std::vector<int>> dp(static_cast<size_t>(n + 1), std::vector<int>(static_cast<size_t>(maxEll + 1), MAX_COST));
  std::vector<std::vector<size_t>> ans(static_cast<size_t>(n + 1), std::vector<size_t>(static_cast<size_t>(maxEll + 1), 0));

  for (int ell = 0; ell <= maxEll; ++ell) {
    dp[static_cast<size_t>(n)][static_cast<size_t>(ell)] = 0;
  }

  for (int i = n - 1; i >= 0; --i) {
    for (int ell = 0; ell <= n; ++ell) {
      const int W =
          (dropIndentW > 0 && ell < dropIndentLines) ? pageWidth - dropIndentW : pageWidth;

      int currlen = -spaceWidth;
      dp[static_cast<size_t>(i)][static_cast<size_t>(ell)] = MAX_COST;

      for (int j = i; j < n; ++j) {
        currlen += wordWidths[static_cast<size_t>(j)] + spaceWidth;
        if (currlen > W) {
          break;
        }

        int cost;
        if (j == n - 1) {
          cost = 0;
        } else {
          const int remainingSpace = W - currlen;
          const long long cost_ll =
              static_cast<long long>(remainingSpace) * remainingSpace +
              static_cast<long long>(dp[static_cast<size_t>(j + 1)][static_cast<size_t>(ell + 1)]);
          if (cost_ll > MAX_COST) {
            cost = MAX_COST;
          } else {
            cost = static_cast<int>(cost_ll);
          }
        }

        if (cost < dp[static_cast<size_t>(i)][static_cast<size_t>(ell)]) {
          dp[static_cast<size_t>(i)][static_cast<size_t>(ell)] = cost;
          ans[static_cast<size_t>(i)][static_cast<size_t>(ell)] = static_cast<size_t>(j);
        }
      }

      if (dp[static_cast<size_t>(i)][static_cast<size_t>(ell)] == MAX_COST) {
        ans[static_cast<size_t>(i)][static_cast<size_t>(ell)] = static_cast<size_t>(i);
        if (i + 1 < n) {
          dp[static_cast<size_t>(i)][static_cast<size_t>(ell)] = dp[static_cast<size_t>(i + 1)][static_cast<size_t>(ell + 1)];
        } else {
          dp[static_cast<size_t>(i)][static_cast<size_t>(ell)] = 0;
        }
      }
    }
  }

  std::vector<size_t> lineBreakIndices;
  size_t idx = 0;
  int ell = 0;
  while (idx < static_cast<size_t>(n)) {
    const size_t last = ans[idx][static_cast<size_t>(ell)];
    lineBreakIndices.push_back(last + 1);
    idx = last + 1;
    ++ell;
  }

  return lineBreakIndices;
}

void ParsedText::applyParagraphIndent(const GfxRenderer& renderer, const int fontId) {
  if (extraParagraphSpacing || words.empty()) {
    return;
  }

  if (cssTextIndentPx >= 0) {
    if (cssTextIndentPx > 0) {
      static const char em[] = "\xe2\x80\x83";
      const int emw = renderer.getTextWidth(fontId, em, EpdFontFamily::REGULAR);
      if (emw > 0) {
        const int n = std::min(80, (cssTextIndentPx + emw - 1) / emw);
        std::string pad;
        pad.reserve(static_cast<size_t>(n) * 3);
        for (int i = 0; i < n; ++i) {
          pad += em;
        }
        words.front().insert(0, pad);
      }
    }
    return;
  }

  if (style == TextBlock::JUSTIFIED || style == TextBlock::LEFT_ALIGN) {
    words.front().insert(0, "\xe2\x80\x83");
  }
}

// Builds break indices while opportunistically splitting the word that would overflow the current line.
std::vector<size_t> ParsedText::computeHyphenatedLineBreaks(const GfxRenderer& renderer, const int fontId,
                                                            const int pageWidth, const int spaceWidth,
                                                            std::vector<uint16_t>& wordWidths, int dropIndentW,
                                                            int dropIndentLines) {
  std::vector<size_t> lineBreakIndices;
  size_t currentIndex = 0;
  int lineNum = 0;

  while (currentIndex < wordWidths.size()) {
    const size_t lineStart = currentIndex;
    int lineWidth = 0;
    const int lineW =
        (dropIndentW > 0 && lineNum < dropIndentLines) ? pageWidth - dropIndentW : pageWidth;

    // Consume as many words as possible for current line, splitting when prefixes fit
    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      const int spacing = isFirstWord ? 0 : spaceWidth;
      const int candidateWidth = spacing + wordWidths[currentIndex];

      // Word fits on current line
      if (lineWidth + candidateWidth <= lineW) {
        lineWidth += candidateWidth;
        ++currentIndex;
        continue;
      }

      // Word would overflow — try to split based on hyphenation points
      const int availableWidth = lineW - lineWidth - spacing;
      const bool allowFallbackBreaks = isFirstWord;  // Only for first word on line

      if (availableWidth > 0 &&
          hyphenateWordAtIndex(currentIndex, availableWidth, renderer, fontId, wordWidths, allowFallbackBreaks)) {
        // Prefix now fits; append it to this line and move to next line
        lineWidth += spacing + wordWidths[currentIndex];
        ++currentIndex;
        break;
      }

      // Could not split: force at least one word per line to avoid infinite loop
      if (currentIndex == lineStart) {
        lineWidth += candidateWidth;
        ++currentIndex;
      }
      break;
    }

    lineBreakIndices.push_back(currentIndex);
    ++lineNum;
  }

  return lineBreakIndices;
}

// Splits words[wordIndex] into prefix (adding a hyphen only when needed) and remainder when a legal breakpoint fits the
// available width.
bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, std::vector<uint16_t>& wordWidths,
                                      const bool allowFallbackBreaks) {
  // Guard against invalid indices or zero available width before attempting to split.
  if (availableWidth <= 0 || wordIndex >= words.size()) {
    return false;
  }

  // Get iterators to target word and style.
  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  std::advance(wordIt, wordIndex);
  std::advance(styleIt, wordIndex);

  const std::string& word = *wordIt;
  const auto style = *styleIt;

  // Collect candidate breakpoints (byte offsets and hyphen requirements).
  auto breakInfos = Hyphenator::breakOffsets(word, allowFallbackBreaks);
  if (breakInfos.empty()) {
    return false;
  }

  size_t chosenOffset = 0;
  int chosenWidth = -1;
  bool chosenNeedsHyphen = true;

  // Iterate over each legal breakpoint and retain the widest prefix that still fits.
  for (const auto& info : breakInfos) {
    const size_t offset = info.byteOffset;
    if (offset == 0 || offset >= word.size()) {
      continue;
    }

    const bool needsHyphen = info.requiresInsertedHyphen;
    const int prefixWidth = measureWordWidth(renderer, fontId, word.substr(0, offset), style, needsHyphen);
    if (prefixWidth > availableWidth || prefixWidth <= chosenWidth) {
      continue;  // Skip if too wide or not an improvement
    }

    chosenWidth = prefixWidth;
    chosenOffset = offset;
    chosenNeedsHyphen = needsHyphen;
  }

  if (chosenWidth < 0) {
    // No hyphenation point produced a prefix that fits in the remaining space.
    return false;
  }

  // Split the word at the selected breakpoint and append a hyphen if required.
  std::string remainder = word.substr(chosenOffset);
  wordIt->resize(chosenOffset);
  if (chosenNeedsHyphen) {
    wordIt->push_back('-');
  }

  // Insert the remainder word (with matching style) directly after the prefix.
  auto insertWordIt = std::next(wordIt);
  auto insertStyleIt = std::next(styleIt);
  words.insert(insertWordIt, remainder);
  wordStyles.insert(insertStyleIt, style);

  // Update cached widths to reflect the new prefix/remainder pairing.
  wordWidths[wordIndex] = static_cast<uint16_t>(chosenWidth);
  const uint16_t remainderWidth = measureWordWidth(renderer, fontId, remainder, style);
  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);
  return true;
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const int spaceWidth,
                             const std::vector<uint16_t>& wordWidths, const std::vector<size_t>& lineBreakIndices,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;

  // Calculate total word width for this line
  int lineWordWidthSum = 0;
  for (size_t i = lastBreakAt; i < lineBreak; i++) {
    lineWordWidthSum += wordWidths[i];
  }

  // --- INDENT LOGIC ---
  uint16_t currentIndent = 0;
  if (this->leftIndentLineCount > 0) {
    currentIndent = this->leftIndentWidth;
    this->leftIndentLineCount--;
  }

  // Adjusted available page width
  const int effectivePageWidth = pageWidth - currentIndent;
  const int spareSpace = effectivePageWidth - lineWordWidthSum;

  // Use normal spacing, don't compress!
  int spacing = spaceWidth;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;

  // Only justify if we have extra space (spareSpace >= 0)
  if (style == TextBlock::JUSTIFIED && !isLastLine && lineWordCount >= 2 && spareSpace >= 0) {
    spacing = spareSpace / (lineWordCount - 1);
  }
  // If spareSpace is negative, keep normal spacing and let it overflow or be handled by the renderer

  // Calculate initial x position starting from the indent
  uint16_t xpos = currentIndent;
  if (style == TextBlock::RIGHT_ALIGN && spareSpace >= 0) {
    xpos += spareSpace - (lineWordCount - 1) * spaceWidth;
  } else if (style == TextBlock::CENTER_ALIGN && spareSpace >= 0) {
    xpos += (spareSpace - (lineWordCount - 1) * spaceWidth) / 2;
  }

  // Pre-calculate X positions for words
  std::list<uint16_t> lineXPos;
  for (size_t i = lastBreakAt; i < lineBreak; i++) {
    const uint16_t currentWordWidth = wordWidths[i];
    lineXPos.push_back(xpos);
    xpos += currentWordWidth + spacing;
  }

  // Extract words (same as before)
  auto wordEndIt = words.begin();
  auto wordStyleEndIt = wordStyles.begin();
  std::advance(wordEndIt, lineWordCount);
  std::advance(wordStyleEndIt, lineWordCount);

  std::list<std::string> lineWords;
  lineWords.splice(lineWords.begin(), words, words.begin(), wordEndIt);
  std::list<EpdFontFamily::Style> lineWordStyles;
  lineWordStyles.splice(lineWordStyles.begin(), wordStyles, wordStyles.begin(), wordStyleEndIt);

  for (auto& word : lineWords) {
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
  }

  processLine(std::make_shared<TextBlock>(std::move(lineWords), std::move(lineXPos), std::move(lineWordStyles), style));
}