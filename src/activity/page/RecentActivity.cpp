/**
 * @file RecentActivity.cpp
 * @brief Definitions for RecentActivity.
 */

#include "RecentActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>
#include <unordered_set>
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

/** No-cover: stats-style double frame + title one word per line, each line centered (see StatisticActivity::renderCover). */
static void drawRecentNoCoverPlaceholder(GfxRenderer& renderer, int x, int y, int w, int h, const std::string& title,
                                          int fontId) {
  if (w <= 1 || h <= 1) {
    return;
  }
  renderer.fillRect(x, y, w, h, false);
  renderer.drawRect(x, y, w, h, true, false);
  if (w > 6 && h > 6) {
    renderer.drawRect(x + 2, y + 2, w - 4, h - 4, true, false);
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

  const int lh = renderer.getLineHeight(fontId);
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
    if (renderer.getTextWidth(fontId, wshow.c_str()) > maxWordW) {
      wshow = renderer.truncatedText(fontId, wshow.c_str(), maxWordW, EpdFontFamily::REGULAR);
    }
    const int tw = renderer.getTextWidth(fontId, wshow.c_str());
    renderer.drawText(fontId, x + (w - tw) / 2, lineY, wshow.c_str(), true, EpdFontFamily::REGULAR);
    lineY += lh;
  }
}

static std::string epubCachePathForBookPath(const std::string& bookPath) {
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(bookPath));
}

/** Same light “gray” as `renderFlow` carousel: sparse ink checker (not `FillTone::Gray`). */
static void drawFlowCarouselBackdrop(GfxRenderer& renderer, int rx, int ry, int rw, int rh) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  for (int y = ry - 5; y < ry + rh + 10; y += 2) {
    if (y < 0 || y >= screenH) {
      continue;
    }
    for (int x = rx - 5; x < rx + rw + 10; x += 2) {
      if (x >= 0 && x < screenW) {
        renderer.drawPixel(x, y, true);
      }
    }
  }
}

/** Flow-style dither strictly inside the rectangle (does not bleed into the white bottom pane). */
static void drawFlowCarouselBackdropInRect(GfxRenderer& renderer, int rx, int ry, int rw, int rh) {
  if (rw <= 0 || rh <= 0) {
    return;
  }
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int x1 = std::max(0, rx);
  const int y1 = std::max(0, ry);
  const int x2 = std::min(screenW, rx + rw);
  const int y2 = std::min(screenH, ry + rh);
  for (int y = y1; y < y2; y += 2) {
    for (int x = x1; x < x2; x += 2) {
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
  const int lh = renderer.getLineHeight(favFont);
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
                                           const std::string& placeholderTitle, int placeholderFontId) {
  if (w < 8 || h < 8) {
    return;
  }
  if (cacheDir.empty()) {
    drawRecentNoCoverPlaceholder(renderer, x, y, w, h, placeholderTitle, placeholderFontId);
    return;
  }
  char path[192];
  snprintf(path, sizeof(path), "%s/thumb.bmp", cacheDir.c_str());
  FsFile file;
  if (!SdMan.openFileForRead("RECENT", path, file)) {
    drawRecentNoCoverPlaceholder(renderer, x, y, w, h, placeholderTitle, placeholderFontId);
    return;
  }
  Bitmap bitmap(file, BitmapDitherMode::None);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    drawRecentNoCoverPlaceholder(renderer, x, y, w, h, placeholderTitle, placeholderFontId);
    return;
  }
  {
    BitmapGrayStyleScope displayGray(renderer, displayImageBitmapGrayStyle());
    renderer.drawBitmap(bitmap, x, y, w, h);
  }
  file.close();
}

void RecentActivity::renderDefaultStatsGrid(int gridStartY, int screenW) {
  const int sel = selectorIndex;
  const int n = static_cast<int>(recentBooks.size());
  if (sel < 0 || sel >= n || screenW < 40) {
    return;
  }

  const RecentBook& curBook = recentBooks[static_cast<size_t>(sel)];
  BookReadingStats stats;
  const bool hasStats = !curBook.cachePath.empty() && loadBookStats(curBook.cachePath.c_str(), stats);

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
      hasSecond && !recentBooks[static_cast<size_t>(compareIdx)].cachePath.empty() &&
      loadBookStats(recentBooks[static_cast<size_t>(compareIdx)].cachePath.c_str(), secondStats);
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
    renderer.drawText(VALUE_FONT, x, y, primaryText, true, EpdFontFamily::BOLD);
    if (!showCompare) {
      return;
    }
    const int iconX = x + renderer.getTextWidth(VALUE_FONT, primaryText) + 10;
    if (curVal > othVal) {
      renderer.drawIcon(Up, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
    } else if (curVal < othVal) {
      renderer.drawIcon(Down, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
    }
    snprintf(secBuf, sizeof(secBuf), "%u", othVal);
    renderer.drawText(CMP_FONT, iconX + kCmpIconSz + kCmpGapAfterIcon, y + kCmpValY, secBuf, true,
                      EpdFontFamily::BOLD);
  };

  auto drawComparedTime = [&](int x, int y, uint32_t curMs, uint32_t othMs) {
    const std::string curStr = formatTime(curMs);
    renderer.drawText(VALUE_FONT, x, y, curStr.c_str(), true, EpdFontFamily::BOLD);
    if (!showCompare) {
      return;
    }
    const int iconX = x + renderer.getTextWidth(VALUE_FONT, curStr.c_str()) + 10;
    if (curMs > othMs) {
      renderer.drawIcon(Up, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
    } else if (curMs < othMs) {
      renderer.drawIcon(Down, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
    }
    const std::string othStr = formatTime(othMs);
    renderer.drawText(CMP_FONT, iconX + kCmpIconSz + kCmpGapAfterIcon, y + kCmpValY, othStr.c_str(), true,
                      EpdFontFamily::BOLD);
  };

  auto drawComparedAvgPage = [&](int x, int y, uint32_t curMs, uint32_t othMs) {
    if (curMs > 0) {
      snprintf(buffer, sizeof(buffer), "%u s", curMs / 1000);
    } else {
      snprintf(buffer, sizeof(buffer), "-");
    }
    renderer.drawText(VALUE_FONT, x, y, buffer, true, EpdFontFamily::BOLD);
    if (!showCompare) {
      return;
    }
    if (othMs > 0) {
      snprintf(secBuf, sizeof(secBuf), "%u s", othMs / 1000);
    } else {
      snprintf(secBuf, sizeof(secBuf), "-");
    }
    const int iconX = x + renderer.getTextWidth(VALUE_FONT, buffer) + 10;
    if (curMs > othMs) {
      renderer.drawIcon(Up, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
    } else if (curMs < othMs) {
      renderer.drawIcon(Down, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
    }
    renderer.drawText(CMP_FONT, iconX + kCmpIconSz + kCmpGapAfterIcon, y + kCmpValY, secBuf, true,
                      EpdFontFamily::BOLD);
  };

  auto drawComparedProgressPct = [&](int x, int y, float curPct, float othPct) {
    const bool curOk = curPct >= 0.0f;
    if (curOk) {
      snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(curPct + 0.5f));
    } else {
      snprintf(buffer, sizeof(buffer), "-");
    }
    renderer.drawText(VALUE_FONT, x, y, buffer, true, EpdFontFamily::BOLD);
    if (!showCompare) {
      return;
    }
    const bool othOk = othPct >= 0.0f;
    if (othOk) {
      snprintf(secBuf, sizeof(secBuf), "%d%%", static_cast<int>(othPct + 0.5f));
    } else {
      snprintf(secBuf, sizeof(secBuf), "-");
    }
    const int iconX = x + renderer.getTextWidth(VALUE_FONT, buffer) + 10;
    const int curInt = curOk ? static_cast<int>(curPct + 0.5f) : -1;
    const int othInt = othOk ? static_cast<int>(othPct + 0.5f) : -1;
    if (curInt >= 0 && othInt >= 0) {
      if (curInt > othInt) {
        renderer.drawIcon(Up, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
      } else if (curInt < othInt) {
        renderer.drawIcon(Down, iconX, y + kCmpIconY, kCmpIconSz, kCmpIconSz);
      }
    }
    renderer.drawText(CMP_FONT, iconX + kCmpIconSz + kCmpGapAfterIcon, y + kCmpValY, secBuf, true,
                      EpdFontFamily::BOLD);
  };

  const uint32_t curTime = hasStats ? stats.totalReadingTimeMs : 0;
  const uint32_t othTime = hasSecondStats ? secondStats.totalReadingTimeMs : 0;
  drawComparedTime(col1X, gridStartY, curTime, othTime);
  renderer.drawText(LABEL_FONT, col1X, gridStartY + 40, "Reading Time", true);

  const uint32_t curPages = hasStats ? stats.totalPagesRead : 0;
  const uint32_t othPages = hasSecondStats ? secondStats.totalPagesRead : 0;
  snprintf(buffer, sizeof(buffer), "%u", curPages);
  drawComparedUint(col2X, gridStartY, buffer, curPages, othPages);
  renderer.drawText(LABEL_FONT, col2X, gridStartY + 40, "Pages", true);

  const int row2Y = gridStartY + rowHeight;
  const uint32_t curCh = hasStats ? stats.totalChaptersRead : 0;
  const uint32_t othCh = hasSecondStats ? secondStats.totalChaptersRead : 0;
  snprintf(buffer, sizeof(buffer), "%u", curCh);
  drawComparedUint(col1X, row2Y, buffer, curCh, othCh);
  renderer.drawText(LABEL_FONT, col1X, row2Y + 40, "Chapters", true);

  const uint32_t curAvg = hasStats ? stats.avgPageTimeMs : 0;
  const uint32_t othAvg = hasSecondStats ? secondStats.avgPageTimeMs : 0;
  drawComparedAvgPage(col2X, row2Y, curAvg, othAvg);
  renderer.drawText(LABEL_FONT, col2X, row2Y + 40, "Average / Page", true);

  const int row3Y = gridStartY + rowHeight * 2;
  const uint32_t curSess = hasStats ? stats.sessionCount : 0;
  const uint32_t othSess = hasSecondStats ? secondStats.sessionCount : 0;
  snprintf(buffer, sizeof(buffer), "%u", curSess);
  drawComparedUint(col1X, row3Y, buffer, curSess, othSess);
  renderer.drawText(LABEL_FONT, col1X, row3Y + 40, "Session", true);

  const float curProg =
      hasStats ? stats.progressPercent
               : ((curBook.progress >= 0.0f && curBook.progress <= 1.0f) ? curBook.progress * 100.0f : -1.0f);
  const float othProg = hasSecondStats ? secondStats.progressPercent : -1.0f;
  drawComparedProgressPct(col2X, row3Y, curProg, othProg);
  renderer.drawText(LABEL_FONT, col2X, row3Y + 40, "Progress", true);
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
    renderer.fillRect(slotX, rowY, thumbW, thumbH, false);

    const std::string cdir = cacheDirAt(bi);
    const std::string ttl = titleAt(bi);
    drawRecentThumbnailAt(slotX, rowY, thumbW, thumbH, cdir, ttl, ATKINSON_HYPERLEGIBLE_12_FONT_ID);
    if (sel) {
      renderer.drawRect(slotX - 2, rowY - 2, thumbW + 4, thumbH + 4, true, false);
      if (thumbW > 6 && thumbH > 6) {
        renderer.drawRect(slotX, rowY, thumbW, thumbH, true, false);
      }
    } else {
      renderer.drawRect(slotX, rowY, thumbW, thumbH, true, false);
    }
  }
}

/**
 * Calculates the number of rows that can be displayed on screen at once.
 */
int RecentActivity::getVisibleRows() const {
  if (currentViewMode == ViewMode::Grid) {
    int screenHeight = renderer.getScreenHeight();
    int availableHeight = screenHeight - TAB_BAR_HEIGHT - 20;
    return (availableHeight > 0) ? availableHeight / GRID_ITEM_HEIGHT : 1;
  }
  return LIST_VISIBLE_ITEMS;
}

/**
 * Loads recent books from persistent storage.
 */
void RecentActivity::loadRecentBooks(const bool resetScroll) {
  recentBooks.clear();
  recentBooks.reserve(MAX_RECENT_BOOKS);
  if (resetScroll) {
    scrollOffset = 0;
    scrollOffsetDefault = 0;
  }

  const auto& allBooks = RECENT_BOOKS.getBooks();
  size_t addedCount = 0;

  for (size_t i = 0; i < allBooks.size() && addedCount < MAX_RECENT_BOOKS; ++i) {
    const auto& book = allBooks[i];
    if (!SdMan.exists(book.path.c_str())) {
      continue;
    }
    recentBooks.push_back(book);
    addedCount++;
  }
  rebuildListStatsFavorites();
  rebuildSimpleUiFavorites();
}

void RecentActivity::rebuildSimpleUiFavorites() {
  simpleUiFavorites_.clear();
  int added = 0;
  for (const auto& fb : BOOK_STATE.getFavoriteBooks()) {
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

void RecentActivity::rebuildListStatsFavorites() {
  listStatsFavoriteOnly_.clear();
  std::unordered_set<std::string> recentPaths;
  recentPaths.reserve(recentBooks.size());
  for (const auto& rb : recentBooks) {
    recentPaths.insert(rb.path);
  }
  for (const auto& fav : BOOK_STATE.getFavoriteBooks()) {
    if (recentPaths.count(fav.path) != 0) {
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

  halfRefreshOnLoadApplied_ = false;
  renderer.clearScreen(0xff);
  loadRecentBooks();

  if (SETTINGS.recentLibraryMode == SystemSetting::RECENT_GRID) {
    currentViewMode = ViewMode::Grid;
  }

  if (SETTINGS.recentLibraryMode == SystemSetting::RECENT_LIST) {
    currentViewMode = ViewMode::Default;
  }

  if (SETTINGS.recentLibraryMode == SystemSetting::RECENT_SIMPLE) {
    currentViewMode = ViewMode::SimpleUi;
    selectorIndex = 0;
    simpleUiFavScroll_ = 0;
  }

  updateRequired = true;
}

/**
 * Cleans up resources when exiting the recent activity.
 */
void RecentActivity::onExit() {
  recentBooks.clear();
  listStatsFavoriteOnly_.clear();
  simpleUiFavorites_.clear();
  Activity::onExit();
}

/**
 * Renders the complete grid view including all visible books.
 */
void RecentActivity::renderGrid(int startY) {
  renderTabBar(renderer);
  int totalBooks = static_cast<int>(recentBooks.size());
  if (totalBooks == 0) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, startY + 150, "No recent books");
    return;
  }

  int visibleRows = getVisibleRows();
  int startRow = scrollOffset;
  int endRow = std::min(startRow + visibleRows, (totalBooks + GRID_COLS - 1) / GRID_COLS);

  int maxItemsPerRender = 4;
  int itemsRendered = 0;

  for (int row = startRow; row < endRow; ++row) {
    for (int col = 0; col < GRID_COLS; ++col) {
      int bookIdx = row * GRID_COLS + col;
      if (bookIdx >= totalBooks) break;

      if (itemsRendered >= maxItemsPerRender) {
        return;
      }

      bool isSelected = (selectorIndex == bookIdx);
      renderGridItem(col, row - startRow, startY, recentBooks[bookIdx], isSelected);
      itemsRendered++;
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
  int coverHeight = static_cast<int>((containerHeight));

  bool coverDrawn = false;

  if (!book.cachePath.empty()) {
    char thumbPath[128];
    snprintf(thumbPath, sizeof(thumbPath), "%s/thumb.bmp", book.cachePath.c_str());

    FsFile file;
    if (SdMan.openFileForRead("RECENT", thumbPath, file)) {
      Bitmap bitmap(file, BitmapDitherMode::None);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        int bw = bitmap.getWidth() > 225 ? containerWidth : bitmap.getWidth();
        int bh = bitmap.getHeight() > 340 ? 340 : bitmap.getHeight();

        int scaledW = bw * 98 / 100;
        int scaledH = bh * 98 / 100;

        int drawX = coverAreaX + (containerWidth - scaledW) / 2;
        int drawY = coverAreaY + (coverHeight - scaledH) / 2 + GRID_SPACING;
        file.close();
        drawRecentThumbnailAt(drawX, drawY, scaledW, scaledH, book.cachePath, bookDisplayTitle(book),
                               ATKINSON_HYPERLEGIBLE_10_FONT_ID);
        coverDrawn = true;
      } else {
        file.close();
      }
    }
  }

  if (!coverDrawn) {
    const int bookX = coverAreaX;
    const int bookY = coverAreaY + GRID_SPACING;
    const int bookWidth = containerWidth;
    const int bookHeightInt = static_cast<int>(containerHeight);
    drawRecentNoCoverPlaceholder(renderer, bookX, bookY, bookWidth, bookHeightInt, bookDisplayTitle(book),
                                 ATKINSON_HYPERLEGIBLE_10_FONT_ID);
  }

  if (book.progress >= 0.0f && book.progress <= 1.0f) {
    int barX = coverAreaX + 15;
    int barY = coverAreaY + containerHeight;
    int barW = containerWidth - 30;
    int barH = 10;

    renderer.fillRect(barX, barY, barW, barH, false);
    renderer.drawRect(barX, barY, barW, barH, true);

    if (book.progress > 0.0f) {
      int fillW = static_cast<int>(barW * book.progress + 0.5f);
      renderer.fillRect(barX, barY, fillW, barH);

      char pText[8];
      int percent = static_cast<int>(book.progress * 100.0f + 0.5f);
      snprintf(pText, sizeof(pText), "%d%%", percent);
      int pW = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, pText);
      renderer.fillRect(barX + barW - pW - 5, barY - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID) - 10,
                        pW + 5, 30, false, true);
      renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, barX + barW - pW,
                        barY - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID) - 6, pText);
    }
  }
}

void RecentActivity::pumpDisplayFromLoop() {
  if (!updateRequired) {
    return;
  }
  renderer.clearScreen();
  renderTabBar(renderer);

  if (currentViewMode == ViewMode::Default) {
    renderDefault();
  } else if (currentViewMode == ViewMode::Grid) {
    renderGrid(TAB_BAR_HEIGHT - 29);
  } else if (currentViewMode == ViewMode::SimpleUi) {
    renderSimpleUi();
  } else {
    renderFlow();
  }

  const auto labels = mappedInput.mapLabels("Remove", "Open", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

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
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, renderer.getScreenHeight() / 2, "No recent books");
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
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID,
                              bodyTop + std::max(8, carouselH / 2 - 8), "No recent books");
  }

  const int belowY = bodyTop + carouselH;
  const int kSepFontCur = ATKINSON_HYPERLEGIBLE_18_FONT_ID;
  const int kSepFontSlash = ATKINSON_HYPERLEGIBLE_18_FONT_ID;
  const int kSepFontPrev = ATKINSON_HYPERLEGIBLE_14_FONT_ID;
  const int lhCur = renderer.getLineHeight(kSepFontCur);
  const int lhPrev = renderer.getLineHeight(kSepFontPrev);
  constexpr int kSepToGridGap = 22;
  if (belowY < bodyBottom) {
    renderer.fillRect(0, belowY, screenW, bodyBottom - belowY, false);
    renderer.drawLine(0, belowY, screenW, belowY, true);
    if (recentCount > 0) {
      const int sepTextY = belowY + 8;
      int tx = 30;
      renderer.drawText(kSepFontCur, tx, sepTextY, "Current", true, EpdFontFamily::BOLD);
      tx += renderer.getTextWidth(kSepFontCur, "Current", EpdFontFamily::BOLD);
      renderer.drawText(kSepFontSlash, tx, sepTextY, " / ", true, EpdFontFamily::REGULAR);
      tx += renderer.getTextWidth(kSepFontSlash, " / ", EpdFontFamily::REGULAR);
      const int prevY = sepTextY + (lhCur - lhPrev) / 2;
      renderer.drawText(kSepFontPrev, tx, prevY, "Previous", true, EpdFontFamily::BOLD);

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
          if (currentViewMode == ViewMode::Grid) {
            const int visibleRows = getVisibleRows();
            const int totalRows = (n + GRID_COLS - 1) / GRID_COLS;
            const int maxScroll = std::max(0, totalRows - visibleRows);
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
    ViewMode expectedMode = ViewMode::Flow;
    if (SETTINGS.recentLibraryMode == SystemSetting::RECENT_GRID) {
      expectedMode = ViewMode::Grid;
    } else if (SETTINGS.recentLibraryMode == SystemSetting::RECENT_LIST) {
      expectedMode = ViewMode::Default;
    } else if (SETTINGS.recentLibraryMode == SystemSetting::RECENT_SIMPLE) {
      expectedMode = ViewMode::SimpleUi;
    } else {
      expectedMode = ViewMode::Flow;
    }
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
  } else {
    if (downPressed && selectorIndex < totalBooks - 1) {
      selectorIndex++;
      selectorChanged = true;
    } else if (upPressed && selectorIndex > 0) {
      selectorIndex--;
      selectorChanged = true;
    }

    if (selectorChanged) {
      if (currentViewMode == ViewMode::Default) {
        int visibleItems = LIST_VISIBLE_ITEMS;
        if (selectorIndex < scrollOffset) {
          scrollOffset = selectorIndex;
        } else if (selectorIndex >= scrollOffset + visibleItems) {
          scrollOffset = selectorIndex - visibleItems + 1;
        }
        int maxOffset = std::max(0, totalBooks - visibleItems);
        scrollOffset = std::max(0, std::min(scrollOffset, maxOffset));
      } else {
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
      }
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
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, renderer.getScreenHeight() / 2, "No recent books");
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
    drawRecentThumbnailAt(leftX, sideY, sideW, sideH, leftBook.cachePath, bookDisplayTitle(leftBook),
                          ATKINSON_HYPERLEGIBLE_10_FONT_ID);
  }

  if (currentIndex + 1 < totalBooks) {
    const RecentBook& rightBook = recentBooks[currentIndex + 1];
    drawRecentThumbnailAt(rightX, sideY, sideW, sideH, rightBook.cachePath, bookDisplayTitle(rightBook),
                          ATKINSON_HYPERLEGIBLE_10_FONT_ID);
  }

  const RecentBook& currentBook = recentBooks[currentIndex];
  renderer.fillRect(centerX, centerY, centerW, centerH, false);
  drawRecentThumbnailAt(centerX, centerY, centerW, centerH, currentBook.cachePath, bookDisplayTitle(currentBook),
                        ATKINSON_HYPERLEGIBLE_14_FONT_ID);

  BookReadingStats stats;
  bool hasStats = false;
  if (!currentBook.cachePath.empty()) {
    hasStats = loadBookStats(currentBook.cachePath.c_str(), stats);
  }

  const int VALUE_FONT = ATKINSON_HYPERLEGIBLE_16_FONT_ID;
  const int LABEL_FONT = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  int statsX = 30;
  int statsY = carouselY + carouselH + 25;
  renderer.drawLine(0, carouselY + carouselH + 10,  screenW, carouselY + carouselH + 10, true);
  std::string title;
  if (!currentBook.title.empty()) {
    title = currentBook.title;
  } else {
    title = formatTitle(getBaseFilename(currentBook.path));
  }
  std::string truncatedTitle =
      renderer.truncatedText(ATKINSON_HYPERLEGIBLE_18_FONT_ID, title.c_str(), screenW - 60, EpdFontFamily::BOLD);
  renderer.drawText(ATKINSON_HYPERLEGIBLE_18_FONT_ID, statsX, statsY, truncatedTitle.c_str(), true,
                    EpdFontFamily::BOLD);

  int authorY = statsY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_18_FONT_ID) - 5;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, statsX, authorY, currentBook.author.c_str());

  float progress = hasStats ? stats.progressPercent : (currentBook.progress * 100.0f);
  if (progress >= 0) {
    int barY = authorY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID) + 20;
    int barW = (screenW - 60) * 0.5;
    int barH = 6;

    renderer.fillRect(statsX, barY, barW, barH, false);
    renderer.drawRect(statsX, barY, barW, barH, true);
    if (progress > 0) {
      int fillW = (int)(barW * (progress / 100.0f));
      renderer.fillRect(statsX, barY, fillW, barH);
    }

    char percentText[8];
    int percent = (int)(progress + 0.5f);
    snprintf(percentText, sizeof(percentText), "%d%%", percent);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, statsX + barW + 12, barY - 13, percentText);
  }

  if (hasStats) {
    char buffer[32];

    int gridStartY = authorY + 100;
    int col1X = statsX;
    int col2X = (screenW) / 2;
    int rowHeight = 95;

    std::string timeStr = formatTime(stats.totalReadingTimeMs);
    renderer.drawText(VALUE_FONT, col1X, gridStartY, timeStr.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawText(LABEL_FONT, col1X, gridStartY + 40, "Reading Time", true);

    snprintf(buffer, sizeof(buffer), "%u", stats.totalPagesRead);
    renderer.drawText(VALUE_FONT, col2X, gridStartY, buffer, true, EpdFontFamily::BOLD);
    renderer.drawText(LABEL_FONT, col2X, gridStartY + 40, "Pages", true);

    int row2Y = gridStartY + rowHeight;

    snprintf(buffer, sizeof(buffer), "%u", stats.totalChaptersRead);
    renderer.drawText(VALUE_FONT, col1X, row2Y, buffer, true, EpdFontFamily::BOLD);
    renderer.drawText(LABEL_FONT, col1X, row2Y + 40, "Chapters", true);

    uint32_t avgPageTime = stats.avgPageTimeMs;
    if (avgPageTime > 0) {
      snprintf(buffer, sizeof(buffer), "%u s", avgPageTime / 1000);
    } else {
      snprintf(buffer, sizeof(buffer), "-");
    }
    renderer.drawText(VALUE_FONT, col2X, row2Y, buffer, true, EpdFontFamily::BOLD);
    renderer.drawText(LABEL_FONT, col2X, row2Y + 40, "Average / Page", true);
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
    renderer.fillRect(rx, ry, m.thumbW, m.thumbH, false);
    const std::string cdir = b.cachePath.empty() ? epubCachePathForBookPath(b.path) : b.cachePath;
    drawRecentThumbnailAt(rx, ry, m.thumbW, m.thumbH, cdir, bookDisplayTitle(b), ATKINSON_HYPERLEGIBLE_12_FONT_ID);
    if (sel) {
      renderer.drawRect(rx - 2, ry - 2, m.thumbW + 4, m.thumbH + 4, true, false);
    } else {
      renderer.drawRect(rx, ry, m.thumbW, m.thumbH, true, false);
    }

    const int titleFont = ATKINSON_HYPERLEGIBLE_16_FONT_ID;
    const int authorFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
    std::string titleStr = b.title.empty() ? formatTitle(getBaseFilename(b.path)) : b.title;
    const int textX = rx + m.thumbW + 18;
    const int maxTextW = std::max(40, screenW - textX - m.marginL);
    const std::string titleDraw =
        renderer.truncatedText(titleFont, titleStr.c_str(), maxTextW, EpdFontFamily::BOLD);
    const int lhTitle = renderer.getLineHeight(titleFont);
    const int lhAuthor = renderer.getLineHeight(authorFont);
    const int authorGap = 10;
    const int blockH = lhTitle + authorGap + lhAuthor;
    const int textY = m.bodyTop + (m.topBandH - blockH) / 2;
    renderer.drawText(titleFont, textX, textY, titleDraw.c_str(), true, EpdFontFamily::BOLD);
    const std::string auth = b.author.empty() ? std::string() : b.author;
    const std::string authDraw = renderer.truncatedText(authorFont, auth.c_str(), maxTextW);
    renderer.drawText(authorFont, textX, textY + lhTitle + authorGap, authDraw.c_str(), true);
  } else {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, m.bodyTop + std::max(20, m.topBandH / 2 - 16),
                              "No recent books", true);
  }

  if (m.favTop < m.bodyBottom) {
    renderer.fillRect(0, m.favTop, screenW, m.bodyBottom - m.favTop, false);
    renderer.drawLine(0, m.favTop, screenW, m.favTop, true);
    const int favHdrFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
    renderer.drawText(favHdrFont, m.marginL, m.favTop + kFavHeaderPadTop, "Favorites", true,
                        EpdFontFamily::BOLD);
    const int hdrSepY = m.favListTop - 1;
    if (hdrSepY > m.favTop) {
      renderer.drawLine(0, hdrSepY, screenW, hdrSepY, true);
    }
  }

  const int favFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
  const int lh = renderer.getLineHeight(favFont);
  constexpr int kPadY = 20;
  clampSimpleUiFavoriteScroll(m.maxVis);

  const int fc = static_cast<int>(simpleUiFavorites_.size());

  if (fc == 0) {
    const int subFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
    const char* line1 = "No favorites yet.";
    const char* line2 = "Star a book in the Library to add it here.";
    const int w1 = renderer.getTextWidth(favFont, line1);
    const int w2 = renderer.getTextWidth(subFont, line2);
    const int lh2 = renderer.getLineHeight(subFont);
    const int block = lh + 12 + lh2;
    const int paneH = m.bodyBottom - m.favListTop;
    const int y0 = m.favListTop + std::max(4, (paneH - block) / 2);
    renderer.drawText(favFont, (screenW - w1) / 2, y0, line1, true);
    renderer.drawText(subFont, (screenW - w2) / 2, y0 + lh + 12, line2, true);
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
      renderer.fillRect(0, rowY, screenW, m.rowH, GfxRenderer::FillTone::Ink);
    }
    const int textY = rowY + kPadY;
    const int maxTitleW = std::max(40, screenW - titleX - m.marginL);
    std::string disp = fb.title.empty() ? formatTitle(getBaseFilename(fb.path)) : fb.title;
    const std::string trunc = renderer.truncatedText(favFont, disp.c_str(), maxTitleW);
    renderer.drawIcon(Star, starX, textY + 2, 24, 24, GfxRenderer::None, rowSel);
    renderer.drawText(favFont, titleX , textY, trunc.c_str(), !rowSel);
    rowY += m.rowH;
    renderer.drawLine(0, rowY, screenW, rowY, true);
  }
}