/**
 * @file ParsedText.cpp
 * @brief Definitions for ParsedText.
 */

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


constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;

bool containsSoftHyphen(const std::string& word) { return word.find(SOFT_HYPHEN_UTF8) != std::string::npos; }


void stripSoftHyphensInPlace(std::string& word) {
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
}


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

/**
 * When a drop cap / left indent is active, the optimal layout uses O(n^2) DP tables (~12 bytes per cell).
 * Long paragraphs (common in fixed-layout / image-heavy EPUBs) exhaust heap and abort(); greedy packing
 * matches the width rules used by hyphenated layout without O(n^2) memory.
 */
std::vector<size_t> computeGreedyLineBreaksWithDropIndent(const int pageWidth, const int spaceWidth,
                                                          const std::vector<uint16_t>& wordWidths,
                                                          const int dropIndentW, const int dropIndentLines) {
  std::vector<size_t> lineBreakIndices;
  const size_t n = wordWidths.size();
  size_t currentIndex = 0;
  int lineNum = 0;

  while (currentIndex < n) {
    const size_t lineStart = currentIndex;
    int lineWidth = 0;
    const int lineW =
        (dropIndentW > 0 && lineNum < dropIndentLines) ? pageWidth - dropIndentW : pageWidth;

    while (currentIndex < n) {
      const bool isFirstWord = currentIndex == lineStart;
      const int spacing = isFirstWord ? 0 : spaceWidth;
      const int candidateWidth = spacing + static_cast<int>(wordWidths[currentIndex]);

      if (lineWidth + candidateWidth <= lineW) {
        lineWidth += candidateWidth;
        ++currentIndex;
        continue;
      }

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

}  // namespace

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle) {
  if (word.empty()) return;

  words.push_back(std::move(word));
  wordStyles.push_back(fontStyle);
}


void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                       const bool includeLastLine) {
  if (words.empty()) {
    return;
  }

  
  applyParagraphIndent(renderer, fontId);

  const int pageWidth = viewportWidth;
  const int spaceWidth = renderer.getSpaceWidth(fontId);
  auto wordWidths = calculateWordWidths(renderer, fontId);
  std::vector<size_t> lineBreakIndices;
  const int dropW = static_cast<int>(leftIndentWidth);
  const int dropL = static_cast<int>(leftIndentLineCount);
  if (hyphenationEnabled) {
    
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

  /** Break words wider than the *tightest* column they may occupy (narrow drop-cap lines), not full pageWidth. */
  const int narrowColumn =
      (dropIndentW > 0 && dropIndentLines > 0) ? std::max(1, pageWidth - dropIndentW) : pageWidth;
  for (size_t i = 0; i < wordWidths.size(); ++i) {
    while (static_cast<int>(wordWidths[i]) > narrowColumn) {
      if (!hyphenateWordAtIndex(i, narrowColumn, renderer, fontId, wordWidths, true)) {
        break;
      }
    }
  }

  const int n = static_cast<int>(words.size());

  
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

  /** Drop-indent optimal DP is (n+1)*(n+2) cells * ~12 B — fails on long blocks (bad_alloc / abort). */
  constexpr size_t kMaxDropIndentDpCells = 4800;
  const size_t gridCells = static_cast<size_t>(n + 1) * static_cast<size_t>(n + 2);
  if (gridCells > kMaxDropIndentDpCells) {
    return computeGreedyLineBreaksWithDropIndent(pageWidth, spaceWidth, wordWidths, dropIndentW, dropIndentLines);
  }

  const int maxEll = n + 1;

  
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
  if (words.empty()) {
    return;
  }

  if (leftIndentWidth > 0 && leftIndentLineCount > 0) {
    return;
  }

  if (!respectParagraphIndent_) {
    return;
  }

  if (cssTextIndentPx >= 0) {
    if (cssTextIndentPx > 0) {
      static const char em[] = "\xe2\x80\x83";
      const EpdFontFamily::Style emStyle = wordStyles.empty() ? EpdFontFamily::REGULAR : wordStyles.front();
      const int emw = renderer.getTextWidth(fontId, em, emStyle);
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

  if (extraParagraphSpacing && style != TextBlock::JUSTIFIED) {
    return;
  }

  if (style == TextBlock::JUSTIFIED || style == TextBlock::LEFT_ALIGN) {
    words.front().insert(0, "\xe2\x80\x83");
  }
}


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

    
    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      const int spacing = isFirstWord ? 0 : spaceWidth;
      const int candidateWidth = spacing + wordWidths[currentIndex];

      
      if (lineWidth + candidateWidth <= lineW) {
        lineWidth += candidateWidth;
        ++currentIndex;
        continue;
      }

      
      const int availableWidth = lineW - lineWidth - spacing;
      const bool allowFallbackBreaks = isFirstWord;  

      if (availableWidth > 0 &&
          hyphenateWordAtIndex(currentIndex, availableWidth, renderer, fontId, wordWidths, allowFallbackBreaks)) {
        
        lineWidth += spacing + wordWidths[currentIndex];
        ++currentIndex;
        break;
      }

      
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



bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, std::vector<uint16_t>& wordWidths,
                                      const bool allowFallbackBreaks) {
  
  if (availableWidth <= 0 || wordIndex >= words.size()) {
    return false;
  }

  
  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  std::advance(wordIt, wordIndex);
  std::advance(styleIt, wordIndex);

  const std::string& word = *wordIt;
  const auto style = *styleIt;

  
  auto breakInfos = Hyphenator::breakOffsets(word, allowFallbackBreaks);
  if (breakInfos.empty()) {
    return false;
  }

  size_t chosenOffset = 0;
  int chosenWidth = -1;
  bool chosenNeedsHyphen = true;

  
  for (const auto& info : breakInfos) {
    const size_t offset = info.byteOffset;
    if (offset == 0 || offset >= word.size()) {
      continue;
    }

    const bool needsHyphen = info.requiresInsertedHyphen;
    const int prefixWidth = measureWordWidth(renderer, fontId, word.substr(0, offset), style, needsHyphen);
    if (prefixWidth > availableWidth || prefixWidth <= chosenWidth) {
      continue;  
    }

    chosenWidth = prefixWidth;
    chosenOffset = offset;
    chosenNeedsHyphen = needsHyphen;
  }

  if (chosenWidth < 0) {
    
    return false;
  }

  
  std::string remainder = word.substr(chosenOffset);
  wordIt->resize(chosenOffset);
  if (chosenNeedsHyphen) {
    wordIt->push_back('-');
  }

  
  auto insertWordIt = std::next(wordIt);
  auto insertStyleIt = std::next(styleIt);
  words.insert(insertWordIt, remainder);
  wordStyles.insert(insertStyleIt, style);

  
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

  
  int lineWordWidthSum = 0;
  for (size_t i = lastBreakAt; i < lineBreak; i++) {
    lineWordWidthSum += wordWidths[i];
  }

  
  uint16_t currentIndent = 0;
  if (this->leftIndentLineCount > 0) {
    currentIndent = this->leftIndentWidth;
    this->leftIndentLineCount--;
  }

  
  const int effectivePageWidth = pageWidth - currentIndent;
  const int spareSpace = effectivePageWidth - lineWordWidthSum;

  
  int spacing = spaceWidth;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;
  const int gapCount = lineWordCount >= 2 ? static_cast<int>(lineWordCount) - 1 : 0;

  if (style == TextBlock::JUSTIFIED && !isLastLine && gapCount > 0) {
    if (spareSpace >= 0) {
      spacing = spareSpace / gapCount;
    } else {
      /** Greedy/DP mismatch or rounding: line is overfull — tighten gaps so words do not overlap the margin. */
      const int tightened = spaceWidth + spareSpace / gapCount;
      spacing = std::max(1, tightened);
    }
  }
  

  
  uint16_t xpos = currentIndent;
  if (style == TextBlock::RIGHT_ALIGN && spareSpace >= 0) {
    xpos += spareSpace - (lineWordCount - 1) * spaceWidth;
  } else if (style == TextBlock::CENTER_ALIGN && spareSpace >= 0) {
    xpos += (spareSpace - (lineWordCount - 1) * spaceWidth) / 2;
  }

  
  std::list<uint16_t> lineXPos;
  for (size_t i = lastBreakAt; i < lineBreak; i++) {
    const uint16_t currentWordWidth = wordWidths[i];
    lineXPos.push_back(xpos);
    int gapAfter = 0;
    if (i + 1 < lineBreak) {
      gapAfter = spaceWidth;
      if (style == TextBlock::JUSTIFIED && !isLastLine && gapCount > 0) {
        if (spareSpace >= 0) {
          const int rem = spareSpace % gapCount;
          const size_t gapIndex = i - lastBreakAt;
          gapAfter = spacing + (gapIndex < static_cast<size_t>(rem) ? 1 : 0);
        } else {
          gapAfter = spacing;
        }
      }
    }
    xpos = static_cast<uint16_t>(static_cast<int>(xpos) + static_cast<int>(currentWordWidth) + gapAfter);
  }

  
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