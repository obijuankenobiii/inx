/**
 * @file StatisticActivity.cpp
 * @brief Definitions for StatisticActivity.
 */

#include "StatisticActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <JpegRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "state/ImageBitmapGrayMaps.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"
namespace {

/** Scale/crop so the bitmap fills [x,y,w,h] without letterboxing (matches Recent thumbnails). */
void drawStatsThumbnailInRect(GfxRenderer& renderer, Bitmap& bitmap, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) {
    return;
  }
  const float iw = static_cast<float>(bitmap.getWidth());
  const float ih = static_cast<float>(bitmap.getHeight());
  float cropX = 0.f;
  float cropY = 0.f;
  if (iw > 0.f && ih > 0.f) {
    const float ir = iw / ih;
    const float tr = static_cast<float>(w) / static_cast<float>(h);
    if (ir > tr) {
      cropX = 1.0f - (tr / ir);
    } else if (ir < tr) {
      cropY = 1.0f - (ir / tr);
    }
  }
  renderer.bitmap.render(bitmap, x, y, w, h, cropX, cropY,
                      SETTINGS.bitmapRoundedCorners != 0 ? BitmapRender::RoundedOutside::PaperOutside
                                                         : BitmapRender::RoundedOutside::None);
}

constexpr unsigned long GO_HOME_MS = 1000;

constexpr int FONT_SANS = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
constexpr int FONT_SANS_SM = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
constexpr int FONT_SERIF = LITERATA_14_FONT_ID;
constexpr int FONT_SERIF_MD = LITERATA_16_FONT_ID;
constexpr int FONT_SERIF_LG = LITERATA_18_FONT_ID;
constexpr int FONT_SERIF_SM = LITERATA_12_FONT_ID;
constexpr float kPi = 3.14159265f;

/** Global “All items” donut row (must match measureAllItemsBodyHeight). */
constexpr int kGlobalAllItemsDonutR = 68;
constexpr int kGlobalAllItemsDonutThick = 10;
constexpr int kGlobalAllItemsDonutPadT = 14;
constexpr int kGlobalAllItemsDonutPadB = 14;
constexpr int kGlobalAllItemsPadSide = 12;
constexpr int kGlobalAllItemsDonutTextGap = 10;

void drawThinProgressBar(const GfxRenderer& renderer, int x, int y, int w, int h, float pct01) {
  if (w <= 0 || h <= 0) {
    return;
  }
  renderer.rectangle.fill(x, y, w, h, static_cast<int>(GfxRenderer::FillTone::Gray), false);
  const float p = std::min(1.f, std::max(0.f, pct01));
  const int fillW = static_cast<int>(static_cast<float>(w) * p + 0.5f);
  if (fillW > 0) {
    renderer.rectangle.fill(x, y, fillW, h, static_cast<int>(GfxRenderer::FillTone::Ink), false);
  }
  renderer.rectangle.render(x, y, w, h, true);
}

/** Circle outline via pixels (GfxRenderer::drawLine does not draw diagonals). */
void drawCircleOutlinePixels(const GfxRenderer& renderer, int cx, int cy, int radius) {
  if (radius < 1) {
    return;
  }
  const int n = std::max(120, radius * 6);
  for (int i = 0; i < n; ++i) {
    const float ang = static_cast<float>(i) / static_cast<float>(n) * (2.f * kPi);
    const int px = cx + static_cast<int>(radius * cosf(ang) + 0.5f);
    const int py = cy + static_cast<int>(radius * sinf(ang) + 0.5f);
    renderer.drawPixel(px, py, true);
  }
}

static float normAngle0TwoPi(float a) {
  const float twoPi = 2.f * kPi;
  while (a < 0.f) {
    a += twoPi;
  }
  while (a >= twoPi) {
    a -= twoPi;
  }
  return a;
}

/** CCW angle from a0 to ang, in [0, 2pi). */
static float ccwDeltaFrom(float ang, float a0) {
  const float twoPi = 2.f * kPi;
  const float A = normAngle0TwoPi(ang);
  const float F = normAngle0TwoPi(a0);
  float d = A - F;
  if (d < 0.f) {
    d += twoPi;
  }
  return d;
}

static bool inSweepCcw(float ang, float a0, float sweepRad) {
  const float twoPi = 2.f * kPi;
  if (sweepRad >= twoPi - 0.002f) {
    return true;
  }
  /** Slightly generous so discrete pixels on the wedge edge are not left as background gaps. */
  return ccwDeltaFrom(ang, a0) <= sweepRad + 0.008f;
}

/** Solid ink in annulus wedge (GfxRenderer has no diagonal drawLine for radials/arcs). */
void fillAnnulusWedgeInkPixels(const GfxRenderer& renderer, int cx, int cy, int ro, int ri, float a0, float sweepRad) {
  if (ri >= ro || sweepRad <= 0.f) {
    return;
  }
  const float twoPi = 2.f * kPi;
  const bool full = sweepRad >= twoPi - 0.002f;
  const long rlo = static_cast<long>(ri) * ri;
  const long rhi = static_cast<long>(ro) * ro;
  const int x0 = cx - ro;
  const int x1 = cx + ro;
  const int y0 = cy - ro;
  const int y1 = cy + ro;
  for (int py = y0; py <= y1; ++py) {
    const long dy = static_cast<long>(py - cy);
    const long dy2 = dy * dy;
    for (int px = x0; px <= x1; ++px) {
      const long dx = static_cast<long>(px - cx);
      const long r2 = dx * dx + dy2;
      /** Inclusive outer chord closes 1px gaps vs scanline gray fill; strict inner keeps the hole. */
      if (r2 <= rlo || r2 > rhi) {
        continue;
      }
      if (!full) {
        const float ang = atan2f(static_cast<float>(py - cy), static_cast<float>(px - cx));
        if (!inSweepCcw(ang, a0, sweepRad)) {
          continue;
        }
      }
      renderer.drawPixel(px, py, true);
    }
  }
}

/** Annulus filled row-by-row (reliable on e-ink; avoids polygon / radial artifacts). */
void fillAnnulusToneScanlines(const GfxRenderer& renderer, int cx, int cy, int ro, int ri, GfxRenderer::FillTone tone) {
  if (ri >= ro || ro < 2) {
    return;
  }
  const int scrW = renderer.getScreenWidth();
  const int scrH = renderer.getScreenHeight();
  const int y0 = std::max(0, cy - ro);
  const int y1 = std::min(scrH - 1, cy + ro);
  for (int y = y0; y <= y1; ++y) {
    const int dy = y - cy;
    const int ady = std::abs(dy);
    if (ady >= ro) {
      continue;
    }
    const long ro2 = static_cast<long>(ro) * ro;
    const long dy2 = static_cast<long>(dy) * dy;
    const long xo2 = ro2 - dy2;
    if (xo2 < 0) {
      continue;
    }
    /** Ceil outer half-chord, floor inner half-chord so gray ring meets ink wedge without 1px white seams. */
    const int xo = static_cast<int>(ceilf(sqrtf(static_cast<float>(xo2)) - 1e-3f));
    auto span = [&](int xa, int xb) {
      if (xa > xb) {
        std::swap(xa, xb);
      }
      xa = std::max(0, xa);
      xb = std::min(scrW - 1, xb);
      if (xa <= xb) {
        renderer.rectangle.fill(xa, y, xb - xa + 1, 1, static_cast<int>(tone), false);
      }
    };
    if (ady >= ri) {
      span(cx - xo, cx + xo);
    } else {
      const long ri2 = static_cast<long>(ri) * ri;
      const long xi2 = ri2 - dy2;
      int xi = static_cast<int>(floorf(sqrtf(static_cast<float>(xi2)) + 1e-3f));
      xi = std::min(xi, xo);
      span(cx - xo, cx - xi);
      span(cx + xi, cx + xo);
    }
  }
}

void drawFullDonutGauge(const GfxRenderer& renderer, int cx, int cy, int rOut, int thick, float pct01, const char* centerPct) {
  const int thickMin = 6;
  const int thickMax = std::max(thickMin + 2, rOut - 8);
  const int thickUse = std::max(thickMin, std::min(thickMax, thick));
  const int rIn = rOut - thickUse;
  if (rIn <= 2 || rOut <= rIn + 2) {
    return;
  }
  const float twoPi = 2.f * kPi;
  /** Fill slightly inside outer/inner ink outlines so the ring reads solid (drawLine has no diagonals). */
  const int roFill = rOut - 1;
  const int riFill = rIn + 1;
  if (roFill > riFill + 1) {
    fillAnnulusToneScanlines(renderer, cx, cy, roFill, riFill, GfxRenderer::FillTone::Gray);
  }

  const float p = std::min(1.f, std::max(0.f, pct01));
  const float a0 = -kPi / 2.f;
  if (p >= 0.999f) {
    fillAnnulusToneScanlines(renderer, cx, cy, roFill, riFill, GfxRenderer::FillTone::Ink);
  } else if (p > 0.004f) {
    /** Tiny angular overlap so the ink wedge meets the gray ring at the start/end radii on a pixel grid. */
    constexpr float kSweepSlopRad = 0.02f;
    const float sweep = std::min(twoPi, p * twoPi + kSweepSlopRad);
    fillAnnulusWedgeInkPixels(renderer, cx, cy, roFill, riFill, a0 - kSweepSlopRad * 0.5f, sweep);
  }

  /** Outlines at real outer/inner radii only (no extra circles inside the hole). */
  drawCircleOutlinePixels(renderer, cx, cy, rOut);
  drawCircleOutlinePixels(renderer, cx, cy, rIn);

  const int tw = renderer.text.getWidth(FONT_SERIF_MD, centerPct);
  const int lhMd = renderer.text.getLineHeight(FONT_SERIF_MD);
  renderer.text.render(FONT_SERIF_MD, cx - tw / 2, cy - lhMd / 2, centerPct);
}

void drawVertRule(const GfxRenderer& renderer, int x, int y, int h) {
  if (h > 0) {
    renderer.line.render(x, y, x, y + h, true);
  }
}

/** Shared geometry for the global “all items” donut + metrics block (must stay in sync across draw paths). */
struct GlobalAllItemsGeom {
  int lhSans;
  int lhNum;
  int kCaptionStackH;
  int kMetricsPadT;
  int kMetricsH;
  int rowH;
};

static GlobalAllItemsGeom computeGlobalAllItemsGeom(const GfxRenderer& renderer) {
  const int lhSans = renderer.text.getLineHeight(FONT_SANS);
  const int lhSm = renderer.text.getLineHeight(FONT_SANS_SM);
  const int lhNum = renderer.text.getLineHeight(FONT_SERIF_LG);
  const int kCaptionStackH = lhSans * 2 + 6;
  constexpr int kMetricsPadT = 8;
  const int kMetricsH = kMetricsPadT + lhNum + 4 + lhSm;
  const int rowH = std::max(kGlobalAllItemsDonutPadT + kGlobalAllItemsDonutR + kGlobalAllItemsDonutR + kGlobalAllItemsDonutPadB,
                            kCaptionStackH + 4);
  return {lhSans, lhNum, kCaptionStackH, kMetricsPadT, kMetricsH, rowH};
}

/** Donut row + right captions only (gauge height = rowH). Donut is centered horizontally in the inner band. */
static void drawGlobalAllItemsGaugeRow(const GfxRenderer& renderer, int innerLeft, int innerRight, int y, float finishedRatio01,
                                       const GlobalAllItemsGeom& g) {
  const int innerW = innerRight - innerLeft;
  const int cx = innerLeft + innerW / 2;
  const int cy = y + kGlobalAllItemsDonutPadT + kGlobalAllItemsDonutR;

  char pct[16];
  snprintf(pct, sizeof(pct), "%.0f%%", finishedRatio01 * 100.f);
  drawFullDonutGauge(renderer, cx, cy, kGlobalAllItemsDonutR, kGlobalAllItemsDonutThick, finishedRatio01, pct);

  const char* line1 = "of your books";
  const char* line2 = "are finished.";
  const int xText = cx + kGlobalAllItemsDonutR + kGlobalAllItemsDonutTextGap;
  const int textW = std::max(0, innerRight - xText - 8);
  const int yText0 = y + (g.rowH - g.kCaptionStackH) / 2;
  const std::string cap1 = renderer.text.truncate(FONT_SANS, line1, textW);
  const std::string cap2 = renderer.text.truncate(FONT_SANS, line2, textW);
  renderer.text.render(FONT_SANS, xText, yText0, cap1.c_str());
  renderer.text.render(FONT_SANS, xText, yText0 + g.lhSans, cap2.c_str());
}

/**
 * Horizontal rule + two-column metrics (second band).
 * yRulePreferred: caller’s ideal rule Y (after gauge + gap).
 * yRuleMin: never place the rule above this (prevents the old min(y, yEnd-h) clamp from erasing the gap under the gauge).
 */
static int drawGlobalAllItemsSecondBand(const GfxRenderer& renderer, int innerLeft, int innerRight, int yRulePreferred,
                                        int yRuleMin, int yContentEnd, uint32_t booksFinished, uint32_t booksOpened,
                                        const GlobalAllItemsGeom& g) {
  const int innerW = innerRight - innerLeft;
  const int yMaxRule = yContentEnd - g.kMetricsH - 2;
  /** Prefer the caller’s Y, never above yMaxRule, never below yRuleMin when there is room (old code only did min→yMax, which stole the gap under the gauge). */
  const int capPref = std::min(yRulePreferred, yMaxRule);
  int yRule = std::min(yMaxRule, std::max(yRuleMin, capPref)) + 20;
  renderer.line.render(innerLeft, yRule, innerRight, yRule, true);
  const int midX = innerLeft + innerW / 2;
  drawVertRule(renderer, midX, yRule, g.kMetricsH);

  char buf[32];
  snprintf(buf, sizeof(buf), "%u", booksFinished);
  const int leftCx = innerLeft + (midX - innerLeft) / 2;
  const int twFin = renderer.text.getWidth(FONT_SERIF_LG, buf);
  const int yNum = yRule + g.kMetricsPadT;
  renderer.text.render(FONT_SERIF_LG, leftCx - twFin / 2, yNum, buf);
  const char* labFin = "Books finished";
  const int twLabF = renderer.text.getWidth(FONT_SANS_SM, labFin);
  const int yLabFin = yNum + g.lhNum + 4;
  renderer.text.render(FONT_SANS_SM, leftCx - twLabF / 2, yLabFin, labFin);

  snprintf(buf, sizeof(buf), "%u", booksOpened);
  const int rightCx = midX + (innerRight - midX) / 2;
  const int twH = renderer.text.getWidth(FONT_SERIF_LG, buf);
  renderer.text.render(FONT_SERIF_LG, rightCx - twH / 2, yNum, buf);
  const char* labH = "Books opened";
  const int twLabH = renderer.text.getWidth(FONT_SANS_SM, labH);
  renderer.text.render(FONT_SANS_SM, rightCx - twLabH / 2, yLabFin, labH);

  return yRule + g.kMetricsH;
}

/**
 * Bottom summary block (480×800): donut on the left, caption lines to the right; rule + two metrics below.
 * y = top of that block (caller leaves a small gap from the row above).
 */
int drawAllItems480x800(const GfxRenderer& renderer, int innerLeft, int innerRight, int y, int yContentEnd,
                         float finishedRatio01, uint32_t booksFinished, uint32_t booksOpened) {
  const GlobalAllItemsGeom g = computeGlobalAllItemsGeom(renderer);
  drawGlobalAllItemsGaugeRow(renderer, innerLeft, innerRight, y, finishedRatio01, g);
  constexpr int kRuleGap = 10;
  const int yRule = y + g.rowH + kRuleGap;
  return drawGlobalAllItemsSecondBand(renderer, innerLeft, innerRight, yRule, yRule, yContentEnd, booksFinished, booksOpened,
                                       g);
}

/** Must match drawAllItems480x800 geometry (global bottom block, single combined call). */
int measureAllItemsBodyHeight(const GfxRenderer& renderer) {
  const GlobalAllItemsGeom g = computeGlobalAllItemsGeom(renderer);
  constexpr int kRuleGap = 10;
  return g.rowH + kRuleGap + 1 + g.kMetricsH;
}

/**
 * Two-column grid with `numRows` rows: value (serif lg) + label (sans sm) per cell.
 * Each column width stops short of the center rule so labels never bleed across.
 * Row 0 is shifted up by `row0LiftPx` only; other rows unchanged. Horizontal rules between rows.
 * @param row0LiftPx use 0 when this grid must not intrude into the margin above `y` (e.g. global stats layout).
 * @param gapBeforeLastRowPx when numRows > 2, extra space inserted after the second-to-last row so the horizontal
 *        rule above the bottom row sits lower (avoids overlap between the middle row labels and the pages row).
 */
int drawFourColumnStatsNx2(const GfxRenderer& renderer, int innerLeft, int y, int innerW, const char* const* vals,
                           const char* const* labs, int numRows, int cellH, int row0LiftPx,
                           int gapBeforeLastRowPx = 0) {
  if (numRows < 1) {
    return 0;
  }
  constexpr int kPadBelowMidRule = 2;
  const int halfW = innerW / 2;
  const int midX = innerLeft + halfW;
  const int gap = (numRows > 2 ? gapBeforeLastRowPx : 0);
  std::vector<int> yBound(static_cast<size_t>(numRows + 1), y);
  for (int r = 1; r <= numRows; ++r) {
    yBound[static_cast<size_t>(r)] =
        yBound[static_cast<size_t>(r - 1)] + cellH + ((r == numRows - 1) ? gap : 0);
  }
  const int blockH = yBound[static_cast<size_t>(numRows)] - y;
  constexpr int kEdgePad = 8;
  constexpr int kMidGutter = 10;
  constexpr int kCellVMarginBottom = 4;
  constexpr int kCellPadTop = 0;
  const int wLeft = std::max(20, midX - innerLeft - kEdgePad - kMidGutter);
  const int wRight = std::max(20, (innerLeft + innerW) - midX - kEdgePad - kMidGutter);
  drawVertRule(renderer, midX, y, blockH);
  for (int r = 1; r < numRows; ++r) {
    renderer.line.render(innerLeft, yBound[static_cast<size_t>(r)], innerLeft + innerW, yBound[static_cast<size_t>(r)],
                      true);
  }

  const int lhVal = renderer.text.getLineHeight(FONT_SERIF_LG);
  const int lhLab = renderer.text.getLineHeight(FONT_SANS_SM);
  constexpr int kValLabGap = 4;
  const int stackH = lhVal + kValLabGap + lhLab;

  auto cell = [&](int col, int row, const char* val, const char* lab) {
    const int cellLeft = (col == 0) ? (innerLeft + kEdgePad) : (midX + kMidGutter);
    const int cw = (col == 0) ? wLeft : wRight;
    const int bandTop = yBound[static_cast<size_t>(row)] + (row == 0 ? kCellPadTop : kPadBelowMidRule);
    const int bandBottom = yBound[static_cast<size_t>(row + 1)] - kCellVMarginBottom;
    const int innerBand = std::max(1, bandBottom - bandTop);
    int rowTop;
    if (row == 0) {
      const int floorY = std::max(0, y - row0LiftPx);
      rowTop = bandTop - row0LiftPx;
      if (rowTop < floorY) {
        rowTop = floorY;
      }
      if (rowTop + stackH > bandBottom) {
        rowTop = bandBottom - stackH;
      }
    } else {
      rowTop = (stackH <= innerBand) ? bandTop : std::max(bandTop, bandBottom - stackH);
    }
    const std::string valT = renderer.text.truncate(FONT_SERIF_LG, val, cw);
    const std::string labT = renderer.text.truncate(FONT_SANS_SM, lab, cw);
    renderer.text.render(FONT_SERIF_LG, cellLeft, rowTop, valT.c_str());
    renderer.text.render(FONT_SANS_SM, cellLeft, rowTop + lhVal + kValLabGap, labT.c_str());
  };
  for (int row = 0; row < numRows; ++row) {
    cell(0, row, vals[row * 2], labs[row * 2]);
    cell(1, row, vals[row * 2 + 1], labs[row * 2 + 1]);
  }
  return blockH;
}

int drawFourColumnStats2x2(const GfxRenderer& renderer, int innerLeft, int y, int innerW, const char* v0, const char* l0,
                           const char* v1, const char* l1, const char* v2, const char* l2, const char* v3,
                           const char* l3, int cellH, int row0LiftPx) {
  const char* vals[] = {v0, v1, v2, v3};
  const char* labs[] = {l0, l1, l2, l3};
  return drawFourColumnStatsNx2(renderer, innerLeft, y, innerW, vals, labs, 2, cellH, row0LiftPx, 0);
}

}

void StatisticActivity::loadStats() {
  ScreenComponents::LoadingProgressLayout layout =
      ScreenComponents::LoadingProgress::show(renderer, "Loading statistics...", 12);
  allBooksStats = getAllBooksStats();
  ScreenComponents::LoadingProgress::setProgress(renderer, layout, 40);
  std::sort(allBooksStats.begin(), allBooksStats.end(),
            [](const BookReadingStats& a, const BookReadingStats& b) { return a.lastReadTimeMs > b.lastReadTimeMs; });
  ScreenComponents::LoadingProgress::setProgress(renderer, layout, 65);
  globalStats = aggregateGlobalStatsFromBooks(allBooksStats);
  ScreenComponents::LoadingProgress::setProgress(renderer, layout, 90);
  viewIndex = 0;
  ScreenComponents::LoadingProgress::setProgress(renderer, layout, 100);
  saveGlobalStats(globalStats);
}

void StatisticActivity::hydrateFromStorage() {
  allBooksStats = getAllBooksStats();
  std::sort(allBooksStats.begin(), allBooksStats.end(),
            [](const BookReadingStats& a, const BookReadingStats& b) { return a.lastReadTimeMs > b.lastReadTimeMs; });
  if (!loadGlobalStats(globalStats)) {
    globalStats = aggregateGlobalStatsFromBooks(allBooksStats);
    saveGlobalStats(globalStats);
  }
}

void StatisticActivity::renderCover(const std::string& bookPath, int x, int y, int width, int height,
                                    const std::string& title, const std::string& author) const {
  const std::string coverJpegPath = bookPath + "/thumb.jpg";
  std::string coverPath = bookPath + "/thumb.bmp";
  bool coverDrawn = false;

  if (SdMan.exists(coverJpegPath.c_str())) {
    JpegRenderer jpeg(renderer);
    coverDrawn = jpeg.fromPath(coverJpegPath, x + 2, y + 2, std::max(1, width - 4), std::max(1, height - 4), true);
  }
  FsFile file;
  if (!coverDrawn && SdMan.openFileForRead("COVER", coverPath.c_str(), file)) {
    Bitmap bitmap(file, BitmapDitherMode::None);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      const int maxW = std::max(1, width - 4);
      const int maxH = std::max(1, height - 4);
      BitmapGrayStyleScope displayGrayStyle(renderer, displayImageBitmapGrayStyle());
      drawStatsThumbnailInRect(renderer, bitmap, x + 2, y + 2, maxW, maxH);
      coverDrawn = true;
    }
    file.close();
  }

  if (coverDrawn) {
    return;
  }

  const bool rr = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(x, y, width, height, false, rr);
  if (!rr) {
    renderer.rectangle.render(x, y, width, height, true, false);
  }

  if (!title.empty()) {
    int lineY = y + 18;
    int maxWidth = width - 24;
    int lineHeight = renderer.text.getLineHeight(FONT_SERIF_SM);

    std::string remaining = title;
    int lineCount = 0;

    while (!remaining.empty() && lineCount < 3) {
      std::string line;
      int lineWidth = 0;

      while (!remaining.empty()) {
        size_t spacePos = remaining.find(' ');
        std::string word = (spacePos != std::string::npos) ? remaining.substr(0, spacePos) : remaining;

        int wordWidth = renderer.text.getWidth(FONT_SERIF_SM, word.c_str(), EpdFontFamily::REGULAR);

        if (lineWidth + wordWidth <= maxWidth) {
          if (!line.empty()) line += " ";
          line += word;
          lineWidth += wordWidth;

          if (spacePos != std::string::npos) {
            remaining = remaining.substr(spacePos + 1);
          } else {
            remaining.clear();
          }
        } else {
          break;
        }
      }

      if (line.empty()) {
        break;
      }

      int textWidth = renderer.text.getWidth(FONT_SERIF_SM, line.c_str(), EpdFontFamily::REGULAR);
      int textX = x + (width - textWidth) / 2;
      renderer.text.render(FONT_SERIF_SM, textX, lineY, line.c_str(), true, EpdFontFamily::REGULAR);
      lineY += lineHeight;
      lineCount++;
    }
  }

  if (!author.empty()) {
    std::string authorText = author;
    int authorWidth = renderer.text.getWidth(FONT_SANS, authorText.c_str());
    int authorX = x + (width - authorWidth) / 2;
    int authorY = y + height - 22;
    renderer.text.render(FONT_SANS, authorX, authorY, authorText.c_str());
  }
}

std::pair<int, int> StatisticActivity::drawGlobalRecentThumbBlock(int boxX, int yTop, const std::string& bookPath,
                                                                  const std::string& title) const {
  constexpr int kMaxBoxW = 165;
  constexpr int kMaxBoxH = 182;
  constexpr int kOuterPad = 2;
  const int availW = std::max(1, kMaxBoxW - 4);
  const int availH = std::max(1, kMaxBoxH - 4);

  const std::string coverJpegPath = bookPath + "/thumb.jpg";
  std::string coverPath = bookPath + "/thumb.bmp";

  if (SdMan.exists(coverJpegPath.c_str())) {
    const int coverW = availW + 4;
    const int coverH = availH + 4;
    const int frameW = coverW + 2 * kOuterPad;
    const int frameH = coverH + 2 * kOuterPad;
    const int innerX = boxX + kOuterPad;
    const int innerY = yTop + kOuterPad;
    const bool rr = SETTINGS.bitmapRoundedCorners != 0;
    renderer.rectangle.fill(boxX, yTop, frameW, frameH, false, rr);
    if (!rr) {
      renderer.rectangle.render(boxX, yTop, frameW, frameH, true, false);
    }
    JpegRenderer jpeg(renderer);
    if (jpeg.fromPath(coverJpegPath, innerX + 2, innerY + 2, availW, availH, true)) {
      return {frameW, frameH};
    }
  }

  FsFile file;
  if (SdMan.openFileForRead("COVER", coverPath.c_str(), file)) {
    const int coverW = availW + 4;
    const int coverH = availH + 4;
    const int frameW = coverW + 2 * kOuterPad;
    const int frameH = coverH + 2 * kOuterPad;
    const int innerX = boxX + kOuterPad;
    const int innerY = yTop + kOuterPad;
    const bool rr = SETTINGS.bitmapRoundedCorners != 0;
    renderer.rectangle.fill(boxX, yTop, frameW, frameH, false, rr);
    if (!rr) {
      renderer.rectangle.render(boxX, yTop, frameW, frameH, true, false);
    }
    Bitmap bitmap(file, BitmapDitherMode::None);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      const int bw = bitmap.getWidth();
      const int bh = bitmap.getHeight();
      if (bw > 0 && bh > 0) {
        {
          BitmapGrayStyleScope displayGrayStyle(renderer, displayImageBitmapGrayStyle());
          drawStatsThumbnailInRect(renderer, bitmap, innerX + 2, innerY + 2, availW, availH);
        }
        file.close();
        return {frameW, frameH};
      }
    }
    file.close();
  }

  constexpr int kFallbackW = 120;
  constexpr int kFallbackH = 132;
  const int coverW = kFallbackW + 4;
  const int coverH = kFallbackH + 4;
  const int frameW = coverW + 2 * kOuterPad;
  const int frameH = coverH + 2 * kOuterPad;
  const bool rr = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(boxX, yTop, frameW, frameH, false, rr);
  if (!rr) {
    renderer.rectangle.render(boxX, yTop, frameW, frameH, true, false);
  }
  renderCover(bookPath, boxX + kOuterPad, yTop + kOuterPad, coverW, coverH, title, "");
  return {frameW, frameH};
}

void StatisticActivity::onEnter() {
  Activity::onEnter();

  hydrateFromStorage();
  viewIndex = 0;

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  render();
  SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Statistics);
}

void StatisticActivity::onExit() {
  Activity::onExit();

  allBooksStats.clear();
  allBooksStats.shrink_to_fit();
}

int StatisticActivity::renderHeader(int y, int innerLeft, int innerRight, int innerW, int Margin) const {
  (void)innerRight;
  const int lhLG = renderer.text.getLineHeight(FONT_SERIF_LG);
  const char* screenTitle = "Reading stats";
  const int maxTitleW = std::max(8, innerW - Margin * 2);
  const std::string titleShown = renderer.text.truncate(FONT_SERIF_LG, screenTitle, maxTitleW);
  renderer.text.render(FONT_SERIF_LG, innerLeft, y, titleShown.c_str());
  return y + lhLG + Margin;
}

int StatisticActivity::renderRecent(int y, int innerLeft, int innerRight, int innerW, int Margin) const {
  constexpr int kThumbTextGap = 15;
  constexpr int kGlobalThumbOuterPad = 2;
  constexpr int g8 = 8;
  constexpr int g10 = 10;

  const int lhSerif = renderer.text.getLineHeight(FONT_SERIF);
  const int lhSans = renderer.text.getLineHeight(FONT_SANS);
  const int lhSm = renderer.text.getLineHeight(FONT_SANS_SM);
  const int yCoverTop = y;

  std::pair<int, int> tf;
  int yThumbBottom;
  if (!allBooksStats.empty()) {
    const BookReadingStats& cur = allBooksStats[0];
    const float prog = (cur.progressPercent >= 0.f)
                           ? std::min(1.f, std::max(0.f, cur.progressPercent / 100.f))
                           : 0.f;
    tf = drawGlobalRecentThumbBlock(innerLeft, yCoverTop, cur.path, cur.title);
    const int textX = innerLeft + tf.first + kThumbTextGap;
    const int textColW = std::max(40, innerRight - textX);

    const int yTitle = yCoverTop + kGlobalThumbOuterPad;
    std::string titleLine =
        renderer.text.truncate(FONT_SERIF, cur.title.c_str(), textColW, EpdFontFamily::ITALIC);
    renderer.text.render(FONT_SERIF, textX, yTitle, titleLine.c_str(), true, EpdFontFamily::ITALIC);
    const int yAuthor = yTitle + lhSerif + g8;
    if (!cur.author.empty()) {
      std::string auth = renderer.text.truncate(FONT_SANS, cur.author.c_str(), textColW);
      renderer.text.render(FONT_SANS, textX, yAuthor, auth.c_str());
    }
    const int yProg = yAuthor + lhSans + g10;
    char progLabel[48];
    snprintf(progLabel, sizeof(progLabel), "Book progress: %.0f%%", prog * 100.f);
    renderer.text.render(FONT_SANS_SM, textX, yProg, progLabel);
    const int yBar = yProg + lhSm + g8;
    drawThinProgressBar(renderer, textX, yBar, textColW, 8, prog);
    yThumbBottom = yBar + 8;
  } else {
    tf = drawGlobalRecentThumbBlock(innerLeft, yCoverTop, "", "");
    const int nw = renderer.text.getWidth(FONT_SANS, "No recent book");
    const int yEmpty = yCoverTop + (tf.second - lhSans) / 2;
    renderer.text.render(FONT_SANS, innerLeft + (tf.first - nw) / 2, yEmpty, "No recent book");
    yThumbBottom = yCoverTop + tf.second;
  }

  const int thumbHeightPx = std::max(tf.second, yThumbBottom - yCoverTop);
  return yCoverTop + thumbHeightPx + Margin;
}

int StatisticActivity::renderFirstGrid(int y, int innerLeft, int innerW, int Margin) const {
  constexpr int kStatsRowH = 58;
  const int hFirstGrid = kStatsRowH * 2;

  char v0[20], v1[20], v2[20], v3[20];
  const float totalHrs = static_cast<float>(globalStats.totalReadingTimeMs) / 3600000.f;
  snprintf(v0, sizeof(v0), "%.1f", totalHrs);
  const float avgMinPerSess =
      globalStats.totalSessions > 0
          ? static_cast<float>(globalStats.totalReadingTimeMs) / 60000.f / static_cast<float>(globalStats.totalSessions)
          : 0.f;
  snprintf(v1, sizeof(v1), "%.0f", avgMinPerSess);
  snprintf(v2, sizeof(v2), "%u", globalStats.totalPagesRead);
  const float readMin = static_cast<float>(globalStats.totalReadingTimeMs) / 60000.f;
  const float pgPerMin = readMin > 0.01f ? static_cast<float>(globalStats.totalPagesRead) / readMin : 0.f;
  snprintf(v3, sizeof(v3), "%.1f", pgPerMin);

  drawFourColumnStats2x2(renderer, innerLeft, y, innerW, v0, "Total hours", v1, "Avg. min/session", v2, "Pages read", v3,
                         "Pages per min", kStatsRowH, 0);
  return y + hFirstGrid + Margin;
}

int StatisticActivity::renderGuage(int y, int innerLeft, int innerRight, int Margin) const {
  const GlobalAllItemsGeom g = computeGlobalAllItemsGeom(renderer);
  float ratio = 0.f;
  if (globalStats.totalBooksStarted > 0) {
    ratio = std::min(1.f, static_cast<float>(globalStats.totalBooksFinished) /
                             static_cast<float>(globalStats.totalBooksStarted));
  }
  drawGlobalAllItemsGaugeRow(renderer, innerLeft, innerRight, y, ratio, g);
  return y + g.rowH + Margin;
}

void StatisticActivity::renderSecondGrid(int y, int innerLeft, int innerRight, int contentBottom) const {
  const GlobalAllItemsGeom g = computeGlobalAllItemsGeom(renderer);
  drawGlobalAllItemsSecondBand(renderer, innerLeft, innerRight, y, y, contentBottom, globalStats.totalBooksFinished,
                               globalStats.totalBooksStarted, g);
}

void StatisticActivity::renderSingleBookView(int bookIdx, int contentTop, int contentBottom) const {
  if (bookIdx < 0 || bookIdx >= static_cast<int>(allBooksStats.size())) {
    return;
  }
  const BookReadingStats& b = allBooksStats[static_cast<size_t>(bookIdx)];
  constexpr int kScreenW = 480;
  constexpr int kMarginX = 20;
  constexpr int g8 = 8;
  constexpr int g10 = 10;
  /** Taller rows than global stats; extra tail height so the pages row sits below the mid divider cleanly. */
  constexpr int kSingleBookStatsRowH = 66;
  constexpr int kSingleBookStatsLastRowExtraPx = 18;

  const int innerLeft = kMarginX;
  const int innerRight = kScreenW - kMarginX;
  const int innerW = innerRight - innerLeft;
  const int y0 = contentTop + 4;
  const int yEnd = contentBottom - 24;

  const int lhLG = renderer.text.getLineHeight(FONT_SERIF_LG);
  const int lhSerif = renderer.text.getLineHeight(FONT_SERIF);
  const int lhSans = renderer.text.getLineHeight(FONT_SANS);
  /** Title and author below cover row; sessions/chapters are in the bottom stats grid (same style as hours). */
  const int metaSpan = lhSerif + g8 + lhSans + g10;
  constexpr int gapCoverTitle = 6;
  constexpr int gapMetaStats = 12;
  const int hStats = kSingleBookStatsRowH * 3 + kSingleBookStatsLastRowExtraPx;
  constexpr int kSingleBookStatsGridLiftPx = 20;
  const int yStatsTop = yEnd - hStats - kSingleBookStatsGridLiftPx;
  const int maxTitleY = yStatsTop - gapMetaStats - metaSpan;

  constexpr int kTitlePad = 10;
  const char* screenTitle = "Reading stats";
  const int maxTitleW = std::max(8, innerW - kTitlePad * 2);
  const std::string titleShown = renderer.text.truncate(FONT_SERIF_LG, screenTitle, maxTitleW);
  renderer.text.render(FONT_SERIF_LG, innerLeft, y0, titleShown.c_str());
  int y = y0 + lhLG + 4;
  y += g8;
  const int yCoverTop = y;

  /** Donut anchored toward the right margin with a wide gap from the cover. */
  constexpr int kBookDonutR = 76;
  constexpr int kBookDonutThick = 11;
  constexpr int kCoverGaugeGap = 32;
  constexpr int kGaugeRightMargin = 55;
  const int cxGauge = innerRight - kGaugeRightMargin - kBookDonutR;
  const int maxCoverW = std::max(100, cxGauge - kBookDonutR - kCoverGaugeGap - innerLeft);

  const int coverAllow = std::max(0, maxTitleY - gapCoverTitle - yCoverTop);
  int coverH = std::max(std::min(260, coverAllow), std::min(120, coverAllow));
  int coverW = std::min(maxCoverW, (coverH * 2) / 3);
  if (coverW < 100) {
    coverW = 100;
  }
  coverH = std::min(coverH, (coverW * 3) / 2);
  coverH = std::min(coverH, coverAllow);

  const float prog =
      (b.progressPercent >= 0.f) ? std::min(1.f, std::max(0.f, b.progressPercent / 100.f)) : 0.f;
  const int boxX = innerLeft;
  const bool rr = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(boxX, yCoverTop, coverW, coverH, false, rr);
  if (!rr) {
    renderer.rectangle.render(boxX, yCoverTop, coverW, coverH, true, false);
  }
  renderCover(b.path, boxX + 1, yCoverTop + 1, coverW - 2, coverH - 2, b.title, "");

  const int rowHeight = std::max(coverH, 2 * kBookDonutR + 4);
  const int cyGauge = yCoverTop + rowHeight / 2;
  char pctStr[16];
  snprintf(pctStr, sizeof(pctStr), "%.0f%%", prog * 100.f);
  drawFullDonutGauge(renderer, cxGauge, cyGauge, kBookDonutR, kBookDonutThick, prog, pctStr);

  const int textMaxW = innerW;
  std::string titleLine = renderer.text.truncate(FONT_SERIF, b.title.c_str(), textMaxW, EpdFontFamily::ITALIC);
  const int yTitle = yCoverTop + rowHeight + gapCoverTitle;
  renderer.text.render(FONT_SERIF, innerLeft, yTitle, titleLine.c_str(), true, EpdFontFamily::ITALIC);
  const int yAuthor = yTitle + lhSerif + g8;
  if (!b.author.empty()) {
    std::string auth = renderer.text.truncate(FONT_SANS, b.author.c_str(), textMaxW);
    renderer.text.render(FONT_SANS, innerLeft, yAuthor, auth.c_str());
  }

  char v0[20], v1[20], v2[20], v3[20], vSess[16], vChap[16];
  const float bookHrs = static_cast<float>(b.totalReadingTimeMs) / 3600000.f;
  snprintf(v0, sizeof(v0), "%.1f", bookHrs);
  const float bookAvgMin = b.sessionCount > 0 ? static_cast<float>(b.totalReadingTimeMs) / 60000.f /
                                                  static_cast<float>(b.sessionCount)
                                              : 0.f;
  snprintf(v1, sizeof(v1), "%.0f", bookAvgMin);
  snprintf(v2, sizeof(v2), "%u", b.totalPagesRead);
  const float bookReadMin = static_cast<float>(b.totalReadingTimeMs) / 60000.f;
  const float bookPgPerMin = bookReadMin > 0.01f ? static_cast<float>(b.totalPagesRead) / bookReadMin : 0.f;
  snprintf(v3, sizeof(v3), "%.1f", bookPgPerMin);
  snprintf(vSess, sizeof(vSess), "%u", static_cast<unsigned>(b.sessionCount));
  snprintf(vChap, sizeof(vChap), "%u", static_cast<unsigned>(b.totalChaptersRead));

  const char* vals[] = {v0, v1, vSess, vChap, v2, v3};
  const char* labs[] = {"Total hours", "Avg. min/session", "Sessions", "Chapters read", "Pages read", "Pages per min"};
  drawFourColumnStatsNx2(renderer, innerLeft, yStatsTop + 20, innerW, vals, labs, 3, kSingleBookStatsRowH, 28,
                         kSingleBookStatsLastRowExtraPx);

  char footer[24];
  snprintf(footer, sizeof(footer), "%d/%zu", bookIdx + 1, allBooksStats.size());
  renderer.text.centered(FONT_SANS_SM, contentBottom - 5, footer);
}

void StatisticActivity::render() {
  renderer.clearScreen();
  renderTabBar(renderer);

  constexpr int kHintReserve = 54;
  const int screenH = renderer.getScreenHeight();
  const int contentBottom = screenH - kHintReserve;
  const int contentTopSingle = TAB_BAR_HEIGHT + 4;

  const int totalViews = 1 + static_cast<int>(allBooksStats.size());
  int v = viewIndex;
  if (v < 0) v = 0;
  if (v >= totalViews) v = totalViews - 1;

  if (v == 0) {
    constexpr int Margin = 10;
    constexpr int kMarginX = 30;
    const int innerLeft = kMarginX;
    const int innerRight = renderer.getScreenWidth() - kMarginX;
    const int innerW = innerRight - innerLeft;

    int GAP = 0;
    GAP = TAB_BAR_HEIGHT + GAP;
    GAP = renderHeader(GAP, innerLeft, innerRight, innerW, Margin);
    GAP = renderRecent(GAP, innerLeft, innerRight, innerW, Margin);
    GAP = renderFirstGrid(GAP + kMarginX, innerLeft, innerW, Margin);
    GAP = renderGuage(GAP + kMarginX - 10, innerLeft - 130, innerRight, Margin);
    renderSecondGrid(GAP + kMarginX, innerLeft, innerRight, contentBottom);
  } else {
    renderSingleBookView(v - 1, contentTopSingle, contentBottom);
  }

  const auto labels = mappedInput.mapLabels("\xC2\xAB Recent", "Refresh", "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void StatisticActivity::loop() {
  if (tabSelectorIndex == 4 && updateRequired) {
    updateRequired = false;
    render();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    updateRequired = true;
    return;
  }

  if (Activity::mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (Activity::mappedInput.getHeldTime() >= GO_HOME_MS) return;
    onGoToRecent();
    return;
  }

  const bool leftPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool rightPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool upPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Down);
  const bool confirmPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Confirm);

  if (leftPressed) {
    tabSelectorIndex = 3;
    navigateToSelectedMenu();
    return;
  }

  if (rightPressed) {
    tabSelectorIndex = 0;
    navigateToSelectedMenu();
    return;
  }

  if (tabSelectorIndex != 4) {
    return;
  }

  if (confirmPressed) {
    loadStats();
    updateRequired = true;
    return;
  }

  const int totalViews = 1 + static_cast<int>(allBooksStats.size());
  if (totalViews <= 1) {
    return;
  }

  if (upPressed) {
    if (viewIndex > 0) {
      viewIndex--;
      updateRequired = true;
    }
    return;
  }

  if (downPressed) {
    if (viewIndex < totalViews - 1) {
      viewIndex++;
      updateRequired = true;
    }
    return;
  }
}
