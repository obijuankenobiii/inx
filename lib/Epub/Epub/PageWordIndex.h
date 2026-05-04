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
};

/**
 * Flattens PageLine / PageHeader / PageDropCap words in reading order.
 * @param lineStartsOut optional: index into @p out for the first word of each text line (element).
 */
void buildPageWordIndex(const Page& page, GfxRenderer& renderer, int bodyFontId, int headerFontId, int marginLeft,
                        int marginTop, std::vector<PageWordHit>& out, std::vector<size_t>* lineStartsOut = nullptr,
                        bool omitStoredWordStrings = false);
