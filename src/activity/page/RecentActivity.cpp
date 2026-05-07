/**
 * @file RecentActivity.cpp
 * @brief Definitions for RecentActivity.
 */

#include "RecentActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <JpegRenderer.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

#include "images/Down.h"
#include "images/Up.h"
#include "state/BookState.h"
#include "state/ImageBitmapGrayMaps.h"
#include "state/Statistics.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

#include "images/Star.h"

namespace {

/**
 * Extracts the base filename from a full path.
 * Removes directory path and file extension, then replaces underscores with spaces.
 */
static std::string getBaseFilename(const std::string& filename) {
  size_t lastSlash = filename.find_last_of('/');
  std::string basename = (lastSlash != std::string::npos) ? filename.substr(lastSlash + 1) : filename;
  size_t lastDot = basename.find_last_of('.');
  if (lastDot != std::string::npos) {
    basename.resize(lastDot);
  }
  std::replace(basename.begin(), basename.end(), '_', ' ');
  return basename;
}

/**
 * Formats a title by capitalizing the first letter of each word.
 */
static std::string formatTitle(const std::string& title) {
  std::string formatted = title;
  bool capitalizeNext = true;
  for (char& c : formatted) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      capitalizeNext = true;
    } else if (capitalizeNext) {
      c = std::toupper(static_cast<unsigned char>(c));
      capitalizeNext = false;
    }
  }
  return formatted;
}

static std::string bookDisplayTitle(const RecentBook& book) {
  if (!book.title.empty()) {
    return book.title;
  }
  return formatTitle(getBaseFilename(book.path));
}

constexpr unsigned long GO_HOME_MS = 1000;

/** O(1): bounded switch on RECENT_LIBRARY_MODE (fixed enum cardinality). */
static RecentActivity::ViewMode viewModeForLibrarySetting(uint8_t mode) {
  using SM = SystemSetting;
  switch (mode) {
    case SM::RECENT_GRID:
      return RecentActivity::ViewMode::Grid;
    case SM::RECENT_LIST:
      return RecentActivity::ViewMode::Default;
    case SM::RECENT_SIMPLE:
      return RecentActivity::ViewMode::SimpleUi;
    case SM::RECENT_BOOK_LIST:
      return RecentActivity::ViewMode::List;
    case SM::RECENT_ICONS:
      return RecentActivity::ViewMode::Icons;
    case SM::RECENT_FLOW:
    default:
      return RecentActivity::ViewMode::Flow;
  }
}

/** O(1) vs library size: at most RecentActivity::MAX_RECENT_BOOKS path compares. */
static bool recentBooksContainPath(const std::vector<RecentBook>& books, const std::string& path) {
  for (const auto& b : books) {
    if (b.path == path) {
      return true;
    }
  }
  return false;
}

/** No-cover: stats-style double frame + title one word per line, each line centered (see StatisticActivity::renderCover). */
static void drawRecentNoCoverPlaceholder(GfxRenderer& renderer, int x, int y, int w, int h, const std::string& title,
                                          int fontId) {
  if (w <= 1 || h <= 1) {
    return;
  }
  const bool rr = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(x, y, w, h, false, rr);
  if (!rr) {
    renderer.rectangle.render(x, y, w, h, true, false);
    if (w > 6 && h > 6) {
      renderer.rectangle.render(x + 2, y + 2, w - 4, h - 4, true, false);
    }
  }

  std::vector<std::string> words;
  std::string remaining = title;
  while (!remaining.empty()) {
    size_t sp = remaining.find(' ');
    if (sp == std::string::npos) {
      words.push_back(remaining);
      break;
    }
    if (sp > 0) {
      words.push_back(remaining.substr(0, sp));
    }
    remaining = (sp + 1 < remaining.size()) ? remaining.substr(sp + 1) : std::string();
  }
  if (words.empty()) {
    return;
  }

  const int lh = renderer.text.getLineHeight(fontId);
  const int maxLines = std::max(1, (h - 12) / std::max(1, lh));
  if (static_cast<int>(words.size()) > maxLines) {
    words.resize(static_cast<size_t>(maxLines));
  }
  const int totalTextH = static_cast<int>(words.size()) * lh;
  int lineY = y + std::max(4, (h - totalTextH) / 2);
  const int innerPad = 6;
  const int maxWordW = std::max(8, w - 2 * innerPad);

  for (const auto& word : words) {
    std::string wshow = word;
    if (renderer.text.getWidth(fontId, wshow.c_str()) > maxWordW) {
      wshow = renderer.text.truncate(fontId, wshow.c_str(), maxWordW, EpdFontFamily::REGULAR);
    }
    const int tw = renderer.text.getWidth(fontId, wshow.c_str());
    renderer.text.render(fontId, x + (w - tw) / 2, lineY, wshow.c_str(), true, EpdFontFamily::REGULAR);
    lineY += lh;
  }
}

static std::string epubCachePathForBookPath(const std::string& bookPath) {
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(bookPath));
}

/** Same light “gray” as `renderFlow` carousel: sparse ink checker (not `FillTone::Gray`). */
static void drawFlowCarouselBackdrop(const GfxRenderer& renderer, int rx, int ry, int rw, int rh) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  // Same even-even 1/4 ink lattice as GfxRenderer corner mask (SparseInkAlignedOutside) for seamless edges.
  for (int y = (ry - 5 + 1) & ~1; y < ry + rh + 10; y += 2) {
    if (y < 0 || y >= screenH) {
      continue;
    }
    for (int x = ((rx - 5) + 1) & ~1; x < rx + rw + 10; x += 2) {
      if (x >= 0 && x < screenW) {
        renderer.drawPixel(x, y, true);
      }
    }
  }
}

/** Flow-style dither strictly inside the rectangle (does not bleed into the white bottom pane). */
static void drawFlowCarouselBackdropInRect(const GfxRenderer& renderer, int rx, int ry, int rw, int rh) {
  if (rw <= 0 || rh <= 0) {
    return;
  }
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int x1 = std::max(0, rx);
  const int y1 = std::max(0, ry);
  const int x2 = std::min(screenW, rx + rw);
  const int y2 = std::min(screenH, ry + rh);
  for (int y = (y1 + 1) & ~1; y < y2; y += 2) {
    for (int x = (x1 + 1) & ~1; x < x2; x += 2) {
      renderer.drawPixel(x, y, true);
    }
  }
}

}  

namespace {
constexpr int kRecentThumbGap = 20;
constexpr int kRecentStripSlots = 2;

/** Simple UI favorites: at most this many titles; list scrolls when more than visible rows. */
constexpr int kSimpleUiFavoritesMaxCount = 10;
constexpr int kSimpleUiFavoritesVisibleMax = 5;
constexpr int kFavHeaderPadTop = 10;
constexpr int kFavHeaderPadBottom = 8;

struct SimpleUiMetrics {
  int bodyTop = 0;
  int bodyBottom = 0;
  int marginL = 0;
  int thumbW = 0;
  int thumbH = 0;
  int topBandH = 0;
  int favTop = 0;
  /** Height from favTop through the "Favorites" header and its separator line (list starts below). */
  int favHeaderBlockH = 0;
  int favListTop = 0;
  int rowH = 0;
  int maxVis = 0;
};

/** Matches `renderSimpleUi` geometry for input clamping. */
inline SimpleUiMetrics computeSimpleUiMetrics(const GfxRenderer& renderer) {
  constexpr int kTabBarH = 65;  
  SimpleUiMetrics m;
  m.bodyTop = kTabBarH - 6 + 8;
  constexpr int kHintReserve = 52;
  m.bodyBottom = renderer.getScreenHeight() - kHintReserve;
  m.marginL = RecentActivity::GRID_SPACING;

  constexpr int kThumbPadV = 28;
  const int favFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
  const int lh = renderer.text.getLineHeight(favFont);
  constexpr int kPadY = 20;
  m.rowH = lh + kPadY * 2;
  m.favHeaderBlockH = kFavHeaderPadTop + lh + kFavHeaderPadBottom + 1;
  // Shrink the top (recent) band so the pane below always fits the header + 5 favorite rows.
  const int minFavoritesPaneH = m.favHeaderBlockH + kSimpleUiFavoritesVisibleMax * m.rowH;
  const int bodySpan = m.bodyBottom - m.bodyTop;
  const int maxTopH = std::max(100, bodySpan - minFavoritesPaneH);

  m.thumbW = RecentActivity::COVER_WIDTH;
  m.thumbH = RecentActivity::COVER_HEIGHT;
  m.topBandH = m.thumbH + kThumbPadV;
  if (m.topBandH > maxTopH) {
    m.topBandH = maxTopH;
    m.thumbH = std::max(100, m.topBandH - kThumbPadV);
    m.thumbW = m.thumbH * RecentActivity::COVER_WIDTH / RecentActivity::COVER_HEIGHT;
  }
  m.favTop = m.bodyTop + m.topBandH;
  m.favListTop = m.favTop + m.favHeaderBlockH;
  // Rows occupy [rowY, rowY + rowH); last pixel rowY + rowH - 1 must stay within bodyBottom.
  m.maxVis = std::min(
      kSimpleUiFavoritesVisibleMax,
      std::max(1, (m.bodyBottom - m.favListTop) / std::max(1, m.rowH)));
  return m;
}

/** Keep horizontal strip window so `selectorIndex` is one of the two visible slots. */
inline void clampRecentStripHScroll(int sel, int bookCount, int& hScroll) {
  if (bookCount <= 0) {
    return;
  }
  const int maxH = std::max(0, bookCount - kRecentStripSlots);
  int h = std::min(maxH, sel);
  h = std::max(0, std::max(h, sel - (kRecentStripSlots - 1)));
  hScroll = h;
}
}  // namespace

void RecentActivity::drawRecentThumbnailAt(int x, int y, int w, int h, const std::string& cacheDir,
                                           const std::string& placeholderTitle, int placeholderFontId,
                                           const bool roundedCornerBackdropIsDither) {
  if (w < 8 || h < 8) {
    return;
  }
  if (cacheDir.empty()) {
    drawRecentNoCoverPlaceholder(renderer, x, y, w, h, placeholderTitle, placeholderFontId);
    return;
  }
  char path[192];
  snprintf(path, sizeof(path), "%s/thumb.bmp", cacheDir.c_str());
  char jpegPath[192];
  snprintf(jpegPath, sizeof(jpegPath), "%s/thumb.jpg", cacheDir.c_str());

  if (SdMan.exists(jpegPath)) {
    JpegRenderer jpeg(renderer);
    if (jpeg.fromPath(jpegPath, x, y, w, h, true)) {
      return;
    }
  }

  FsFile file;
  if (SdMan.openFileForRead("RECENT", path, file)) {
    Bitmap bitmap(file, BitmapDitherMode::None);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      BitmapGrayStyleScope displayGray(renderer, displayImageBitmapGrayStyle());
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
      const BitmapRender::RoundedOutside roundedOutside =
          SETTINGS.bitmapRoundedCorners == 0
              ? BitmapRender::RoundedOutside::None
              : (roundedCornerBackdropIsDither ? BitmapRender::RoundedOutside::SparseInkAlignedOutside
                                               : BitmapRender::RoundedOutside::PaperOutside);
      renderer.bitmap.render(bitmap, x, y, w, h, cropX, cropY, roundedOutside);
      file.close();
      return;
    }
    file.close();
  }

  drawRecentNoCoverPlaceholder(renderer, x, y, w, h, placeholderTitle, placeholderFontId);
}

void RecentActivity::renderDefaultStatsGrid(int gridStartY, int screenW) {
  const int sel = selectorIndex;
  const int n = static_cast<int>(recentBooks.size());
  if (sel < 0 || sel >= n || screenW < 40) {
    return;
  }

  const RecentBook& curBook = recentBooks[static_cast<size_t>(sel)];
  BookReadingStats stats;
  const bool hasStats = loadBookStats(curBook.cachePath.c_str(), stats);

  const int h = listStatsRecentHScroll;
  int compareIdx = -1;
  if (n >= 2 && h >= 0 && h + 1 < n) {
    if (sel == h) {
      compareIdx = h + 1;
    } else if (sel == h + 1) {
      compareIdx = h;
    }
  }

  BookReadingStats secondStats;
  const bool hasSecond = compareIdx >= 0;
  const bool hasSecondStats =
      hasSecond && loadBookStats(recentBooks[static_cast<size_t>(compareIdx)].cachePath.c_str(), secondStats);
  const bool showCompare = hasStats && hasSecondStats && n > 1;

  const int VALUE_FONT = ATKINSON_HYPERLEGIBLE_16_FONT_ID;
  const int LABEL_FONT = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
  const int CMP_FONT = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
  constexpr int kCmpIconSz = 40;
  constexpr int kCmpIconY = 2;
  /** Pixels between the right edge of the compare icon and the “previous” value text. */
  constexpr int kCmpGapAfterIcon = 14;
  constexpr int kCmpValY = 8;
  const int statsX = 30;
  const int col1X = statsX;
  const int col2X = screenW / 2;
  const int rowHeight = 95;
  char buffer[32];
  char secBuf[32];

  auto drawComparedUint = [&](int x, int y, const char* primaryText, uint32_t curVal, uint32_t othVal) {
    renderer.text.render(VALUE_FONT, x, y, primaryText, true, EpdFontFamily::BOLD);
    if (!showCompare) {
      return;
    }
    const int iconX = x + renderer.text.getWidth(VALUE_FONT, primaryText) + 10;
    if (curVal > othVal) {
      renderer.bitmap.icon(Up, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
    } else if (curVal < othVal) {
      renderer.bitmap.icon(Down, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
    }
    snprintf(secBuf, sizeof(secBuf), "%u", othVal);
    renderer.text.render(CMP_FONT, iconX + kCmpIconSz + kCmpGapAfterIcon, y + kCmpValY, secBuf, true,
                      EpdFontFamily::BOLD);
  };

  auto drawComparedTime = [&](int x, int y, uint32_t curMs, uint32_t othMs) {
    const std::string curStr = formatTime(curMs);
    renderer.text.render(VALUE_FONT, x, y, curStr.c_str(), true, EpdFontFamily::BOLD);
    if (!showCompare) {
      return;
    }
    const int iconX = x + renderer.text.getWidth(VALUE_FONT, curStr.c_str()) + 10;
    if (curMs > othMs) {
      renderer.bitmap.icon(Up, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
    } else if (curMs < othMs) {
      renderer.bitmap.icon(Down, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
    }
    const std::string othStr = formatTime(othMs);
    renderer.text.render(CMP_FONT, iconX + kCmpIconSz + kCmpGapAfterIcon, y + kCmpValY, othStr.c_str(), true,
                      EpdFontFamily::BOLD);
  };

  auto drawComparedAvgPage = [&](int x, int y, uint32_t curMs, uint32_t othMs) {
    if (curMs > 0) {
      snprintf(buffer, sizeof(buffer), "%u s", curMs / 1000);
    } else {
      snprintf(buffer, sizeof(buffer), "-");
    }
    renderer.text.render(VALUE_FONT, x, y, buffer, true, EpdFontFamily::BOLD);
    if (!showCompare) {
      return;
    }
    if (othMs > 0) {
      snprintf(secBuf, sizeof(secBuf), "%u s", othMs / 1000);
    } else {
      snprintf(secBuf, sizeof(secBuf), "-");
    }
    const int iconX = x + renderer.text.getWidth(VALUE_FONT, buffer) + 10;
    if (curMs > othMs) {
      renderer.bitmap.icon(Up, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
    } else if (curMs < othMs) {
      renderer.bitmap.icon(Down, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
    }
    renderer.text.render(CMP_FONT, iconX + kCmpIconSz + kCmpGapAfterIcon, y + kCmpValY, secBuf, true,
                      EpdFontFamily::BOLD);
  };

  auto drawComparedProgressPct = [&](int x, int y, float curPct, float othPct) {
    const bool curOk = curPct >= 0.0f;
    if (curOk) {
      snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(curPct + 0.5f));
    } else {
      snprintf(buffer, sizeof(buffer), "-");
    }
    renderer.text.render(VALUE_FONT, x, y, buffer, true, EpdFontFamily::BOLD);
    if (!showCompare) {
      return;
    }
    const bool othOk = othPct >= 0.0f;
    if (othOk) {
      snprintf(secBuf, sizeof(secBuf), "%d%%", static_cast<int>(othPct + 0.5f));
    } else {
      snprintf(secBuf, sizeof(secBuf), "-");
    }
    const int iconX = x + renderer.text.getWidth(VALUE_FONT, buffer) + 10;
    const int curInt = curOk ? static_cast<int>(curPct + 0.5f) : -1;
    const int othInt = othOk ? static_cast<int>(othPct + 0.5f) : -1;
    if (curInt >= 0 && othInt >= 0) {
      if (curInt > othInt) {
        renderer.bitmap.icon(Up, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
      } else if (curInt < othInt) {
        renderer.bitmap.icon(Down, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
      }
    }
    renderer.text.render(CMP_FONT, iconX + kCmpIconSz + kCmpGapAfterIcon, y + kCmpValY, secBuf, true,
                      EpdFontFamily::BOLD);
  };

  const uint32_t curTime = hasStats ? stats.totalReadingTimeMs : 0;
  const uint32_t othTime = hasSecondStats ? secondStats.totalReadingTimeMs : 0;
  drawComparedTime(col1X, gridStartY, curTime, othTime);
  renderer.text.render(LABEL_FONT, col1X, gridStartY + 40, "Reading Time", true);

  const uint32_t curPages = hasStats ? stats.totalPagesRead : 0;
  const uint32_t othPages = hasSecondStats ? secondStats.totalPagesRead : 0;
  snprintf(buffer, sizeof(buffer), "%u", curPages);
  drawComparedUint(col2X, gridStartY, buffer, curPages, othPages);
  renderer.text.render(LABEL_FONT, col2X, gridStartY + 40, "Pages", true);

  const int row2Y = gridStartY + rowHeight;
  const uint32_t curCh = hasStats ? stats.totalChaptersRead : 0;
  const uint32_t othCh = hasSecondStats ? secondStats.totalChaptersRead : 0;
  snprintf(buffer, sizeof(buffer), "%u", curCh);
  drawComparedUint(col1X, row2Y, buffer, curCh, othCh);
  renderer.text.render(LABEL_FONT, col1X, row2Y + 40, "Chapters", true);

  const uint32_t curAvg = hasStats ? stats.avgPageTimeMs : 0;
  const uint32_t othAvg = hasSecondStats ? secondStats.avgPageTimeMs : 0;
  drawComparedAvgPage(col2X, row2Y, curAvg, othAvg);
  renderer.text.render(LABEL_FONT, col2X, row2Y + 40, "Average / Page", true);

  const int row3Y = gridStartY + rowHeight * 2;
  const uint32_t curSess = hasStats ? stats.sessionCount : 0;
  const uint32_t othSess = hasSecondStats ? secondStats.sessionCount : 0;
  snprintf(buffer, sizeof(buffer), "%u", curSess);
  drawComparedUint(col1X, row3Y, buffer, curSess, othSess);
  renderer.text.render(LABEL_FONT, col1X, row3Y + 40, "Session", true);

  const float curProg =
      hasStats ? stats.progressPercent
               : ((curBook.progress >= 0.0f && curBook.progress <= 1.0f) ? curBook.progress * 100.0f : -1.0f);
  const float othProg = hasSecondStats ? secondStats.progressPercent : -1.0f;
  drawComparedProgressPct(col2X, row3Y, curProg, othProg);
  renderer.text.render(LABEL_FONT, col2X, row3Y + 40, "Progress", true);
}

void RecentActivity::drawListStatsStrip(int bandX, int bandY, int bandW, int bandH, int hScroll, int count,
                                        const std::function<std::string(int)>& cacheDirAt,
                                        const std::function<std::string(int)>& titleAt,
                                        const std::function<bool(int)>& selectedAt) {
  if (count <= 0 || bandH < 40) {
    return;
  }
  /** Default list strip: slightly larger than legacy flow side thumbs (189×286). */
  constexpr int kListStatsThumbW = 210;
  constexpr int kListStatsThumbH = 318;
  const int marginInner = 8;
  const int bandRight = bandX + bandW - marginInner;

  int thumbW = kListStatsThumbW;
  int thumbH = kListStatsThumbH;
  if (thumbH > bandH - 16) {
    thumbH = std::max(100, bandH - 16);
    thumbW = std::max(60, thumbH * kListStatsThumbW / kListStatsThumbH);
  }
  const int rowY = bandY + (bandH - thumbH) / 2;

  const int slots = std::min(kRecentStripSlots, count - hScroll);
  if (slots <= 0) {
    return;
  }
  const int totalStripW = thumbW * slots + kRecentThumbGap * (slots - 1);
  int baseX = bandX + marginInner + std::max(0, (bandW - marginInner * 2 - totalStripW) / 2);

  for (int slot = 0; slot < slots; ++slot) {
    const int bi = hScroll + slot;
    if (bi >= count) {
      break;
    }
    const int slotX = baseX + slot * (thumbW + kRecentThumbGap);
    const int visW = std::min(thumbW, bandRight - slotX);
    if (visW < 8) {
      break;
    }

    const bool sel = selectedAt(bi);
    const bool rr = SETTINGS.bitmapRoundedCorners != 0;
    renderer.rectangle.fill(slotX, rowY, thumbW, thumbH, false, rr);

    const std::string cdir = cacheDirAt(bi);
    const std::string ttl = titleAt(bi);
    drawRecentThumbnailAt(slotX, rowY, thumbW, thumbH, cdir, ttl, ATKINSON_HYPERLEGIBLE_12_FONT_ID);
    if (sel) {
      renderer.rectangle.render(slotX - 2, rowY - 2, thumbW + 4, thumbH + 4, true, rr);
      if (!rr && thumbW > 6 && thumbH > 6) {
        renderer.rectangle.render(slotX, rowY, thumbW, thumbH, true, false);
      }
    } else if (!rr) {
      renderer.rectangle.render(slotX, rowY, thumbW, thumbH, true, false);
    }
  }
}

/**
 * Calculates the number of rows that can be displayed on screen at once.
 */
int RecentActivity::getVisibleRows() const {
  if (currentViewMode == ViewMode::Icons) {
    return 3;  // 2×3 icon grid (scroll for more)
  }
  if (currentViewMode == ViewMode::Grid) {
    int screenHeight = renderer.getScreenHeight();
    int availableHeight = screenHeight - TAB_BAR_HEIGHT - 20;
    return (availableHeight > 0) ? availableHeight / GRID_ITEM_HEIGHT : 1;
  }
  if (currentViewMode == ViewMode::List) {
    return LIST_VISIBLE_ITEMS;
  }
  return LIST_VISIBLE_ITEMS;
}

/**
 * Loads recent books from persistent storage.
 */
void RecentActivity::loadRecentBooks(const bool resetScroll) {
  recentBooks.clear();
  recentBooks.reserve(MAX_RECENT_BOOKS);
  const int maxShow =
      std::min(MAX_RECENT_BOOKS, std::max(1, static_cast<int>(SETTINGS.recentVisibleCount)));
  if (resetScroll) {
    scrollOffset = 0;
    scrollOffsetDefault = 0;
  }

  const auto& allBooks = RECENT_BOOKS.getBooks();
  size_t addedCount = 0;

  for (size_t i = 0; i < allBooks.size() && addedCount < maxShow; ++i) {
    const auto& book = allBooks[i];
    if (!SdMan.exists(book.path.c_str())) {
      continue;
    }
    recentBooks.push_back(book);
    addedCount++;
  }
  const std::vector<BookState::Book> favorites = BOOK_STATE.getFavoriteBooks();
  rebuildListStatsFavorites(favorites);
  rebuildSimpleUiFavorites(favorites);
}

void RecentActivity::rebuildSimpleUiFavorites(const std::vector<BookState::Book>& favorites) {
  simpleUiFavorites_.clear();
  int added = 0;
  for (const auto& fb : favorites) {
    if (!SdMan.exists(fb.path.c_str())) {
      continue;
    }
    simpleUiFavorites_.push_back(fb);
    if (++added >= kSimpleUiFavoritesMaxCount) {
      break;
    }
  }
}

void RecentActivity::clampSimpleUiFavoriteScroll(const int maxVisibleFavs) {
  const int recentSlots = recentBooks.empty() ? 0 : 1;
  const int fc = static_cast<int>(simpleUiFavorites_.size());
  if (fc <= maxVisibleFavs) {
    simpleUiFavScroll_ = 0;
    return;
  }
  if (selectorIndex < recentSlots) {
    return;
  }
  const int fi = selectorIndex - recentSlots;
  if (fi < simpleUiFavScroll_) {
    simpleUiFavScroll_ = fi;
  }
  if (fi >= simpleUiFavScroll_ + maxVisibleFavs) {
    simpleUiFavScroll_ = fi - maxVisibleFavs + 1;
  }
  const int maxScroll = std::max(0, fc - maxVisibleFavs);
  simpleUiFavScroll_ = std::max(0, std::min(simpleUiFavScroll_, maxScroll));
}

/**
 * Favorites not already on the recent strip. Recent membership per favorite is O(MAX_RECENT_BOOKS) (constant);
 * total cost is O(|favorites|) SD exists checks — unavoidable without incremental BookState hooks.
 */
void RecentActivity::rebuildListStatsFavorites(const std::vector<BookState::Book>& favorites) {
  listStatsFavoriteOnly_.clear();
  for (const auto& fav : favorites) {
    if (recentBooksContainPath(recentBooks, fav.path)) {
      continue;
    }
    if (!SdMan.exists(fav.path.c_str())) {
      continue;
    }
    listStatsFavoriteOnly_.push_back(fav);
  }
}

/**
 * Initializes the recent activity when entered.
 */
void RecentActivity::onEnter() {
  Activity::onEnter();

  layoutEngine_.reset();
  layoutEngineBoundMode_ = ViewMode::Flow;
  halfRefreshOnLoadApplied_ = false;
  renderer.clearScreen(0xff);
  loadRecentBooks();

  currentViewMode = viewModeForLibrarySetting(SETTINGS.recentLibraryMode);
  if (currentViewMode == ViewMode::SimpleUi) {
    selectorIndex = 0;
    simpleUiFavScroll_ = 0;
  } else if (currentViewMode == ViewMode::List || currentViewMode == ViewMode::Icons) {
    selectorIndex = 0;
    scrollOffset = 0;
  }

  updateRequired = true;
}

/**
 * Cleans up resources when exiting the recent activity.
 */
void RecentActivity::onExit() {
  layoutEngine_.reset();
  layoutEngineBoundMode_ = ViewMode::Flow;
  recentBooks.clear();
  listStatsFavoriteOnly_.clear();
  simpleUiFavorites_.clear();
  renderer.resetTransientReaderState();
  Activity::onExit();
}

/**
 * Renders the complete grid view including all visible books.
 */
void RecentActivity::renderGrid(int startY) {
  renderTabBar(renderer);
  int totalBooks = static_cast<int>(recentBooks.size());
  if (totalBooks == 0) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, startY + 150, "No recent books");
    return;
  }

  int visibleRows = getVisibleRows();
  int startRow = scrollOffset;
  int endRow = std::min(startRow + visibleRows, (totalBooks + GRID_COLS - 1) / GRID_COLS);

  for (int row = startRow; row < endRow; ++row) {
    for (int col = 0; col < GRID_COLS; ++col) {
      int bookIdx = row * GRID_COLS + col;
      if (bookIdx >= totalBooks) break;

      bool isSelected = (selectorIndex == bookIdx);
      renderGridItem(col, row - startRow, startY, recentBooks[bookIdx], isSelected);
    }
  }
}

void RecentActivity::renderIcons(int startY) {
  const int totalBooks = static_cast<int>(recentBooks.size());
  if (totalBooks == 0) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, startY + 150, "No recent books");
    return;
  }

  constexpr int kCols = 2;
  constexpr int kRowsVisible = 3;
  constexpr int kFrameW = 200;
  constexpr int kFrameH = 200;
  constexpr int kGapX = 10;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight() - 30;
  const int availW = std::max(1, screenW - GRID_SPACING * 2);
  const int availH = std::max(1, screenH - startY - GRID_SPACING * 2);

  const int kGapY =
      (kRowsVisible > 1) ? std::max(8, (availH - kRowsVisible * kFrameH) / (kRowsVisible - 1)) : 0;
  const int blockH = kRowsVisible * kFrameH + (kRowsVisible - 1) * kGapY;
  const int blockTop = startY + GRID_SPACING + std::max(0, (availH - blockH) / 2);

  const int twoW = kCols * kFrameW + (kCols - 1) * kGapX;
  const int row0X = GRID_SPACING + std::max(0, (availW - twoW) / 2);
  const bool rr = SETTINGS.bitmapRoundedCorners != 0;

  const int startRow = scrollOffset;
  const int endRow = std::min(startRow + kRowsVisible, (totalBooks + kCols - 1) / kCols);

  for (int row = startRow; row < endRow; ++row) {
    for (int col = 0; col < kCols; ++col) {
      const int bookIdx = row * kCols + col;
      if (bookIdx >= totalBooks) {
        break;
      }
      const int visualRow = row - startRow;
      const int boxX = row0X + col * (kFrameW + kGapX);
      const int boxY = blockTop + visualRow * (kFrameH + kGapY);
      const bool selected = (selectorIndex == bookIdx);

      if (rr) {
        renderer.rectangle.fill(boxX, boxY, kFrameW, kFrameH, false, rr);
      }
      if (selected) {
        renderer.rectangle.render(boxX - 2, boxY - 2, kFrameW + 4, kFrameH + 4, true, rr);
      } else if (!rr) {
        renderer.rectangle.render(boxX, boxY, kFrameW, kFrameH, true, false);
      }

      const int innerX = boxX + 4;
      const int innerY = boxY + 4;
      const int innerW = std::max(8, kFrameW - 8);
      const int innerH = std::max(8, kFrameH - 8);
      const RecentBook& b = recentBooks[static_cast<size_t>(bookIdx)];
      drawRecentThumbnailAt(innerX, innerY, innerW, innerH, b.cachePath, bookDisplayTitle(b),
                            ATKINSON_HYPERLEGIBLE_10_FONT_ID, false);
    }
  }
}

/**
 * Renders a single grid item including cover image or placeholder.
 */
void RecentActivity::renderGridItem(int gridX, int gridY, int startY, const RecentBook& book, bool selected) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight() - 20;

  int availableWidth = screenW - (GRID_COLS + 1) * GRID_SPACING;
  int containerWidth = availableWidth / GRID_COLS;

  int visibleRows = getVisibleRows();
  int availableHeight = screenH - startY - (GRID_SPACING * 2);
  int containerHeight = (availableHeight / visibleRows) - GRID_SPACING;

  int itemX = GRID_SPACING + gridX * (containerWidth + GRID_SPACING);
  int itemY = startY + GRID_SPACING + gridY * (containerHeight + GRID_SPACING);

  if (selected) {
    for (int y = itemY + 10; y < itemY + GRID_ITEM_HEIGHT + 63; y += 2) {
      for (int x = itemX - 12; x < itemX + containerWidth + 12; x += 2) {
        renderer.drawPixel(x, y, true);
      }
    }
  }

  int coverAreaX = itemX;
  int coverAreaY = itemY;

  const int drawX = coverAreaX;
  const int drawY = coverAreaY + GRID_SPACING;
  const int drawW = containerWidth;
  const int drawH = static_cast<int>(containerHeight);
  drawRecentThumbnailAt(drawX, drawY, drawW, drawH, book.cachePath, bookDisplayTitle(book),
                        ATKINSON_HYPERLEGIBLE_10_FONT_ID, selected);

  if (book.progress >= 0.0f && book.progress <= 1.0f) {
    int barX = coverAreaX + 15;
    int barY = coverAreaY + containerHeight;
    int barW = containerWidth - 30;
    int barH = 10;

    renderer.rectangle.fill(barX, barY, barW, barH, false);
    renderer.rectangle.render(barX, barY, barW, barH, true);

    if (book.progress > 0.0f) {
      int fillW = static_cast<int>(barW * book.progress + 0.5f);
      renderer.rectangle.fill(barX, barY, fillW, barH);

      char pText[8];
      int percent = static_cast<int>(book.progress * 100.0f + 0.5f);
      snprintf(pText, sizeof(pText), "%d%%", percent);
      int pW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, pText);
      renderer.rectangle.fill(barX + barW - pW - 5, barY - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID) - 10,
                        pW + 5, 30, false, true);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, barX + barW - pW,
                        barY - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID) - 6, pText);
    }
  }
}

void RecentActivity::renderList(int startY) {
  const int totalBooks = static_cast<int>(recentBooks.size());
  if (totalBooks == 0) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, startY + 150, "No recent books");
    return;
  }

  constexpr int kHintReserve = 54;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int contentBottom = screenH - kHintReserve;
  const int contentH = std::max(1, contentBottom - startY);
  const int rowH = std::max(56, contentH / LIST_VISIBLE_ITEMS);
  constexpr int padX = 30;
  const int thumbH = std::max(48, rowH - 10);
  const int thumbW = std::min(88, thumbH * RecentActivity::COVER_WIDTH / RecentActivity::COVER_HEIGHT);
  const bool thumbRound = SETTINGS.bitmapRoundedCorners != 0;

  const int visibleCount = std::min(LIST_VISIBLE_ITEMS, totalBooks - scrollOffset);
  for (int slot = 0; slot < visibleCount; ++slot) {
    const int bi = scrollOffset + slot;
    const int y = startY + slot * rowH;
    const RecentBook& book = recentBooks[static_cast<size_t>(bi)];
    const bool selected = (selectorIndex == bi);

    if (selected) {
      renderer.rectangle.render(padX / 2, y + 1, screenW - padX, rowH, true, false);
    }

    const int ty = y + (rowH - thumbH) / 2;
    const int tx = padX;
    const std::string cacheDir =
        book.cachePath.empty() ? epubCachePathForBookPath(book.path) : book.cachePath;
    if (thumbRound) {
      renderer.rectangle.fill(tx, ty, thumbW, thumbH, false, true);
    }
    drawRecentThumbnailAt(tx, ty, thumbW, thumbH, cacheDir, bookDisplayTitle(book), ATKINSON_HYPERLEGIBLE_10_FONT_ID,
                            false);

    const int textX = tx + thumbW + 14;
    const int textRight = screenW - padX;
    const int textW = std::max(40, textRight - textX);

    const int fontTitle = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
    const int fontAuthor = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
    const int lhT = renderer.text.getLineHeight(fontTitle);
    const int lhA = renderer.text.getLineHeight(fontAuthor);
    const int tyT = y + 20;
    const std::string dispTitle = bookDisplayTitle(book);
    const std::string titleLine =
        renderer.text.truncate(fontTitle, dispTitle.c_str(), textW, EpdFontFamily::REGULAR);
    renderer.text.render(fontTitle, textX, tyT, titleLine.c_str(), true, EpdFontFamily::REGULAR);
    int lastTextBottom = tyT + lhT;
    int tyA = tyT + lhT + 4;
    if (!book.author.empty()) {
      const std::string auth = renderer.text.truncate(fontAuthor, book.author.c_str(), textW);
      renderer.text.render(fontAuthor, textX, tyA, auth.c_str());
      lastTextBottom = tyA + lhA;
    }

    float prog = book.progress;
    if (prog < 0.f || prog > 1.f) {
      prog = 0.f;
    }
    char pctBuf[12];
    snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", static_cast<double>(prog * 100.f));
    const int fontPct = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
    const int pctW = renderer.text.getWidth(fontPct, pctBuf);
    constexpr int barH = 8;
    int barY = lastTextBottom + 20;
    barY = std::max(barY, tyT + lhT + 4);
    barY = std::min(barY, y + rowH - barH - 4);
    if (barY < tyT + lhT) {
      barY = y + rowH - barH - 4;
    }
    const int barX = textX;
    const int barW = std::max(24, textRight - pctW - 10 - barX);

    renderer.rectangle.fill(barX, barY, barW, barH, false);
    renderer.rectangle.render(barX, barY, barW, barH, true);
    const int fillW = static_cast<int>(static_cast<float>(barW) * prog + 0.5f);
    if (fillW > 0) {
      renderer.rectangle.fill(barX, barY, fillW, barH, true);
    }
    renderer.text.render(fontPct, barX + barW + 6, barY - 1, pctBuf, false);

    const bool isLastVisibleRow = (slot == visibleCount - 1);
    if (!isLastVisibleRow) {
      const int lineY = y + rowH - 1;
      const int x0 = padX / 2;
      const int x1 = screenW - padX / 2;
      for (int px = x0; px < x1; px += 3) {
        renderer.drawPixel(px, lineY, true);
      }
    }
  }
}

void RecentActivity::syncLayoutEngineForViewMode() {
  if (!layoutEngine_ || layoutEngineBoundMode_ != currentViewMode) {
    layoutEngine_ = makeLayoutEngine(currentViewMode);
    layoutEngineBoundMode_ = currentViewMode;
  }
}

std::unique_ptr<RecentActivity::LayoutEngine> RecentActivity::makeLayoutEngine(ViewMode mode) {
  switch (mode) {
    case ViewMode::Default:
      return std::unique_ptr<LayoutEngine>(new DefaultViewLayout());
    case ViewMode::Grid:
      return std::unique_ptr<LayoutEngine>(new GridViewLayout());
    case ViewMode::Icons:
      return std::unique_ptr<LayoutEngine>(new IconsViewLayout());
    case ViewMode::SimpleUi:
      return std::unique_ptr<LayoutEngine>(new SimpleUiViewLayout());
    case ViewMode::List:
      return std::unique_ptr<LayoutEngine>(new ListViewLayout());
    case ViewMode::Flow:
    default:
      return std::unique_ptr<LayoutEngine>(new FlowViewLayout());
  }
}

void RecentActivity::DefaultViewLayout::paint(RecentActivity& self) {
  self.renderDefault();
}

void RecentActivity::GridViewLayout::paint(RecentActivity& self) {
  self.renderGrid(self.recentGridPaintStartY());
}

void RecentActivity::IconsViewLayout::paint(RecentActivity& self) {
  self.renderIcons(self.recentIconsPaintStartY());
}

void RecentActivity::SimpleUiViewLayout::paint(RecentActivity& self) {
  self.renderSimpleUi();
}

void RecentActivity::ListViewLayout::paint(RecentActivity& self) {
  self.renderList(self.recentListPaintStartY());
}

void RecentActivity::FlowViewLayout::paint(RecentActivity& self) {
  self.renderFlow();
}

void RecentActivity::pumpDisplayFromLoop() {
  if (!updateRequired) {
    return;
  }
  renderer.clearScreen();
  renderTabBar(renderer);

  syncLayoutEngineForViewMode();
  layoutEngine_->paint(*this);

  const auto labels = mappedInput.mapLabels("Remove", "Open", "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
  if (!halfRefreshOnLoadApplied_) {
    halfRefreshOnLoadApplied_ = true;
    SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Recent);
  }
  updateRequired = false;
}

/**
 * Formats milliseconds into a human-readable time string.
 * Output format: "X.X h" for hours (with one decimal), "X m" for minutes.
 */
std::string RecentActivity::formatTime(uint32_t milliseconds) const {
  char buffer[32];
  float hours = milliseconds / (1000.0f * 3600.0f);

  if (hours >= 1.0f) {
    snprintf(buffer, sizeof(buffer), "%.1f h", hours);
  } else {
    uint32_t minutes = milliseconds / (1000 * 60);
    snprintf(buffer, sizeof(buffer), "%u m", minutes);
  }
  return std::string(buffer);
}

/**
 * List (default) view: two thumbnails in the top band; stats below use the same 2×2 grid as Flow.
 */
void RecentActivity::renderDefault() {
  const int recentCount = static_cast<int>(recentBooks.size());
  const int favCount = static_cast<int>(listStatsFavoriteOnly_.size());
  if (recentCount == 0 && favCount == 0) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, renderer.getScreenHeight() / 2, "No recent books");
    return;
  }

  if (recentCount > 0) {
    clampRecentStripHScroll(selectorIndex, recentCount, listStatsRecentHScroll);
  }

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int startY = TAB_BAR_HEIGHT - 6;
  const int bodyTop = startY + 8;
  const int bodyBottom = screenH - 28;
  constexpr int kCarouselH = 340;
  const int carouselH = std::min(kCarouselH, std::max(120, bodyBottom - bodyTop));

  drawFlowCarouselBackdropInRect(renderer, 0, bodyTop, screenW, carouselH);
  if (recentCount > 0 && carouselH > 40) {
    drawListStatsStrip(
        0, bodyTop, screenW, carouselH, listStatsRecentHScroll, recentCount,
        [&](int bi) -> std::string {
          const RecentBook& b = recentBooks[static_cast<size_t>(bi)];
          return b.cachePath.empty() ? epubCachePathForBookPath(b.path) : b.cachePath;
        },
        [&](int bi) -> std::string { return bookDisplayTitle(recentBooks[static_cast<size_t>(bi)]); },
        [&](int bi) { return selectorIndex == bi; });
  } else {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID,
                              bodyTop + std::max(8, carouselH / 2 - 8), "No recent books");
  }

  const int belowY = bodyTop + carouselH;
  const int kSepFontCur = ATKINSON_HYPERLEGIBLE_18_FONT_ID;
  const int kSepFontSlash = ATKINSON_HYPERLEGIBLE_18_FONT_ID;
  const int kSepFontPrev = ATKINSON_HYPERLEGIBLE_14_FONT_ID;
  const int lhCur = renderer.text.getLineHeight(kSepFontCur);
  const int lhPrev = renderer.text.getLineHeight(kSepFontPrev);
  constexpr int kSepToGridGap = 22;
  if (belowY < bodyBottom) {
    renderer.rectangle.fill(0, belowY, screenW, bodyBottom - belowY, false);
    renderer.line.render(0, belowY, screenW, belowY, true);
    if (recentCount > 0) {
      const int sepTextY = belowY + 8;
      int tx = 30;
      renderer.text.render(kSepFontCur, tx, sepTextY, "Current", true, EpdFontFamily::BOLD);
      tx += renderer.text.getWidth(kSepFontCur, "Current", EpdFontFamily::BOLD);
      renderer.text.render(kSepFontSlash, tx, sepTextY, " / ", true, EpdFontFamily::REGULAR);
      tx += renderer.text.getWidth(kSepFontSlash, " / ", EpdFontFamily::REGULAR);
      const int prevY = sepTextY + (lhCur - lhPrev) / 2;
      renderer.text.render(kSepFontPrev, tx, prevY, "Previous", true, EpdFontFamily::BOLD);

      const int gridY = sepTextY + std::max(lhCur, lhPrev) + kSepToGridGap;
      if (gridY < bodyBottom) {
        renderDefaultStatsGrid(gridY, screenW);
      }
    }
  }
}

/**
 * Main loop for handling user input and updating state.
 */
void RecentActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    updateRequired = true;
    return;
  }

  if (updateRequired) {
    pumpDisplayFromLoop();
  }

  const int totalBooks = static_cast<int>(recentBooks.size());
  const bool isDefaultView = (currentViewMode == ViewMode::Default);
  const bool isSimpleUi = (currentViewMode == ViewMode::SimpleUi);
  const bool isListView = (currentViewMode == ViewMode::List);

  bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
  bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);
  bool leftPressed = mappedInput.wasPressed(MappedInputManager::Button::Left);
  bool rightPressed = mappedInput.wasPressed(MappedInputManager::Button::Right);
  bool confirmPressed = mappedInput.wasReleased(MappedInputManager::Button::Confirm);

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() >= GO_HOME_MS) {
      return;
    }
    if (tabSelectorIndex == 0) {
      if ((isDefaultView && totalBooks > 0 && selectorIndex >= 0 && selectorIndex < totalBooks) ||
          (isSimpleUi && totalBooks > 0 && selectorIndex == 0)) {
        RECENT_BOOKS.removeBook(recentBooks[selectorIndex].path);
        loadRecentBooks(false);
        const int n = static_cast<int>(recentBooks.size());
        if (n == 0) {
          scrollOffset = 0;
          scrollOffsetDefault = 0;
          selectorIndex = 0;
        } else {
          if (selectorIndex >= n) {
            selectorIndex = n - 1;
          }
          const int maxScroll = std::max(0, n - kRecentStripSlots);
          scrollOffsetDefault = std::max(0, std::min(scrollOffsetDefault, maxScroll));
        }
        simpleUiFavScroll_ = 0;
        updateRequired = true;
      } else if (!isDefaultView && !isSimpleUi && totalBooks > 0 && selectorIndex >= 0 && selectorIndex < totalBooks) {
        RECENT_BOOKS.removeBook(recentBooks[selectorIndex].path);
        loadRecentBooks(false);
        const int n = static_cast<int>(recentBooks.size());
        if (n == 0) {
          selectorIndex = 0;
          scrollOffset = 0;
          scrollOffsetDefault = 0;
        } else {
          if (selectorIndex >= n) {
            selectorIndex = n - 1;
          }
          if (currentViewMode == ViewMode::Grid || currentViewMode == ViewMode::Icons) {
            const int visibleRows = getVisibleRows();
            const int totalRows = (n + GRID_COLS - 1) / GRID_COLS;
            const int maxScroll = std::max(0, totalRows - visibleRows);
            scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
          } else if (currentViewMode == ViewMode::List) {
            const int visibleItems = LIST_VISIBLE_ITEMS;
            const int maxScroll = std::max(0, n - visibleItems);
            scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
          }
        }
        updateRequired = true;
      }
    }
    return;
  }

  if (leftPressed) {
    tabSelectorIndex = 4;
    navigateToSelectedMenu();
    return;
  }

  if (rightPressed) {
    tabSelectorIndex = 1;
    navigateToSelectedMenu();
    return;
  }

  if (tabSelectorIndex != 0) {
    return;
  }

  if (!isDefaultView && !isSimpleUi && totalBooks == 0) {
    return;
  }
  if (isDefaultView && totalBooks == 0) {
    return;
  }

  {
    const ViewMode expectedMode = viewModeForLibrarySetting(SETTINGS.recentLibraryMode);
    if (expectedMode != currentViewMode) {
      currentViewMode = expectedMode;
      scrollOffset = 0;
      selectorIndex = 0;
      simpleUiFavScroll_ = 0;
      listStatsRecentHScroll = 0;
      updateRequired = true;
      return;
    }
  }

  bool selectorChanged = false;

  if (isSimpleUi) {
    const int recentSlots = totalBooks > 0 ? 1 : 0;
    const int favCount = static_cast<int>(simpleUiFavorites_.size());
    const int totalSel = recentSlots + favCount;
    if (totalSel == 0) {
      return;
    }
    if (selectorIndex >= totalSel) {
      selectorIndex = totalSel - 1;
    }
    if (selectorIndex < 0) {
      selectorIndex = 0;
    }

    const SimpleUiMetrics su = computeSimpleUiMetrics(renderer);
    const int maxVis = su.maxVis;

    if (upPressed && selectorIndex > 0) {
      selectorIndex--;
      selectorChanged = true;
    }
    if (downPressed && selectorIndex < totalSel - 1) {
      selectorIndex++;
      selectorChanged = true;
    }
    if (selectorChanged) {
      clampSimpleUiFavoriteScroll(maxVis);
      updateRequired = true;
    }
    if (confirmPressed) {
      if (recentSlots == 1 && selectorIndex == 0) {
        bookSelected = true;
        onSelectBook(recentBooks[0].path);
        return;
      }
      const int fi = selectorIndex - recentSlots;
      if (fi >= 0 && fi < favCount) {
        bookSelected = true;
        onSelectBook(simpleUiFavorites_[static_cast<size_t>(fi)].path);
        return;
      }
    }
    return;
  }

  if (isDefaultView) {
    if (downPressed && selectorIndex < totalBooks - 1) {
      selectorIndex++;
      selectorChanged = true;
    }
    if (upPressed && selectorIndex > 0) {
      selectorIndex--;
      selectorChanged = true;
    }

    if (selectorChanged) {
      clampRecentStripHScroll(selectorIndex, totalBooks, listStatsRecentHScroll);
      updateRequired = true;
    }

    if (confirmPressed && selectorIndex >= 0 && selectorIndex < totalBooks) {
      bookSelected = true;
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
  } else if (isListView) {
    if (downPressed && selectorIndex < totalBooks - 1) {
      selectorIndex++;
      selectorChanged = true;
    }
    if (upPressed && selectorIndex > 0) {
      selectorIndex--;
      selectorChanged = true;
    }

    if (selectorChanged) {
      const int visibleItems = LIST_VISIBLE_ITEMS;
      if (selectorIndex < scrollOffset) {
        scrollOffset = selectorIndex;
      } else if (selectorIndex >= scrollOffset + visibleItems) {
        scrollOffset = selectorIndex - visibleItems + 1;
      }
      const int maxOffset = std::max(0, totalBooks - visibleItems);
      scrollOffset = std::max(0, std::min(scrollOffset, maxOffset));
      updateRequired = true;
    }

    if (confirmPressed && selectorIndex >= 0 && selectorIndex < totalBooks) {
      bookSelected = true;
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
  } else {
    if (downPressed && selectorIndex < totalBooks - 1) {
      selectorIndex++;
      selectorChanged = true;
    } else if (upPressed && selectorIndex > 0) {
      selectorIndex--;
      selectorChanged = true;
    }

    if (selectorChanged) {
      int currentRow = selectorIndex / GRID_COLS;
      int visibleRows = getVisibleRows();
      if (currentRow < scrollOffset) {
        scrollOffset = currentRow;
      } else if (currentRow >= scrollOffset + visibleRows) {
        scrollOffset = currentRow - visibleRows + 1;
      }
      int totalRows = (totalBooks + GRID_COLS - 1) / GRID_COLS;
      int maxOffset = std::max(0, totalRows - visibleRows);
      scrollOffset = std::max(0, std::min(scrollOffset, maxOffset));
      updateRequired = true;
    }

    if (confirmPressed && selectorIndex >= 0 && selectorIndex < totalBooks) {
      bookSelected = true;
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
  }
}
void RecentActivity::renderFlow() {
  if (recentBooks.empty()) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, renderer.getScreenHeight() / 2, "No recent books");
    return;
  }

  const int screenW = renderer.getScreenWidth();
  const int startY = TAB_BAR_HEIGHT + 5;

  int currentIndex = selectorIndex;
  int totalBooks = (int)recentBooks.size();

  int carouselW = screenW;
  int carouselH = 340;
  int carouselX = 0;
  int carouselY = startY;

  drawFlowCarouselBackdrop(renderer, carouselX, carouselY, carouselW, carouselH);

  const bool rr = SETTINGS.bitmapRoundedCorners != 0;

  int centerW = 210;
  int centerH = 318;
  int centerX = carouselX + (carouselW - centerW) / 2;
  int centerY = carouselY + (carouselH - centerH) / 2 + 4;

  float scale = 0.9;
  int sideW = (int)(centerW * scale);
  int sideH = (int)(centerH * scale);
  int leftX = centerX - sideW - 20;
  int rightX = centerX + centerW + 20;
  int sideY = centerY + (centerH - sideH) / 2;

  
  if (currentIndex > 0) {
    const RecentBook& leftBook = recentBooks[currentIndex - 1];
    if (rr) {
      renderer.rectangle.fill(leftX, sideY, sideW, sideH, false, true);
    }
    drawRecentThumbnailAt(leftX, sideY, sideW, sideH, leftBook.cachePath, bookDisplayTitle(leftBook),
                          ATKINSON_HYPERLEGIBLE_10_FONT_ID, true);
  }

  if (currentIndex + 1 < totalBooks) {
    const RecentBook& rightBook = recentBooks[currentIndex + 1];
    if (rr) {
      renderer.rectangle.fill(rightX, sideY, sideW, sideH, false, true);
    }
    drawRecentThumbnailAt(rightX, sideY, sideW, sideH, rightBook.cachePath, bookDisplayTitle(rightBook),
                          ATKINSON_HYPERLEGIBLE_10_FONT_ID, true);
  }

  const RecentBook& currentBook = recentBooks[currentIndex];
  renderer.rectangle.fill(centerX, centerY, centerW, centerH, false, rr);
  drawRecentThumbnailAt(centerX, centerY, centerW, centerH, currentBook.cachePath, bookDisplayTitle(currentBook),
                        ATKINSON_HYPERLEGIBLE_14_FONT_ID, true);

  BookReadingStats stats;
  bool hasStats = loadBookStats(currentBook.cachePath.c_str(), stats);

  const int VALUE_FONT = ATKINSON_HYPERLEGIBLE_16_FONT_ID;
  const int LABEL_FONT = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  int statsX = 30;
  int statsY = carouselY + carouselH + 25;
  renderer.line.render(0, carouselY + carouselH + 10,  screenW, carouselY + carouselH + 10, true);
  std::string title;
  if (!currentBook.title.empty()) {
    title = currentBook.title;
  } else {
    title = formatTitle(getBaseFilename(currentBook.path));
  }
  std::string truncatedTitle =
      renderer.text.truncate(ATKINSON_HYPERLEGIBLE_18_FONT_ID, title.c_str(), screenW - 60, EpdFontFamily::BOLD);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_18_FONT_ID, statsX, statsY, truncatedTitle.c_str(), true,
                    EpdFontFamily::BOLD);

  int authorY = statsY + renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_18_FONT_ID) - 5;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, statsX, authorY, currentBook.author.c_str());

  float progress = hasStats ? stats.progressPercent : (currentBook.progress * 100.0f);
  if (progress >= 0) {
    int barY = authorY + renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID) + 20;
    int barW = (screenW - 60) * 0.5;
    int barH = 6;

    renderer.rectangle.fill(statsX, barY, barW, barH, false);
    renderer.rectangle.render(statsX, barY, barW, barH, true);
    if (progress > 0) {
      int fillW = (int)(barW * (progress / 100.0f));
      renderer.rectangle.fill(statsX, barY, fillW, barH);
    }

    char percentText[8];
    int percent = (int)(progress + 0.5f);
    snprintf(percentText, sizeof(percentText), "%d%%", percent);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, statsX + barW + 12, barY - 13, percentText);
  }

  if (hasStats) {
    char buffer[32];

    int gridStartY = authorY + 100;
    int col1X = statsX;
    int col2X = (screenW) / 2;
    int rowHeight = 95;

    std::string timeStr = formatTime(stats.totalReadingTimeMs);
    renderer.text.render(VALUE_FONT, col1X, gridStartY, timeStr.c_str(), true, EpdFontFamily::BOLD);
    renderer.text.render(LABEL_FONT, col1X, gridStartY + 40, "Reading Time", true);

    snprintf(buffer, sizeof(buffer), "%u", stats.totalPagesRead);
    renderer.text.render(VALUE_FONT, col2X, gridStartY, buffer, true, EpdFontFamily::BOLD);
    renderer.text.render(LABEL_FONT, col2X, gridStartY + 40, "Pages", true);

    int row2Y = gridStartY + rowHeight;

    snprintf(buffer, sizeof(buffer), "%u", stats.totalChaptersRead);
    renderer.text.render(VALUE_FONT, col1X, row2Y, buffer, true, EpdFontFamily::BOLD);
    renderer.text.render(LABEL_FONT, col1X, row2Y + 40, "Chapters", true);

    uint32_t avgPageTime = stats.avgPageTimeMs;
    if (avgPageTime > 0) {
      snprintf(buffer, sizeof(buffer), "%u s", avgPageTime / 1000);
    } else {
      snprintf(buffer, sizeof(buffer), "-");
    }
    renderer.text.render(VALUE_FONT, col2X, row2Y, buffer, true, EpdFontFamily::BOLD);
    renderer.text.render(LABEL_FONT, col2X, row2Y + 40, "Average / Page", true);
  }
}

void RecentActivity::renderSimpleUi() {
  const int screenW = renderer.getScreenWidth();
  const SimpleUiMetrics m = computeSimpleUiMetrics(renderer);

  drawFlowCarouselBackdropInRect(renderer, 0, m.bodyTop, screenW, m.topBandH);

  const int recentSlots = recentBooks.empty() ? 0 : 1;
  if (recentSlots != 0) {
    const RecentBook& b = recentBooks[0];
    const int rx = m.marginL;
    const int ry = m.bodyTop + (m.topBandH - m.thumbH) / 2;
    const bool sel = (selectorIndex == 0);
    const bool rr = SETTINGS.bitmapRoundedCorners != 0;
    renderer.rectangle.fill(rx, ry, m.thumbW, m.thumbH, false, rr);
    const std::string cdir = b.cachePath.empty() ? epubCachePathForBookPath(b.path) : b.cachePath;
    drawRecentThumbnailAt(rx, ry, m.thumbW, m.thumbH, cdir, bookDisplayTitle(b), ATKINSON_HYPERLEGIBLE_12_FONT_ID, true);
    if (sel) {
      renderer.rectangle.render(rx - 2, ry - 2, m.thumbW + 4, m.thumbH + 4, true, rr);
    } else if (!rr) {
      renderer.rectangle.render(rx, ry, m.thumbW, m.thumbH, true, false);
    }

    const int titleFont = ATKINSON_HYPERLEGIBLE_16_FONT_ID;
    const int authorFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
    std::string titleStr = b.title.empty() ? formatTitle(getBaseFilename(b.path)) : b.title;
    const int textX = rx + m.thumbW + 18;
    const int maxTextW = std::max(40, screenW - textX - m.marginL);
    const std::string titleDraw =
        renderer.text.truncate(titleFont, titleStr.c_str(), maxTextW, EpdFontFamily::BOLD);
    const int lhTitle = renderer.text.getLineHeight(titleFont);
    const int lhAuthor = renderer.text.getLineHeight(authorFont);
    const int authorGap = 10;
    constexpr int kSimpleProgressBarH = 10;
    constexpr int kSimpleProgressBarGap = 10;
    const bool showProg = b.progress >= 0.0f && b.progress <= 1.0f;
    const int blockH =
        lhTitle + authorGap + lhAuthor + (showProg ? (kSimpleProgressBarGap + kSimpleProgressBarH) : 0);
    const int textY = m.bodyTop + (m.topBandH - blockH) / 2;
    renderer.text.render(titleFont, textX, textY, titleDraw.c_str(), true, EpdFontFamily::BOLD);
    const std::string auth = b.author.empty() ? std::string() : b.author;
    const std::string authDraw = renderer.text.truncate(authorFont, auth.c_str(), maxTextW);
    renderer.text.render(authorFont, textX, textY + lhTitle + authorGap, authDraw.c_str(), true);
    if (showProg) {
      const int barY = textY + lhTitle + authorGap + lhAuthor + kSimpleProgressBarGap;
      const int barW = maxTextW;
      renderer.rectangle.fill(textX, barY, barW, kSimpleProgressBarH, false);
      renderer.rectangle.render(textX, barY, barW, kSimpleProgressBarH, true);
      if (b.progress > 0.0f) {
        const int fillW = static_cast<int>(static_cast<float>(barW) * b.progress + 0.5f);
        renderer.rectangle.fill(textX, barY, fillW, kSimpleProgressBarH);
      }
    }
  } else {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, m.bodyTop + std::max(20, m.topBandH / 2 - 16),
                              "No recent books", true);
  }

  if (m.favTop < m.bodyBottom) {
    renderer.rectangle.fill(0, m.favTop, screenW, m.bodyBottom - m.favTop, false);
    renderer.line.render(0, m.favTop, screenW, m.favTop, true);
    const int favHdrFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
    renderer.text.render(favHdrFont, m.marginL, m.favTop + kFavHeaderPadTop, "Favorites", true,
                        EpdFontFamily::BOLD);
    const int hdrSepY = m.favListTop - 1;
    if (hdrSepY > m.favTop) {
      renderer.line.render(0, hdrSepY, screenW, hdrSepY, true);
    }
  }

  const int favFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
  const int lh = renderer.text.getLineHeight(favFont);
  constexpr int kPadY = 20;
  clampSimpleUiFavoriteScroll(m.maxVis);

  const int fc = static_cast<int>(simpleUiFavorites_.size());

  if (fc == 0) {
    const int subFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
    const char* line1 = "No favorites yet.";
    const char* line2 = "Star a book in the Library to add it here.";
    const int w1 = renderer.text.getWidth(favFont, line1);
    const int w2 = renderer.text.getWidth(subFont, line2);
    const int lh2 = renderer.text.getLineHeight(subFont);
    const int block = lh + 12 + lh2;
    const int paneH = m.bodyBottom - m.favListTop;
    const int y0 = m.favListTop + std::max(4, (paneH - block) / 2);
    renderer.text.render(favFont, (screenW - w1) / 2, y0, line1, true);
    renderer.text.render(subFont, (screenW - w2) / 2, y0 + lh + 12, line2, true);
    return;
  }

  int rowY = m.favListTop;
  const int endVi = std::min(fc, simpleUiFavScroll_ + m.maxVis);
  const int starX = m.marginL;
  const int titleX = starX + 34;
  for (int i = simpleUiFavScroll_; i < endVi; ++i) {
    const auto& fb = simpleUiFavorites_[static_cast<size_t>(i)];
    const int rowSelIndex = recentSlots + i;
    const bool rowSel = (selectorIndex == rowSelIndex);
    if (rowSel) {
      renderer.rectangle.fill(0, rowY, screenW, m.rowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    const int textY = rowY + kPadY;
    const int maxTitleW = std::max(40, screenW - titleX - m.marginL);
    std::string disp = fb.title.empty() ? formatTitle(getBaseFilename(fb.path)) : fb.title;
    const std::string trunc = renderer.text.truncate(favFont, disp.c_str(), maxTitleW);
    renderer.bitmap.icon(Star, starX, textY + 2, 24, 24, BitmapRender::Orientation::None, rowSel);
    renderer.text.render(favFont, titleX , textY, trunc.c_str(), !rowSel);
    rowY += m.rowH;
    renderer.line.render(0, rowY, screenW, rowY, true);
  }
}