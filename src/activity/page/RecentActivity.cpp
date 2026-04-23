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

/**
 * RAII wrapper for mutex operations with timeout.
 */
class MutexGuard {
 private:
  SemaphoreHandle_t& mutex;
  bool acquired;

 public:
  explicit MutexGuard(SemaphoreHandle_t& m) : mutex(m), acquired(false) {
    if (mutex) {
      acquired = (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE);
    }
  }

  ~MutexGuard() {
    if (acquired && mutex) {
      xSemaphoreGive(mutex);
    }
  }

  bool isAcquired() const { return acquired; }
};

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
}  // namespace

void RecentActivity::noteThumbnailGrayscaleJob(const std::string& cacheDir, int drawX, int drawY, int drawW,
                                               int drawH) {
  if (!SETTINGS.readerImageGrayscale || cacheDir.empty() || drawW <= 0 || drawH <= 0) {
    return;
  }
  ThumbnailGrayscaleJob job;
  job.cacheDir = cacheDir;
  job.drawX = drawX;
  job.drawY = drawY;
  job.drawW = drawW;
  job.drawH = drawH;
  thumbnailGrayscaleJobs_.push_back(std::move(job));
}

void RecentActivity::runThumbnailGrayscalePassIfNeeded() {
  if (!SETTINGS.readerImageGrayscale || thumbnailGrayscaleJobs_.empty() || !renderer.needsBitmapGrayscale()) {
    thumbnailGrayscaleJobs_.clear();
    return;
  }

  const bool storedBwBuffer = renderer.storeBwBuffer();
  const BitmapDitherMode dither = bitmapDitherModeFromSetting(SETTINGS.displayImageDither);

  auto drawThumbsForMode = [&](GfxRenderer::RenderMode mode) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(mode);
    BitmapGrayStyleScope grayScope(renderer, displayImageBitmapGrayStyle());
    for (const ThumbnailGrayscaleJob& job : thumbnailGrayscaleJobs_) {
      char path[160];
      snprintf(path, sizeof(path), "%s/thumb.bmp", job.cacheDir.c_str());
      FsFile file;
      if (!SdMan.openFileForRead("RECENT", path, file)) {
        continue;
      }
      Bitmap bitmap(file, dither);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderer.drawBitmap(bitmap, job.drawX, job.drawY, job.drawW, job.drawH);
      }
      file.close();
    }
  };

  drawThumbsForMode(GfxRenderer::GRAYSCALE_LSB);
  renderer.copyGrayscaleLsbBuffers();
  drawThumbsForMode(GfxRenderer::GRAYSCALE_MSB);
  renderer.copyGrayscaleMsbBuffers();
  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  if (storedBwBuffer) {
    renderer.restoreBwBuffer();
  } else {
    renderer.cleanupGrayscaleWithFrameBuffer();
  }
  thumbnailGrayscaleJobs_.clear();
}

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
  Bitmap bitmap(file, bitmapDitherModeFromSetting(SETTINGS.displayImageDither));
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
  noteThumbnailGrayscaleJob(cacheDir, x, y, w, h);
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
  constexpr int kCmpIcon = 32;
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
    const int iconX = x + renderer.getTextWidth(VALUE_FONT, primaryText) + 6;
    const int iconY = y;
    if (curVal > othVal) {
      renderer.drawIcon(Up, iconX, iconY + 4, kCmpIcon, kCmpIcon);
    } else if (curVal < othVal) {
      renderer.drawIcon(Down, iconX, iconY + 4, kCmpIcon, kCmpIcon);
    }
    snprintf(secBuf, sizeof(secBuf), "%u", othVal);
    renderer.drawText(CMP_FONT, iconX + 40, iconY + 8, secBuf, true, EpdFontFamily::BOLD);
  };

  auto drawComparedTime = [&](int x, int y, uint32_t curMs, uint32_t othMs) {
    const std::string curStr = formatTime(curMs);
    renderer.drawText(VALUE_FONT, x, y, curStr.c_str(), true, EpdFontFamily::BOLD);
    if (!showCompare) {
      return;
    }
    const int iconX = x + renderer.getTextWidth(VALUE_FONT, curStr.c_str()) + 6;
    const int iconY = y;
    if (curMs > othMs) {
      renderer.drawIcon(Up, iconX, iconY + 4, kCmpIcon, kCmpIcon);
    } else if (curMs < othMs) {
      renderer.drawIcon(Down, iconX, iconY + 4, kCmpIcon, kCmpIcon);
    }
    const std::string othStr = formatTime(othMs);
    renderer.drawText(CMP_FONT, iconX + 40, iconY + 8, othStr.c_str(), true, EpdFontFamily::BOLD);
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
    const int iconX = x + renderer.getTextWidth(VALUE_FONT, buffer) + 6;
    const int iconY = y;
    if (curMs > othMs) {
      renderer.drawIcon(Up, iconX, iconY + 4, kCmpIcon, kCmpIcon);
    } else if (curMs < othMs) {
      renderer.drawIcon(Down, iconX, iconY + 4, kCmpIcon, kCmpIcon);
    }
    renderer.drawText(CMP_FONT, iconX + 40, iconY + 8, secBuf, true, EpdFontFamily::BOLD);
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
    const int iconX = x + renderer.getTextWidth(VALUE_FONT, buffer) + 6;
    const int iconY = y;
    const int curInt = curOk ? static_cast<int>(curPct + 0.5f) : -1;
    const int othInt = othOk ? static_cast<int>(othPct + 0.5f) : -1;
    if (curInt >= 0 && othInt >= 0) {
      if (curInt > othInt) {
        renderer.drawIcon(Up, iconX, iconY + 4, kCmpIcon, kCmpIcon);
      } else if (curInt < othInt) {
        renderer.drawIcon(Down, iconX, iconY + 4, kCmpIcon, kCmpIcon);
      }
    }
    renderer.drawText(CMP_FONT, iconX + 40, iconY + 8, secBuf, true, EpdFontFamily::BOLD);
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
  constexpr int kFlowSideW = 189;
  constexpr int kFlowSideH = 286;
  const int marginInner = 8;
  const int bandRight = bandX + bandW - marginInner;

  int thumbW = kFlowSideW;
  int thumbH = kFlowSideH;
  if (thumbH > bandH - 16) {
    thumbH = std::max(100, bandH - 16);
    thumbW = std::max(60, thumbH * kFlowSideW / kFlowSideH);
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
 * Static trampoline function for FreeRTOS task creation.
 */
void RecentActivity::taskTrampoline(void* param) { static_cast<RecentActivity*>(param)->displayTaskLoop(); }

/**
 * Initializes the recent activity when entered.
 */
void RecentActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  if (!renderingMutex) return;
  halfRefreshOnLoadApplied_ = false;
  renderer.clearScreen(0xff);
  loadRecentBooks();

  if (SETTINGS.recentLibraryMode == SystemSetting::RECENT_GRID) {
    currentViewMode = ViewMode::Grid;
  }

  if (SETTINGS.recentLibraryMode == SystemSetting::RECENT_LIST) {
    currentViewMode = ViewMode::Default;
  }

  if (displayTaskHandle == nullptr) {
    xTaskCreate(&RecentActivity::taskTrampoline, "RecentTask", 8192, this, 1, &displayTaskHandle);
  }

  updateRequired = true;
}

/**
 * Cleans up resources when exiting the recent activity.
 */
void RecentActivity::onExit() {
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }

  recentBooks.clear();
  listStatsFavoriteOnly_.clear();
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
      Bitmap bitmap(file, bitmapDitherModeFromSetting(SETTINGS.displayImageDither));
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

/**
 * Display task loop that runs in a separate FreeRTOS task.
 */
void RecentActivity::displayTaskLoop() {
  while (true) {
    {
      MutexGuard guard(renderingMutex);
      if (guard.isAcquired() && updateRequired) {
        renderer.resetBitmapGrayscaleDetection();
        thumbnailGrayscaleJobs_.clear();
        renderer.clearScreen();
        renderTabBar(renderer);

        if (currentViewMode == ViewMode::Default) {
          renderDefault();
        } else if (currentViewMode == ViewMode::Grid) {
          renderGrid(TAB_BAR_HEIGHT - 29);
        } else {
          renderFlow();
        }

        const auto labels = mappedInput.mapLabels("Remove", "Open", "", "");
        renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3,
                                 labels.btn4);

        renderer.displayBuffer();
        runThumbnailGrayscalePassIfNeeded();
        if (!halfRefreshOnLoadApplied_) {
          halfRefreshOnLoadApplied_ = true;
          SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Recent);
        }
        updateRequired = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
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
    auto clampH = [](int sel, int count, int& hScroll) {
      if (count <= 0) {
        return;
      }
      const int maxH = std::max(0, count - kRecentStripSlots);
      int h = std::min(maxH, sel);
      h = std::max(0, std::max(h, sel - (kRecentStripSlots - 1)));
      hScroll = h;
    };
    clampH(selectorIndex, recentCount, listStatsRecentHScroll);
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
  if (belowY < bodyBottom) {
    renderer.fillRect(0, belowY, screenW, bodyBottom - belowY, false);
    if (recentCount > 0) {
      renderDefaultStatsGrid(belowY + 8, screenW);
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

  const int totalBooks = static_cast<int>(recentBooks.size());
  const bool isDefaultView = (currentViewMode == ViewMode::Default);

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
      if (isDefaultView && totalBooks > 0 && selectorIndex >= 0 && selectorIndex < totalBooks) {
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
        updateRequired = true;
      } else if (!isDefaultView && totalBooks > 0 && selectorIndex >= 0 && selectorIndex < totalBooks) {
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

  if (!isDefaultView && totalBooks == 0) {
    return;
  }
  if (isDefaultView && totalBooks == 0) {
    return;
  }

  if (!isDefaultView) {
    ViewMode newViewMode = ViewMode::Flow;
    if (SETTINGS.recentLibraryMode == SystemSetting::RECENT_GRID) {
      newViewMode = ViewMode::Grid;
    }

    if (SETTINGS.recentLibraryMode == SystemSetting::RECENT_LIST) {
      newViewMode = ViewMode::Default;
    }

    if (newViewMode != currentViewMode) {
      currentViewMode = newViewMode;
      scrollOffset = 0;
      selectorIndex = 0;
      updateRequired = true;
      return;
    }
  }

  bool selectorChanged = false;

  if (isDefaultView) {
    auto clampListHScroll = [](int sel, int count, int& hScroll) {
      if (count <= 0) {
        return;
      }
      const int maxH = std::max(0, count - kRecentStripSlots);
      int h = std::min(maxH, sel);
      h = std::max(0, std::max(h, sel - (kRecentStripSlots - 1)));
      hScroll = h;
    };

    if (downPressed && selectorIndex < totalBooks - 1) {
      selectorIndex++;
      selectorChanged = true;
    }
    if (upPressed && selectorIndex > 0) {
      selectorIndex--;
      selectorChanged = true;
    }

    if (selectorChanged) {
      clampListHScroll(selectorIndex, totalBooks, listStatsRecentHScroll);
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
    bool drawn = false;
    if (!leftBook.cachePath.empty()) {
      char path[128];
      snprintf(path, sizeof(path), "%s/thumb.bmp", leftBook.cachePath.c_str());
      FsFile file;
      if (SdMan.openFileForRead("RECENT", path, file)) {
        Bitmap bitmap(file, bitmapDitherModeFromSetting(SETTINGS.displayImageDither));
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderer.drawBitmap(bitmap, leftX, sideY, sideW, sideH);
          drawn = true;
        }
        file.close();
      }
    }
    if (!drawn) {
      drawRecentNoCoverPlaceholder(renderer, leftX, sideY, sideW, sideH, bookDisplayTitle(leftBook),
                                   ATKINSON_HYPERLEGIBLE_10_FONT_ID);
    }
  }

  if (currentIndex + 1 < totalBooks) {
    const RecentBook& rightBook = recentBooks[currentIndex + 1];
    bool drawn = false;
    if (!rightBook.cachePath.empty()) {
      char path[128];
      snprintf(path, sizeof(path), "%s/thumb.bmp", rightBook.cachePath.c_str());
      FsFile file;
      if (SdMan.openFileForRead("RECENT", path, file)) {
        Bitmap bitmap(file, bitmapDitherModeFromSetting(SETTINGS.displayImageDither));
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderer.drawBitmap(bitmap, rightX, sideY, sideW, sideH);
          drawn = true;
        }
        file.close();
      }
    }
    if (!drawn) {
      drawRecentNoCoverPlaceholder(renderer, rightX, sideY, sideW, sideH, bookDisplayTitle(rightBook),
                                   ATKINSON_HYPERLEGIBLE_10_FONT_ID);
    }
  }

  const RecentBook& currentBook = recentBooks[currentIndex];
  bool centerDrawn = false;
  if (!currentBook.cachePath.empty()) {
    renderer.fillRect(centerX, centerY, centerW, centerH, false);
    char path[128];
    snprintf(path, sizeof(path), "%s/thumb.bmp", currentBook.cachePath.c_str());
    FsFile file;
    if (SdMan.openFileForRead("RECENT", path, file)) {
      Bitmap bitmap(file, bitmapDitherModeFromSetting(SETTINGS.displayImageDither));
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderer.drawBitmap(bitmap, centerX, centerY, centerW, centerH);
        centerDrawn = true;
      }
      file.close();
    }
  }
  if (!centerDrawn) {
    drawRecentNoCoverPlaceholder(renderer, centerX, centerY, centerW, centerH, bookDisplayTitle(currentBook),
                                 ATKINSON_HYPERLEGIBLE_14_FONT_ID);
  }

  BookReadingStats stats;
  bool hasStats = false;
  if (!currentBook.cachePath.empty()) {
    hasStats = loadBookStats(currentBook.cachePath.c_str(), stats);
  }

  const int VALUE_FONT = ATKINSON_HYPERLEGIBLE_16_FONT_ID;
  const int LABEL_FONT = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  int statsX = 30;
  int statsY = carouselY + carouselH + 25;

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