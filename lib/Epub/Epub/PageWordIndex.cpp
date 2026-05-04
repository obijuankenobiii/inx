/**
 * @file PageWordIndex.cpp
 */

#include "Epub/PageWordIndex.h"

#include <GfxRenderer.h>

#include <EpdFontFamily.h>

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
      case TAG_PageLine: {
        const auto* pl = static_cast<const PageLine*>(el.get());
        const TextBlock& tb = pl->getTextBlock();
        if (lineStartsOut) {
          lineStartsOut->push_back(out.size());
        }
        const int baseX = pl->xPos + marginLeft;
        const int baseY = pl->yPos + marginTop;
        const size_t wc = tb.getWordCount();
        for (size_t wi = 0; wi < wc; ++wi) {
          PageWordHit h;
          h.elementIndex = ei;
          h.wordIndexInElement = wi;
          h.fontId = bodyFontId;
          const EpdFontFamily::Style st = tb.getWordStyleAt(wi);
          const uint16_t relX = tb.getWordXAt(wi);
          const std::string wtext = tb.getWordAt(wi);
          if (!omitStoredWordStrings) {
            h.text = wtext;
          }
          h.screenX = baseX + relX;
          h.screenY = baseY;
          h.screenW = std::max(1, renderer.getTextWidth(bodyFontId, wtext.c_str(), st));
          h.screenH = renderer.getLineHeight(bodyFontId);
          h.isDropCap = false;
          out.push_back(std::move(h));
        }
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
        const size_t wc = tb.getWordCount();
        for (size_t wi = 0; wi < wc; ++wi) {
          PageWordHit h;
          h.elementIndex = ei;
          h.wordIndexInElement = wi;
          h.fontId = hdrFont;
          const EpdFontFamily::Style st = tb.getWordStyleAt(wi);
          const uint16_t relX = tb.getWordXAt(wi);
          const std::string wtext = tb.getWordAt(wi);
          if (!omitStoredWordStrings) {
            h.text = wtext;
          }
          h.screenX = baseX + relX;
          h.screenY = baseY;
          h.screenW = std::max(1, renderer.getTextWidth(hdrFont, wtext.c_str(), st));
          h.screenH = renderer.getLineHeight(hdrFont);
          h.isDropCap = false;
          out.push_back(std::move(h));
        }
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
          // Match PageDropCap::render (y offset -5 vs body lines).
          h.screenY = dc->yPos + marginTop - 5;
          h.screenW = std::max(1, renderer.getTextWidth(df, dct.c_str(), EpdFontFamily::BOLD));
        }
        h.screenH = renderer.getLineHeight(df);
        h.isDropCap = true;
        out.push_back(std::move(h));
        break;
      }
      default:
        break;
    }
  }
}
