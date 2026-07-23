/**
 * @file RecentActivity.cpp
 * @brief Definitions for RecentActivity.
 */

#include "RecentActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HardwareSerial.h>
#include <ImageRender.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

#include "../reader/Epub/EpubActivity.h"
#include "../reader/Epub/EpubAnnotations.h"
#include "Epub/Page.h"
#include "Epub/Section.h"
#include "components/recent/RecentLayouts.h"
#include "images/Star.h"
#include "state/BookProgress.h"
#include "state/BookSetting.h"
#include "state/BookState.h"
#include "state/Session.h"
#include "state/Statistics.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiTheme.h"
#include "util/StringUtils.h"

extern bool sdCardAvailable;

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
    case SM::RECENT_LIST_DEPRECATED:
      return RecentActivity::ViewMode::Flow;
    case SM::RECENT_SIMPLE:
      return RecentActivity::ViewMode::SimpleUi;
    case SM::RECENT_BOOK_LIST:
      return RecentActivity::ViewMode::List;
    case SM::RECENT_ICONS:
      return RecentActivity::ViewMode::Icons;
    case SM::RECENT_COVER:
      return RecentActivity::ViewMode::Cover;
    case SM::RECENT_FLOW:
    default:
      return RecentActivity::ViewMode::Flow;
  }
}

/** No-cover: stats-style double frame + title one word per line, each line centered (see
 * StatisticActivity::renderCover). */
static void drawRecentNoCoverPlaceholder(GfxRenderer& renderer, int x, int y, int w, int h, const std::string& title,
                                         int fontId) {
  if (w <= 1 || h <= 1) {
    return;
  }
  const bool rr = SETTINGS.bitmapRoundedCorners != 0;
  const bool subtle = SETTINGS.bitmapRoundedCorners == 2;
  renderer.rectangle.fill(x, y, w, h, false, rr, subtle);
  if (!rr) {
    renderer.rectangle.render(x, y, w, h, true, false);
    if (w > 6 && h > 6) {
      renderer.rectangle.render(x + 2, y + 2, w - 4, h - 4, true, false);
    }
  }

  const int lh = renderer.text.getLineHeight(fontId);
  const int maxLines = std::max(1, (h - 12) / std::max(1, lh));
  int wordCount = 0;
  bool inWord = false;
  for (char c : title) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      inWord = false;
    } else if (!inWord) {
      inWord = true;
      ++wordCount;
    }
  }
  if (wordCount <= 0) {
    return;
  }
  const int linesToDraw = std::min(maxLines, wordCount);
  const int totalTextH = linesToDraw * lh;
  int lineY = y + std::max(4, (h - totalTextH) / 2);
  const int innerPad = 6;
  const int maxWordW = std::max(8, w - 2 * innerPad);

  size_t pos = 0;
  for (int line = 0; line < linesToDraw; ++line) {
    while (pos < title.size() && std::isspace(static_cast<unsigned char>(title[pos]))) {
      ++pos;
    }
    size_t end = pos;
    while (end < title.size() && !std::isspace(static_cast<unsigned char>(title[end]))) {
      ++end;
    }
    std::string wshow = title.substr(pos, end - pos);
    if (renderer.text.getWidth(fontId, wshow.c_str()) > maxWordW) {
      wshow = renderer.text.truncate(fontId, wshow.c_str(), maxWordW, EpdFontFamily::REGULAR);
    }
    const int tw = renderer.text.getWidth(fontId, wshow.c_str());
    renderer.text.render(fontId, x + (w - tw) / 2, lineY, wshow.c_str(), true, EpdFontFamily::REGULAR);
    lineY += lh;
    pos = end;
  }
}

static std::string epubCachePathForBookPath(const std::string& bookPath) {
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(bookPath));
}

struct IconRect {
  int x;
  int y;
  int w;
  int h;
};

static IconRect fitBookCoverRect(int x, int y, int w, int h) {
  int coverW = w;
  int coverH = coverW * RecentActivity::COVER_HEIGHT / RecentActivity::COVER_WIDTH;
  if (coverH > h) {
    coverH = h;
    coverW = coverH * RecentActivity::COVER_WIDTH / RecentActivity::COVER_HEIGHT;
  }
  return {x + (w - coverW) / 2, y + (h - coverH) / 2, coverW, coverH};
}

static IconRect inflateIconRect(const IconRect& rect, int percent) {
  const int extraW = std::max(2, rect.w * percent / 100);
  const int extraH = std::max(2, rect.h * percent / 100);
  return {rect.x - extraW / 2, rect.y - extraH / 2, rect.w + extraW, rect.h + extraH};
}

static void renderThickIconRect(const GfxRenderer& renderer, const IconRect& rect, bool rounded, int thickness) {
  for (int i = 0; i < thickness; ++i) {
    renderer.rectangle.render(rect.x - i, rect.y - i, rect.w + i * 2, rect.h + i * 2, true, rounded);
  }
}

static void drawRecentThumbnailPlaceholder(GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                                           const bool selected = false) {
  if (w <= 0 || h <= 0) {
    return;
  }
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(x, y, w, h, false, rounded);
  renderer.rectangle.render(x, y, w, h, true, rounded);
  if (selected) {
    renderer.rectangle.render(x - 2, y - 2, w + 4, h + 4, true, rounded);
  }
}

static void drawProgressBadge(const GfxRenderer& renderer, const IconRect& coverRect, float progress) {
  if (progress < 0.0f || progress > 1.0f) {
    return;
  }
  char label[8];
  snprintf(label, sizeof(label), "%d%%", static_cast<int>(progress * 100.0f + 0.5f));
  constexpr int font = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
  const int textW = renderer.text.getWidth(font, label);
  const int textH = renderer.text.getLineHeight(font);
  const int badgeW = std::max(22, textW + 8);
  const int badgeH = textH + 4;
  const int badgeX = coverRect.x + coverRect.w - badgeW - 2;
  const int badgeY = coverRect.y + 2;
  renderer.rectangle.fill(badgeX, badgeY, badgeW, badgeH, true);
  renderer.text.render(font, badgeX + (badgeW - textW) / 2, badgeY + 1, label, false, EpdFontFamily::BOLD);
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

}  // namespace

namespace {
constexpr int kRecentThumbGap = 20;
constexpr int kRecentStripSlots = 2;

/** Simple UI favorites: at most this many titles; list scrolls when more than visible rows. */
constexpr int kSimpleUiFavoritesMaxCount = 10;
constexpr int kSimpleUiFavoritesVisibleMax = 5;
constexpr int kFavHeaderPadTop = 10;
constexpr int kFavHeaderPadBottom = 8;
constexpr int kSimpleUiLabelFont = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
constexpr int kSimpleUiBodyFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
constexpr int kSimpleUiTitleFont = ATKINSON_HYPERLEGIBLE_14_FONT_ID;
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
  SimpleUiMetrics m;
  m.bodyTop = INX_THEME.mainContentTop() - 6 + 8;
  constexpr int kHintReserve = 52;
  m.bodyBottom =
      INX_THEME.mainTabsAtBottom() ? INX_THEME.mainContentBottom(renderer) : renderer.getScreenHeight() - kHintReserve;
  m.marginL = RecentActivity::GRID_SPACING;

  constexpr int kThumbPadV = 28;
  const int favFont = kSimpleUiBodyFont;
  const int lh = renderer.text.getLineHeight(favFont);
  constexpr int kPadY = 18;
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
  m.maxVis = std::min(kSimpleUiFavoritesVisibleMax, std::max(1, (m.bodyBottom - m.favListTop) / std::max(1, m.rowH)));
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

#include "components/recent/HomeMenuDrawer.ipp"

RecentActivity::~RecentActivity() {
  delete homeMenuDrawer_;
  homeMenuDrawer_ = nullptr;
  freeRecentPageBuffer();
}

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
  const std::string imagePath = resolveThumbnailPath(cacheDir);

  if (!imagePath.empty()) {
    ImageRender::Options options;
    options.cropToFill = true;
    options.useDisplayCache = true;
    if (SETTINGS.bitmapRoundedCorners == 0) {
      options.roundedOutside = BitmapRender::RoundedOutside::None;
    } else if (SETTINGS.bitmapRoundedCorners == 2) {
      options.roundedOutside = roundedCornerBackdropIsDither
                                   ? BitmapRender::RoundedOutside::SubtleSparseInkAlignedOutside
                                   : BitmapRender::RoundedOutside::SubtlePaperOutside;
    } else {
      options.roundedOutside = roundedCornerBackdropIsDither ? BitmapRender::RoundedOutside::SparseInkAlignedOutside
                                                             : BitmapRender::RoundedOutside::PaperOutside;
    }

    const ImageRender image = ImageRender::create(renderer, imagePath);
    if (image.renderDisplayCacheOnly(x, y, w, h, options)) {
      return;
    }
    if (queueRecentImageCacheBuild(imagePath, x, y, w, h, options.cropToFill, options.roundedOutside)) {
      drawRecentThumbnailPlaceholder(renderer, x, y, w, h);
      return;
    }
    renderer.rectangle.fill(x, y, w, h, false);
    if (image.render(x, y, w, h, options)) {
      return;
    }
  }

  drawRecentNoCoverPlaceholder(renderer, x, y, w, h, placeholderTitle, placeholderFontId);
}

void RecentActivity::drawRecentCoverFitAt(int x, int y, int w, int h, const std::string& cacheDir,
                                          const std::string& placeholderTitle, int placeholderFontId) {
  if (w < 8 || h < 8) {
    return;
  }
  if (cacheDir.empty()) {
    drawRecentNoCoverPlaceholder(renderer, x, y, w, h, placeholderTitle, placeholderFontId);
    return;
  }

  const std::string imagePath = resolveCoverPath(cacheDir);
  if (!imagePath.empty()) {
    ImageRender::Options options;
    options.cropToFill = false;
    options.useDisplayCache = true;
    if (SETTINGS.bitmapRoundedCorners == 0) {
      options.roundedOutside = BitmapRender::RoundedOutside::None;
    } else if (SETTINGS.bitmapRoundedCorners == 2) {
      options.roundedOutside = BitmapRender::RoundedOutside::SubtlePaperOutside;
    } else {
      options.roundedOutside = BitmapRender::RoundedOutside::PaperOutside;
    }

    const ImageRender image = ImageRender::create(renderer, imagePath);
    if (image.renderDisplayCacheOnly(x, y, w, h, options)) {
      return;
    }
    if (queueRecentImageCacheBuild(imagePath, x, y, w, h, options.cropToFill, options.roundedOutside)) {
      drawRecentThumbnailPlaceholder(renderer, x, y, w, h);
      return;
    }
    renderer.rectangle.fill(x, y, w, h, false, SETTINGS.bitmapRoundedCorners != 0);
    if (image.render(x, y, w, h, options)) {
      return;
    }
  }

  drawRecentNoCoverPlaceholder(renderer, x, y, w, h, placeholderTitle, placeholderFontId);
}

std::string RecentActivity::resolveThumbnailPath(const std::string& cacheDir) const {
  if (cacheDir.empty()) {
    return "";
  }
  const auto cached = thumbnailPathCache_.find(cacheDir);
  if (cached != thumbnailPathCache_.end()) {
    return cached->second;
  }

  char jpegPath[192];
  snprintf(jpegPath, sizeof(jpegPath), "%s/thumb.jpg", cacheDir.c_str());
  if (SdMan.exists(jpegPath)) {
    thumbnailPathCache_[cacheDir] = jpegPath;
    return jpegPath;
  }

  char pngPath[192];
  snprintf(pngPath, sizeof(pngPath), "%s/thumb.png", cacheDir.c_str());
  if (SdMan.exists(pngPath)) {
    thumbnailPathCache_[cacheDir] = pngPath;
    return pngPath;
  }

  char bmpPath[192];
  snprintf(bmpPath, sizeof(bmpPath), "%s/thumb.bmp", cacheDir.c_str());
  if (SdMan.exists(bmpPath)) {
    thumbnailPathCache_[cacheDir] = bmpPath;
    return bmpPath;
  }

  thumbnailPathCache_[cacheDir] = "";
  return "";
}

std::string RecentActivity::resolveCoverPath(const std::string& cacheDir) const {
  if (cacheDir.empty()) {
    return "";
  }
  const auto cached = coverPathCache_.find(cacheDir);
  if (cached != coverPathCache_.end()) {
    return cached->second;
  }

  const char* names[] = {"cover.jpg", "cover.png", "cover.bmp", "cover_crop.jpg", "cover_crop.bmp"};
  for (const char* name : names) {
    char path[192];
    snprintf(path, sizeof(path), "%s/%s", cacheDir.c_str(), name);
    if (SdMan.exists(path)) {
      coverPathCache_[cacheDir] = path;
      return path;
    }
  }

  const std::string fallback = resolveThumbnailPath(cacheDir);
  coverPathCache_[cacheDir] = fallback;
  return fallback;
}

/**
 * Calculates the number of rows that can be displayed on screen at once.
 */
int RecentActivity::getVisibleRows() const {
  if (currentViewMode == ViewMode::Icons) {
    return ICON_ROWS;
  }
  if (currentViewMode == ViewMode::Grid) {
    int availableHeight = INX_THEME.mainContentBottom(renderer) - recentGridPaintStartY() - 20;
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
  freeRecentPageBuffer();
  recentBooks.clear();
  recentStats_.clear();
  const int requestedCount = std::max(1, static_cast<int>(SETTINGS.recentVisibleCount));
  const int iconGridCount = ICON_COLS * ICON_ROWS;
  const bool iconMode = viewModeForLibrarySetting(SETTINGS.recentLibraryMode) == ViewMode::Icons;
  const int maxShow = std::min(MAX_RECENT_BOOKS, iconMode ? std::max(requestedCount, iconGridCount) : requestedCount);
  if (resetScroll) {
    scrollOffset = 0;
  }

  const auto& allBooks = RECENT_BOOKS.getBooks();
  size_t addedCount = 0;

  for (size_t i = 0; i < allBooks.size() && addedCount < maxShow; ++i) {
    const auto& book = allBooks[i];
    if (!SdMan.exists(book.path.c_str())) {
      continue;
    }
    recentBooks.push_back(book);
    recentStats_.push_back(CachedRecentStats{});
    addedCount++;
  }
  const std::vector<BookState::Book> favorites = BOOK_STATE.getFavoriteBooks();
  rebuildSimpleUiFavorites(favorites);
}

bool RecentActivity::openBookPath(const std::string& path, const std::string& title, const std::string& author,
                                  const bool removeMissingFromRecents) {
  if (path.empty()) {
    return false;
  }
  const std::string selectedPath = path;

  // SD access can transiently fail under contention (this page streams thumbnails/stats), making a present
  // book read as "missing". Retry a few times before treating it as gone, so a valid book always opens
  // instead of being removed from recents and resetting the selection to the first book.
  bool present = SdMan.exists(selectedPath.c_str());
  for (int attempt = 0; !present && attempt < 4; ++attempt) {
    delay(25);
    present = SdMan.exists(selectedPath.c_str());
  }

  if (!present) {
    if (removeMissingFromRecents) {
      RECENT_BOOKS.removeBook(selectedPath);
      loadRecentBooks(false);
      const int n = static_cast<int>(recentBooks.size());
      selectorIndex = n == 0 ? 0 : std::min(selectorIndex, n - 1);
      scrollOffset = 0;
      updateRequired = true;
    }
    return false;
  }

  bookSelected = true;
  onSelectBook(selectedPath);
  return true;
}

int RecentActivity::selectedRecentIndexForRemove() const {
  if (recentBooks.empty() || selectorIndex < 0 || selectorIndex >= static_cast<int>(recentBooks.size())) {
    return -1;
  }
  if (currentViewMode == ViewMode::SimpleUi && selectorIndex != 0) {
    return -1;
  }
  return selectorIndex;
}

void RecentActivity::beginRemoveConfirmation() {
  const int index = selectedRecentIndexForRemove();
  if (index < 0) {
    return;
  }
  removeConfirmIndex_ = index;
  removeConfirmOpen_ = true;
  freeRecentPageBuffer();
  updateRequired = true;
}

void RecentActivity::cancelRemoveConfirmation() {
  removeConfirmOpen_ = false;
  removeConfirmIndex_ = -1;
  freeRecentPageBuffer();
  updateRequired = true;
}

void RecentActivity::confirmRemoveRecent() {
  if (removeConfirmIndex_ >= 0 && removeConfirmIndex_ < static_cast<int>(recentBooks.size())) {
    RECENT_BOOKS.removeBook(recentBooks[static_cast<size_t>(removeConfirmIndex_)].path);
  }

  removeConfirmOpen_ = false;
  removeConfirmIndex_ = -1;
  loadRecentBooks(false);

  const int n = static_cast<int>(recentBooks.size());
  if (n == 0) {
    selectorIndex = 0;
    scrollOffset = 0;
  } else {
    if (selectorIndex >= n) {
      selectorIndex = n - 1;
    }
    if (currentViewMode == ViewMode::Grid || currentViewMode == ViewMode::Icons) {
      const int cols = currentViewMode == ViewMode::Icons ? ICON_COLS : GRID_COLS;
      const int visibleRows = getVisibleRows();
      const int totalRows = (n + cols - 1) / cols;
      const int maxScroll = std::max(0, totalRows - visibleRows);
      scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
    } else if (currentViewMode == ViewMode::List) {
      const int maxScroll = std::max(0, n - LIST_VISIBLE_ITEMS);
      scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
    }
  }
  simpleUiFavScroll_ = 0;
  freeRecentPageBuffer();
  updateRequired = true;
}

void RecentActivity::renderRemoveConfirmation() {
  renderer.clearScreen();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int centerY = screenH / 2;

  std::string title = "this book";
  if (removeConfirmIndex_ >= 0 && removeConfirmIndex_ < static_cast<int>(recentBooks.size())) {
    title = bookDisplayTitle(recentBooks[static_cast<size_t>(removeConfirmIndex_)]);
  }
  title = renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, title.c_str(), screenW - 64);

  renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, centerY - 92, "RECENT BOOK", true, EpdFontFamily::BOLD);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_14_FONT_ID, centerY - 54, "Remove from recent?", true,
                         EpdFontFamily::BOLD);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY - 10, title.c_str(), true, EpdFontFamily::REGULAR);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, centerY + 26, "The book file and reading progress will stay.",
                         true, EpdFontFamily::REGULAR);
  const auto labels = mappedInput.mapLabels("Cancel", "Remove", "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  updateRequired = false;
}

const RecentActivity::CachedRecentStats& RecentActivity::statsForRecentIndex(const int index) const {
  static const CachedRecentStats empty;
  if (index < 0 || index >= static_cast<int>(recentStats_.size()) || index >= static_cast<int>(recentBooks.size())) {
    return empty;
  }
  CachedRecentStats& cached = recentStats_[static_cast<size_t>(index)];
  if (!cached.attempted) {
    cached.attempted = true;
    const RecentBook& book = recentBooks[static_cast<size_t>(index)];
    std::string cachePath = book.cachePath;
    if (cachePath.empty()) {
      cachePath = epubCachePathForBookPath(book.path);
    }
    cached.loaded = !cachePath.empty() && loadBookStats(cachePath.c_str(), cached.stats);
  }
  return cached;
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
 * Initializes the recent activity when entered.
 */
void RecentActivity::onEnter() {
  Activity::onEnter();

  freeRecentPageBuffer();
  layoutEngine_.reset();
  layoutEngineBoundMode_ = ViewMode::Flow;
  halfRefreshOnLoadApplied_ = false;
  removeConfirmOpen_ = false;
  removeConfirmIndex_ = -1;
  ignoreBackReleaseOnEnter_ = mappedInput.isPressed(MappedInputManager::Button::Back) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Back);
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

  pendingInitialLoadingFrame_ = false;
  updateRequired = true;
}

/**
 * Cleans up resources when exiting the recent activity.
 */
void RecentActivity::onExit() {
  freeRecentPageBuffer();
  resetRecentImageCacheJobs();
  removeConfirmOpen_ = false;
  removeConfirmIndex_ = -1;
  delete homeMenuDrawer_;
  homeMenuDrawer_ = nullptr;
  layoutEngine_.reset();
  layoutEngineBoundMode_ = ViewMode::Flow;
  std::vector<RecentBook>().swap(recentBooks);
  std::vector<CachedRecentStats>().swap(recentStats_);
  std::unordered_map<std::string, std::string>().swap(thumbnailPathCache_);
  std::unordered_map<std::string, std::string>().swap(coverPathCache_);
  std::vector<BookState::Book>().swap(simpleUiFavorites_);
  renderer.resetTransientReaderState();
  Activity::onExit();
}

void RecentActivity::renderInitialLoadingFrame() {
  renderer.clearScreen();
  renderTabBar(renderer);

  const int top = mainContentTop();
  const int bottom = INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) : renderer.getScreenHeight() - 42;
  const int centerY = top + std::max(1, bottom - top) / 2 - 12;
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Loading recents");

  renderer.displayBuffer();
}

void RecentActivity::renderSdCardUnavailableMessage() {
  const int top = mainContentTop();
  const int bottom = INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) : renderer.getScreenHeight() - 42;
  const int lineHeight = renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID);
  const int centerY = top + std::max(1, bottom - top) / 2;

  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY - lineHeight, "SD card not available", true,
                         EpdFontFamily::BOLD);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, centerY + 8, "Storage features are disabled");
}

void RecentActivity::openHomeMenuDrawer() {
  freeRecentPageBuffer();
  if (!homeMenuDrawer_) {
    homeMenuDrawer_ = new HomeMenuDrawer(*this);
  }
  homeMenuDrawer_->show();
}

void RecentActivity::closeHomeMenuDrawer() {
  if (homeMenuDrawer_) {
    homeMenuDrawer_->hide();
  }
}

#include "components/recent/Cover.ipp"
#include "components/recent/Flow.ipp"
#include "components/recent/Grid.ipp"
#include "components/recent/List.ipp"
#include "components/recent/SimpleUi.ipp"

void RecentActivity::drawBufferedSelectionOverlay() {
  const int totalBooks = static_cast<int>(recentBooks.size());
  if (selectorIndex < 0 || selectorIndex >= totalBooks) {
    return;
  }

  if (currentViewMode == ViewMode::Grid) {
    const int selectedRow = selectorIndex / GRID_COLS;
    const int visibleRows = getVisibleRows();
    if (selectedRow < scrollOffset || selectedRow >= scrollOffset + visibleRows) {
      return;
    }
    renderGridItem(selectorIndex % GRID_COLS, selectedRow - scrollOffset, recentGridPaintStartY(),
                   recentBooks[static_cast<size_t>(selectorIndex)], true);
    return;
  }

  if (currentViewMode == ViewMode::List) {
    if (selectorIndex < scrollOffset || selectorIndex >= scrollOffset + LIST_VISIBLE_ITEMS) {
      return;
    }
    constexpr int kHintReserve = 54;
    constexpr int padX = 30;
    const int screenW = renderer.getScreenWidth();
    const int contentBottom = renderer.getScreenHeight() - kHintReserve;
    const int startY = recentListPaintStartY();
    const int contentH = std::max(1, contentBottom - startY);
    const int rowH = std::max(56, contentH / LIST_VISIBLE_ITEMS);
    const int slot = selectorIndex - scrollOffset;
    const int y = startY + slot * rowH;
    renderer.rectangle.render(padX / 2, y + 1, screenW - padX, rowH, true, false);
    return;
  }

  if (currentViewMode != ViewMode::Icons) {
    return;
  }

  constexpr int kCols = ICON_COLS;
  constexpr int kRowsVisible = ICON_ROWS;
  constexpr int kGap = 8;
  constexpr int kMarginX = 10;
  constexpr int kMarginY = 8;
  const int selectedRow = selectorIndex / kCols;
  if (selectedRow < scrollOffset || selectedRow >= scrollOffset + kRowsVisible) {
    return;
  }

  const int startY = recentIconsPaintStartY();
  const int screenW = renderer.getScreenWidth();
  const int contentBottom =
      INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) : renderer.getScreenHeight() - 54;
  const int availW = std::max(1, screenW - kMarginX * 2);
  const int availH = std::max(1, contentBottom - startY - kMarginY * 2);
  const int frameW = std::max(40, (availW - (kCols - 1) * kGap) / kCols);
  const int frameH = std::max(40, (availH - (kRowsVisible - 1) * kGap) / kRowsVisible);
  const int blockW = kCols * frameW + (kCols - 1) * kGap;
  const int blockH = kRowsVisible * frameH + (kRowsVisible - 1) * kGap;
  const int row0X = kMarginX + std::max(0, (availW - blockW) / 2);
  const int blockTop = startY + kMarginY + std::max(0, (availH - blockH) / 2);
  const int col = selectorIndex % kCols;
  const int visualRow = selectedRow - scrollOffset;
  const int boxX = row0X + col * (frameW + kGap);
  const int boxY = blockTop + visualRow * (frameH + kGap);
  constexpr int kInnerPad = 4;
  const int innerX = boxX + kInnerPad;
  const int innerY = boxY + kInnerPad;
  const int innerW = std::max(8, frameW - kInnerPad * 2);
  const int innerH = std::max(8, frameH - kInnerPad * 2);
  const IconRect coverRect = inflateIconRect(fitBookCoverRect(innerX, innerY, innerW, innerH), 5);
  renderThickIconRect(renderer, coverRect, SETTINGS.bitmapRoundedCorners != 0, 3);
}

/**
 * Renders a single grid item including cover image or placeholder.
 */
void RecentActivity::renderGridItem(int gridX, int gridY, int startY, const RecentBook& book, bool selected) {
  const int screenW = renderer.getScreenWidth();
  const int contentBottom =
      INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) : renderer.getScreenHeight() - 54;
  constexpr int kGridSpacing = 10;

  int availableWidth = screenW - (GRID_COLS + 1) * kGridSpacing;
  int containerWidth = availableWidth / GRID_COLS;

  int visibleRows = getVisibleRows();
  int availableHeight = contentBottom - startY - (kGridSpacing * 2);
  int containerHeight = std::max(1, (availableHeight / visibleRows) - kGridSpacing);

  int itemX = kGridSpacing + gridX * (containerWidth + kGridSpacing);
  int itemY = startY + kGridSpacing + gridY * (containerHeight + kGridSpacing);

  constexpr int kGridThumbnailScalePercent = 95;
  const int drawW = std::max(1, containerWidth * kGridThumbnailScalePercent / 100);
  const int drawH = std::max(1, static_cast<int>(containerHeight) * kGridThumbnailScalePercent / 100);
  const int drawX = itemX + (containerWidth - drawW) / 2;
  const int drawY = itemY + (containerHeight - drawH) / 2;

  if (selected) {
    const int overlayX = std::max(0, drawX - 8);
    const int overlayY = std::max(startY, drawY - 8);
    const int overlayW = std::min(screenW - overlayX, drawW + 16);
    const int overlayH = std::min(contentBottom - overlayY, drawH + 16);
    for (int y = overlayY; y < overlayY + overlayH; y += 2) {
      for (int x = overlayX; x < overlayX + overlayW; x += 2) {
        renderer.drawPixel(x, y, true);
      }
    }
  }
  drawRecentThumbnailAt(drawX, drawY, drawW, drawH, book.cachePath, bookDisplayTitle(book),
                        ATKINSON_HYPERLEGIBLE_10_FONT_ID, selected);

  if (book.progress >= 0.0f && book.progress <= 1.0f) {
    int barX = drawX + 15;
    int barY = drawY + drawH - kGridSpacing;
    int barW = drawW - 30;
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
      renderer.rectangle.fill(barX + barW - pW - 5,
                              barY - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID) - 10, pW + 5, 30,
                              false, true);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, barX + barW - pW,
                           barY - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID) - 6, pText);
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
    case ViewMode::Grid:
      return std::unique_ptr<LayoutEngine>(new GridViewLayout());
    case ViewMode::Icons:
      return std::unique_ptr<LayoutEngine>(new IconsViewLayout());
    case ViewMode::Cover:
      return std::unique_ptr<LayoutEngine>(new CoverViewLayout());
    case ViewMode::SimpleUi:
      return std::unique_ptr<LayoutEngine>(new SimpleUiViewLayout());
    case ViewMode::List:
      return std::unique_ptr<LayoutEngine>(new ListViewLayout());
    case ViewMode::Flow:
    default:
      return std::unique_ptr<LayoutEngine>(new FlowViewLayout());
  }
}

void RecentActivity::GridViewLayout::paint(RecentActivity& self) {
  recent::Grid::render(self, self.recentGridPaintStartY());
}

void RecentActivity::IconsViewLayout::paint(RecentActivity& self) {
  recent::Grid3x3::render(self, self.recentIconsPaintStartY());
}

void RecentActivity::CoverViewLayout::paint(RecentActivity& self) { recent::Cover::render(self); }

void RecentActivity::SimpleUiViewLayout::paint(RecentActivity& self) { recent::SimpleUi::render(self); }

void RecentActivity::ListViewLayout::paint(RecentActivity& self) {
  recent::List::render(self, self.recentListPaintStartY());
}

void RecentActivity::FlowViewLayout::paint(RecentActivity& self) { recent::Flow::render(self); }

bool RecentActivity::canUseRecentPageBuffer() const {
  return !recentBooks.empty() &&
         (currentViewMode == ViewMode::Grid || currentViewMode == ViewMode::Icons || currentViewMode == ViewMode::List);
}

bool RecentActivity::storeRecentPageBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  freeRecentPageBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  recentPageBuffer_ = static_cast<uint8_t*>(malloc(bufferSize));
  if (!recentPageBuffer_) {
    return false;
  }

  memcpy(recentPageBuffer_, frameBuffer, bufferSize);
  recentPageBufferStored_ = true;
  recentPageBufferMode_ = currentViewMode;
  recentPageBufferScrollOffset_ = scrollOffset;
  recentPageBufferBookCount_ = static_cast<int>(recentBooks.size());
  return true;
}

bool RecentActivity::restoreRecentPageBuffer() {
  if (!recentPageBufferStored_ || !recentPageBuffer_ || recentPageBufferMode_ != currentViewMode ||
      recentPageBufferScrollOffset_ != scrollOffset ||
      recentPageBufferBookCount_ != static_cast<int>(recentBooks.size())) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  memcpy(frameBuffer, recentPageBuffer_, renderer.getBufferSize());
  return true;
}

void RecentActivity::freeRecentPageBuffer() {
  if (recentPageBuffer_) {
    free(recentPageBuffer_);
    recentPageBuffer_ = nullptr;
  }
  recentPageBufferStored_ = false;
  recentPageBufferMode_ = ViewMode::Flow;
  recentPageBufferScrollOffset_ = -1;
  recentPageBufferBookCount_ = -1;
}

void RecentActivity::resetRecentImageCacheJobs() { recentImageCacheJobPending_ = false; }

bool RecentActivity::queueRecentImageCacheBuild(const std::string& path, int x, int y, int w, int h, bool cropToFill,
                                                BitmapRender::RoundedOutside roundedOutside) {
  if (path.empty() || w <= 0 || h <= 0) {
    return false;
  }
  if (recentImageCacheJobPending_) {
    return true;
  }

  RecentImageCacheJob& job = recentImageCacheJob_;
  job.path = path;
  job.x = x;
  job.y = y;
  job.w = w;
  job.h = h;
  job.cropToFill = cropToFill;
  job.roundedOutside = roundedOutside;
  recentImageCacheJobPending_ = true;
  return true;
}

bool RecentActivity::processNextRecentImageCacheJob() {
  if (!recentImageCacheJobPending_) {
    return false;
  }

  const RecentImageCacheJob job = recentImageCacheJob_;
  recentImageCacheJobPending_ = false;

  ImageRender::Options options;
  options.cropToFill = job.cropToFill;
  options.useDisplayCache = true;
  options.roundedOutside = job.roundedOutside;
  if (!ImageRender::create(renderer, job.path).render(job.x, job.y, job.w, job.h, options)) {
    return false;
  }
  freeRecentPageBuffer();
  updateRequired = true;
  return true;
}

void RecentActivity::pumpDisplayFromLoop() {
  if (!updateRequired) {
    return;
  }
  if (pendingInitialLoadingFrame_) {
    pendingInitialLoadingFrame_ = false;
    renderInitialLoadingFrame();
    updateRequired = true;
    return;
  }
  const bool canUseBuffer = canUseRecentPageBuffer();
  if (canUseBuffer && restoreRecentPageBuffer()) {
    drawBufferedSelectionOverlay();
    renderer.displayBuffer();
    if (!halfRefreshOnLoadApplied_) {
      halfRefreshOnLoadApplied_ = true;
      SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Recent);
    }
    updateRequired = false;
    return;
  }

  renderer.clearScreen();
  renderTabBar(renderer);
  resetRecentImageCacheJobs();

  if (sdCardAvailable) {
    syncLayoutEngineForViewMode();
    suppressBufferedSelection_ = canUseBuffer;
    layoutEngine_->paint(*this);
    suppressBufferedSelection_ = false;
  } else {
    renderSdCardUnavailableMessage();
  }

  const auto labels = mappedInput.mapLabels("Menu", "Open", "", "");
  renderButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (canUseBuffer) {
    storeRecentPageBuffer();
    drawBufferedSelectionOverlay();
  }

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
 * Main loop for handling user input and updating state.
 */
void RecentActivity::loop() {
  if (homeMenuDrawer_ && homeMenuDrawer_->visible()) {
    homeMenuDrawer_->handleInput(mappedInput);
    return;
  }

  if (updateRequired) {
    if (removeConfirmOpen_) {
      renderRemoveConfirmation();
    } else {
      pumpDisplayFromLoop();
    }
  } else if (processNextRecentImageCacheJob()) {
    return;
  }

  if (removeConfirmOpen_) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      cancelRemoveConfirmation();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      confirmRemoveRecent();
      return;
    }
    return;
  }

  const int totalBooks = static_cast<int>(recentBooks.size());
  const bool isSimpleUi = (currentViewMode == ViewMode::SimpleUi);
  const bool isListView = (currentViewMode == ViewMode::List);
  const bool isCoverView = (currentViewMode == ViewMode::Cover);
  const bool isIconsView = (currentViewMode == ViewMode::Icons);

  // Tab vs item nav buttons depend on the main-menu nav setting. In front mode these map to the same physical
  // keys as before (no behavior change); side mode swaps the axes (Up/Down = tabs, Left/Right = items).
  bool upPressed = mappedInput.wasPressed(itemPrevButton());
  bool downPressed = mappedInput.wasPressed(itemNextButton());
  bool leftPressed = mappedInput.wasPressed(tabPrevButton());
  bool rightPressed = mappedInput.wasPressed(tabNextButton());
  // Open on press instead of release so heavy cover redraws do not swallow the
  // confirm edge, especially in Flow and other image-heavy recent views.
  bool confirmPressed = mappedInput.wasReleased(MappedInputManager::Button::Confirm);

  if (ignoreBackReleaseOnEnter_) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      return;
    }
    ignoreBackReleaseOnEnter_ = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() >= GO_HOME_MS) {
      return;
    }
    openHomeMenuDrawer();
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

  if (!isSimpleUi && !isCoverView && totalBooks == 0) {
    return;
  }

  {
    const ViewMode expectedMode = viewModeForLibrarySetting(SETTINGS.recentLibraryMode);
    if (expectedMode != currentViewMode) {
      freeRecentPageBuffer();
      currentViewMode = expectedMode;
      scrollOffset = 0;
      selectorIndex = 0;
      simpleUiFavScroll_ = 0;
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
        const auto& book = recentBooks[0];
        openBookPath(book.path, book.title, book.author, true);
        return;
      }
      const int fi = selectorIndex - recentSlots;
      if (fi >= 0 && fi < favCount) {
        const auto& book = simpleUiFavorites_[static_cast<size_t>(fi)];
        openBookPath(book.path, book.title, book.author, false);
        return;
      }
    }
    return;
  }

  if (isCoverView) {
    scrollOffset = 0;
    if (totalBooks == 0) {
      return;
    }
    if (selectorIndex < 0) {
      selectorIndex = 0;
    }
    if (selectorIndex >= totalBooks) {
      selectorIndex = totalBooks - 1;
    }
    if (downPressed) {
      selectorIndex = (selectorIndex + 1) % totalBooks;
      updateRequired = true;
      return;
    }
    if (upPressed) {
      selectorIndex = (selectorIndex + totalBooks - 1) % totalBooks;
      updateRequired = true;
      return;
    }
    if (confirmPressed) {
      const auto& book = recentBooks[static_cast<size_t>(selectorIndex)];
      openBookPath(book.path, book.title, book.author, true);
      return;
    }
    return;
  }

  if (isListView) {
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
      const auto& book = recentBooks[selectorIndex];
      openBookPath(book.path, book.title, book.author, true);
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
      const int cols = isIconsView ? ICON_COLS : GRID_COLS;
      int currentRow = selectorIndex / cols;
      int visibleRows = getVisibleRows();
      if (currentRow < scrollOffset) {
        scrollOffset = currentRow;
      } else if (currentRow >= scrollOffset + visibleRows) {
        scrollOffset = currentRow - visibleRows + 1;
      }
      int totalRows = (totalBooks + cols - 1) / cols;
      int maxOffset = std::max(0, totalRows - visibleRows);
      scrollOffset = std::max(0, std::min(scrollOffset, maxOffset));
      updateRequired = true;
    }

    if (confirmPressed && selectorIndex >= 0 && selectorIndex < totalBooks) {
      const auto& book = recentBooks[selectorIndex];
      openBookPath(book.path, book.title, book.author, true);
      return;
    }
  }
}
