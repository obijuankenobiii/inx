/**
 * @file PageWordIndex.cpp
 */

#include "Epub/PageWordIndex.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>

#include <cctype>

void buildPageWordIndex(const Page& page, GfxRenderer& renderer, const int bodyFontId, const int headerFontId,
                        const int marginLeft, const int marginTop, std::vector<PageWordHit>& out,
                        std::vector<size_t>* lineStartsOut, const bool omitStoredWordStrings) {
  out.clear();
  if (lineStartsOut) {
    lineStartsOut->clear();
  }

  for (size_t ei = 0; ei < page.elements.size(); ++ei) {
    const auto& el = page.elements[ei];
    switch (el->getTag()) {
      case TAG_PageSmallCaps:
      case TAG_PageLine: {
        const TextBlock* tbPtr = nullptr;
        int16_t elemX = 0;
        int16_t elemY = 0;
        if (el->getTag() == TAG_PageSmallCaps) {
          const auto* sc = static_cast<const PageSmallCaps*>(el.get());
          tbPtr = &sc->getTextBlock();
          elemX = sc->xPos;
          elemY = sc->yPos;
        } else {
          const auto* pl = static_cast<const PageLine*>(el.get());
          tbPtr = &pl->getTextBlock();
          elemX = pl->xPos;
          elemY = pl->yPos;
        }
        const TextBlock& tb = *tbPtr;
        if (lineStartsOut) {
          lineStartsOut->push_back(out.size());
        }
        const int baseX = elemX + marginLeft;
        const int baseY = elemY + marginTop;
        const int lineHeight = renderer.text.getLineHeight(bodyFontId);
        tb.forEachWord([&](const size_t wi, const std::string& wtext, const uint16_t relX,
                           const EpdFontFamily::Style st, const std::string& footnoteTarget) {
          PageWordHit h;
          h.elementIndex = ei;
          h.wordIndexInElement = wi;
          h.fontId = bodyFontId;
          if (!omitStoredWordStrings) {
            h.text = wtext;
          }
          h.screenX = baseX + relX;
          h.screenY = baseY;
          h.screenW = std::max(1, renderer.text.getWidth(bodyFontId, wtext.c_str(), st));
          h.screenH = lineHeight;
          h.isDropCap = false;
          h.footnoteTarget = footnoteTarget;
          out.push_back(std::move(h));
        });
        break;
      }
      case TAG_PageHeader: {
        const auto* ph = static_cast<const PageHeader*>(el.get());
        const TextBlock& tb = ph->getTextBlock();
        if (lineStartsOut) {
          lineStartsOut->push_back(out.size());
        }
        const int hdrFont = ph->getHeaderFontId();
        const int baseX = ph->xPos + marginLeft;
        const int baseY = ph->yPos + marginTop;
        const int lineHeight = renderer.text.getLineHeight(hdrFont);
        tb.forEachWord([&](const size_t wi, const std::string& wtext, const uint16_t relX,
                           const EpdFontFamily::Style st, const std::string& footnoteTarget) {
          PageWordHit h;
          h.elementIndex = ei;
          h.wordIndexInElement = wi;
          h.fontId = hdrFont;
          if (!omitStoredWordStrings) {
            h.text = wtext;
          }
          h.screenX = baseX + relX;
          h.screenY = baseY;
          h.screenW = std::max(1, renderer.text.getWidth(hdrFont, wtext.c_str(), st));
          h.screenH = lineHeight;
          h.isDropCap = false;
          h.footnoteTarget = footnoteTarget;
          out.push_back(std::move(h));
        });
        break;
      }
      case TAG_PageDropCap: {
        const auto* dc = static_cast<const PageDropCap*>(el.get());
        if (lineStartsOut) {
          lineStartsOut->push_back(out.size());
        }
        PageWordHit h;
        h.elementIndex = ei;
        h.wordIndexInElement = 0;
        const int df = dc->getDropCapFontId();
        h.fontId = df;
        {
          const std::string dct = dc->getDropCapText();
          if (!omitStoredWordStrings) {
            h.text = dct;
          }
          h.screenX = dc->xPos + marginLeft;
          if (dc->isInlineFirstLine()) {
            h.screenY = dc->yPos + marginTop + renderer.text.getFontAscenderSize(bodyFontId) -
                        renderer.text.getFontAscenderSize(df);
          } else {
            h.screenY = dc->yPos + marginTop + PageDropCap::VERTICAL_ADJUSTMENT;
          }
          h.screenW = std::max(1, renderer.text.getWidth(df, dct.c_str(), EpdFontFamily::BOLD));
        }
        h.screenH = renderer.text.getLineHeight(df);
        h.isDropCap = true;
        out.push_back(std::move(h));
        break;
      }
      default:
        break;
    }
  }
  markHyphenJoins(out);
}

bool isLineBreakHyphenPrefix(const std::string& text) {
  if (text.size() < 2 || text.back() != '-') {
    return false;
  }
  const unsigned char before = static_cast<unsigned char>(text[text.size() - 2]);
  return std::isalpha(before) != 0 || before >= 0x80;
}

void markHyphenJoins(std::vector<PageWordHit>& words) {
  if (words.empty()) {
    return;
  }
  for (size_t i = 0; i + 1 < words.size(); ++i) {
    if (!isLineBreakHyphenPrefix(words[i].text)) {
      continue;
    }
    if (words[i + 1].screenY > words[i].screenY + 1) {
      words[i].hyphenJoinNext = true;
      words[i + 1].hyphenJoinPrev = true;
    }
  }
  if (isLineBreakHyphenPrefix(words.back().text)) {
    words.back().hyphenJoinNext = true;
  }
}

void expandHyphenJoinRange(const std::vector<PageWordHit>& words, const size_t index, size_t& lo, size_t& hi) {
  if (words.empty() || index >= words.size()) {
    lo = 0;
    hi = 0;
    return;
  }
  lo = index;
  hi = index;
  while (lo > 0 && words[lo].hyphenJoinPrev) {
    --lo;
  }
  while (hi + 1 < words.size() && words[hi].hyphenJoinNext) {
    ++hi;
  }
}

std::string joinedHyphenRangeText(const std::vector<PageWordHit>& words, const size_t lo, const size_t hi,
                                  const bool keepLineBreakHyphen) {
  std::string out;
  if (words.empty() || lo > hi) {
    return out;
  }
  for (size_t i = lo; i <= hi && i < words.size(); ++i) {
    std::string token = words[i].text;
    const bool joinToNext = i < hi && words[i].hyphenJoinNext;
    if (!keepLineBreakHyphen && joinToNext && !token.empty() && token.back() == '-') {
      token.pop_back();
    }
    if (i > lo && !(i > 0 && words[i - 1].hyphenJoinNext)) {
      out += ' ';
    }
    out += token;
  }
  return out;
}
