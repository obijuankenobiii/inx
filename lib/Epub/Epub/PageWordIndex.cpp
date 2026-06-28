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
                           const EpdFontFamily::Style st) {
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
                           const EpdFontFamily::Style st) {
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
          // Match PageDropCap::render (top aligned with the first body line).
          h.screenY = dc->yPos + marginTop - 5;
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
}
