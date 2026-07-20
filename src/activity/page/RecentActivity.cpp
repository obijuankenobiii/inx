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
#include "images/Star.h"
#include "state/BookSetting.h"
#include "state/BookState.h"
#include "state/Statistics.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
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
constexpr int kHomeDrawerRowH = 60;
constexpr int kHomeDrawerMainRowH = 54;
constexpr int kHomeDrawerHeaderH = UiTheme::MAIN_TAB_BAR_HEIGHT;
constexpr int kHomeDrawerPadX = 20;
constexpr int kHomeDrawerMainBottomPad = 100;

enum class HomeDrawerMode { Main, Recents, Bookmarks, Annotations, BookmarkDetail, AnnotationDetail };

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

bool isHomeDrawerLandscape(const GfxRenderer& gfx) {
  const auto o = gfx.getOrientation();
  return o == GfxRenderer::LandscapeClockwise || o == GfxRenderer::LandscapeCounterClockwise;
}

std::string cachePathForRecentBook(const RecentBook& book) {
  return book.cachePath.empty() ? epubCachePathForBookPath(book.path) : book.cachePath;
}

std::string titleForCachePath(const std::string& cachePath) {
  const auto& books = RECENT_BOOKS.getBooks();
  for (const auto& book : books) {
    if (cachePathForRecentBook(book) == cachePath) {
      return bookDisplayTitle(book);
    }
  }
  const size_t slash = cachePath.find_last_of('/');
  return slash == std::string::npos ? cachePath : cachePath.substr(slash + 1);
}

std::vector<std::string> epubCacheDirs() {
  std::vector<std::string> out;
  FsFile root = SdMan.open("/.metadata/epub");
  if (!root || !root.isDirectory()) {
    if (root) {
      root.close();
    }
    return out;
  }
  char name[96];
  for (FsFile f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) {
      f.getName(name, sizeof(name));
      out.push_back(std::string("/.metadata/epub/") + name);
    }
    f.close();
  }
  root.close();
  return out;
}

std::string trimDrawerText(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}
}  // namespace

class RecentActivity::HomeMenuDrawer {
 public:
  explicit HomeMenuDrawer(RecentActivity& owner) : owner_(owner), renderer_(owner.renderer) { syncLayout(); }

  bool visible() const { return visible_; }

  void show() {
    visible_ = true;
    mode_ = HomeDrawerMode::Main;
    selected_ = 0;
    scroll_ = 0;
    detailText_.clear();
    syncLayout();
    render();
  }

  void hide() {
    visible_ = false;
    owner_.updateRequired = true;
  }

  void handleInput(MappedInputManager& input) {
    if (!visible_) {
      return;
    }
    if (input.wasReleased(MappedInputManager::Button::Back)) {
      hide();
      return;
    }

    if (mode_ == HomeDrawerMode::BookmarkDetail || mode_ == HomeDrawerMode::AnnotationDetail) {
      return;
    }

    const int count = itemCount();
    if (count == 0) {
      return;
    }

    bool changed = false;
    if (input.wasPressed(MappedInputManager::Button::Up)) {
      selected_ = (selected_ + count - 1) % count;
      changed = true;
    } else if (input.wasPressed(MappedInputManager::Button::Down)) {
      selected_ = (selected_ + 1) % count;
      changed = true;
    }

    if (changed) {
      clampScroll();
      render();
      return;
    }

    if (input.wasReleased(MappedInputManager::Button::Confirm)) {
      activateSelected();
      return;
    }

    if ((mode_ == HomeDrawerMode::Recents) &&
        (input.wasReleased(MappedInputManager::Button::Right) || input.wasReleased(MappedInputManager::Button::Left))) {
      deleteSelectedRecent();
    }
  }

  void render() {
    if (!visible_) {
      return;
    }
    syncLayout();
    if (mode_ == HomeDrawerMode::BookmarkDetail) {
      renderer_.clearScreen();
      renderBookmarkPreview();
      renderer_.displayBuffer(HalDisplay::FAST_REFRESH);
      return;
    }

    renderer_.rectangle.fill(drawerX_, drawerY_, drawerW_, drawerH_, false);
    if (mode_ == HomeDrawerMode::Main) {
      renderer_.rectangle.render(drawerX_, drawerY_, drawerW_, drawerH_, true);
    }

    const int titleY =
        drawerY_ + (kHomeDrawerHeaderH - renderer_.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
    renderer_.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, drawerX_ + kHomeDrawerPadX, titleY, title(), true,
                          EpdFontFamily::BOLD);
    renderer_.line.render(drawerX_, drawerY_ + kHomeDrawerHeaderH, drawerX_ + drawerW_, drawerY_ + kHomeDrawerHeaderH,
                          true);

    if (mode_ == HomeDrawerMode::AnnotationDetail) {
      renderDetail();
    } else {
      renderRows();
    }

    drawHints();
    renderer_.displayBuffer(HalDisplay::FAST_REFRESH);
  }

 private:
  struct DrawerRow {
    std::string label;
    std::string sublabel;
    std::string cachePath;
    int recentIndex = -1;
    int spine = -1;
    int page = -1;
  };

  RecentActivity& owner_;
  GfxRenderer& renderer_;
  bool visible_ = false;
  HomeDrawerMode mode_ = HomeDrawerMode::Main;
  int selected_ = 0;
  int scroll_ = 0;
  int drawerX_ = 0;
  int drawerY_ = 0;
  int drawerW_ = 0;
  int drawerH_ = 0;
  int rowsPerPage_ = 1;
  std::vector<DrawerRow> rows_;
  std::string detailText_;

  void syncLayout() {
    const int sw = renderer_.getScreenWidth();
    const int sh = renderer_.getScreenHeight();
    if (mode_ != HomeDrawerMode::Main) {
      drawerX_ = 0;
      drawerY_ = 0;
      drawerW_ = sw;
      drawerH_ = sh;
    } else if (isHomeDrawerLandscape(renderer_)) {
      drawerW_ = sw / 2;
      drawerX_ = sw - drawerW_;
      drawerH_ = kHomeDrawerHeaderH + 3 * kHomeDrawerMainRowH + kHomeDrawerMainBottomPad;
      drawerY_ = sh - drawerH_;
    } else {
      drawerX_ = 0;
      drawerW_ = sw;
      drawerH_ = kHomeDrawerHeaderH + 3 * kHomeDrawerMainRowH + kHomeDrawerMainBottomPad;
      drawerY_ = sh - drawerH_;
    }
    const int rowH = mode_ == HomeDrawerMode::Main ? kHomeDrawerMainRowH : kHomeDrawerRowH;
    rowsPerPage_ = mode_ == HomeDrawerMode::Main ? 3 : std::max(1, (drawerH_ - kHomeDrawerHeaderH - 12 - 46) / rowH);
  }

  const char* title() const {
    switch (mode_) {
      case HomeDrawerMode::Recents:
        return "Manage Recents";
      case HomeDrawerMode::Bookmarks:
        return "Bookmarks";
      case HomeDrawerMode::Annotations:
        return "Annotations";
      case HomeDrawerMode::BookmarkDetail:
        return "Bookmark";
      case HomeDrawerMode::AnnotationDetail:
        return "Annotation";
      case HomeDrawerMode::Main:
      default:
        return "Menu";
    }
  }

  int itemCount() const {
    if (mode_ == HomeDrawerMode::Main) {
      return 3;
    }
    return static_cast<int>(rows_.size());
  }

  std::string mainLabel(const int index) const {
    switch (index) {
      case 0:
        return "Manage Recents";
      case 1:
        return "Annotations";
      case 2:
      default:
        return "Bookmarks";
    }
  }

  void clampScroll() {
    const int count = itemCount();
    if (selected_ < 0) {
      selected_ = 0;
    }
    if (selected_ >= count) {
      selected_ = std::max(0, count - 1);
    }
    if (selected_ < scroll_) {
      scroll_ = selected_;
    } else if (selected_ >= scroll_ + rowsPerPage_) {
      scroll_ = selected_ - rowsPerPage_ + 1;
    }
    scroll_ = std::max(0, scroll_);
  }

  void renderRows() {
    const int count = itemCount();
    if (count == 0) {
      const char* empty = mode_ == HomeDrawerMode::Recents       ? "No recent books"
                          : mode_ == HomeDrawerMode::Bookmarks   ? "No bookmarks"
                          : mode_ == HomeDrawerMode::Annotations ? "No annotations"
                                                                 : "";
      const int msgY = drawerY_ + kHomeDrawerHeaderH + 42;
      renderer_.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, msgY, empty, true);
      return;
    }

    const int rowH = mode_ == HomeDrawerMode::Main ? kHomeDrawerMainRowH : kHomeDrawerRowH;
    for (int row = 0; row < rowsPerPage_; ++row) {
      const int itemIndex = scroll_ + row;
      if (itemIndex >= count) {
        break;
      }
      const int itemY = drawerY_ + kHomeDrawerHeaderH + row * rowH + 1;
      const bool selected = itemIndex == selected_;
      renderer_.rectangle.fill(
          drawerX_, itemY, drawerW_, rowH,
          selected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));

      const std::string label = mode_ == HomeDrawerMode::Main ? mainLabel(itemIndex) : rows_[itemIndex].label;
      const int textX = drawerX_ + kHomeDrawerPadX;
      const int textY = itemY + (rowH - renderer_.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
      const int maxW = drawerW_ - kHomeDrawerPadX * 2 - 18;
      const std::string clipped = renderer_.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, label.c_str(), maxW);
      renderer_.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, clipped.c_str(), selected ? 0 : 1,
                            EpdFontFamily::REGULAR);

      renderer_.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, drawerX_ + drawerW_ - 30, textY, "›", selected ? 0 : 1);
      renderer_.line.render(drawerX_, itemY + rowH - 1, drawerX_ + drawerW_, itemY + rowH - 1, true,
                            LineRender::Style::Dotted);
    }
  }

  void renderDetail() {
    if (mode_ == HomeDrawerMode::BookmarkDetail) {
      renderBookmarkPreview();
      return;
    }

    int y = drawerY_ + kHomeDrawerHeaderH + 18;
    const int textX = drawerX_ + kHomeDrawerPadX;
    const int maxW = drawerW_ - kHomeDrawerPadX * 2;
    std::string remaining = detailText_;
    const int lineH = renderer_.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 4;
    while (!remaining.empty() && y + lineH < drawerY_ + drawerH_ - 50) {
      size_t take = remaining.size();
      while (take > 0 &&
             renderer_.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, remaining.substr(0, take).c_str()) > maxW) {
        const size_t space = remaining.rfind(' ', take - 1);
        take = (space == std::string::npos || space == 0) ? take - 1 : space;
      }
      if (take == 0) {
        break;
      }
      std::string line = trimDrawerText(remaining.substr(0, take));
      renderer_.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, y, line.c_str(), true);
      remaining.erase(0, take);
      remaining = trimDrawerText(remaining);
      y += lineH;
    }
  }

  void renderBookmarkPreview() {
    if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) {
      renderPreviewUnavailable();
      return;
    }

    const DrawerRow& row = rows_[selected_];
    int fontId = ATKINSON_HYPERLEGIBLE_14_FONT_ID;
    Section previewSection(row.cachePath, row.spine, renderer_);
    if (!previewSection.loadSectionFileForPreview(&fontId)) {
      renderPreviewUnavailable();
      return;
    }
    previewSection.currentPage = row.page;
    if (!FontManager::ensureReaderLayoutFonts(fontId, renderer_)) {
      renderPreviewUnavailable();
      return;
    }
    std::unique_ptr<Page> page = previewSection.loadPageFromSectionFile();
    if (!page) {
      renderPreviewUnavailable();
      return;
    }

    const BookSettings previewSettings = loadPreviewBookSettings(row.cachePath);
    const int pageX = previewMarginLeft(previewSettings);
    const int pageY = previewMarginTop(previewSettings, fontId);
    page->render(renderer_, fontId, FontManager::getNextFont(fontId), pageX, pageY, false, ImageRenderMode::OneBit);
  }

  BookSettings loadPreviewBookSettings(const std::string& cachePath) const {
    BookSettings settings;
    settings.loadFromFile(cachePath);
    return settings;
  }

  int previewMarginLeft(const BookSettings& settings) const {
    int oT = 0;
    int oR = 0;
    int oB = 0;
    int oL = 0;
    renderer_.getOrientedViewableTRBL(&oT, &oR, &oB, &oL);
    (void)oT;
    (void)oR;
    (void)oB;
    return oL + settings.screenMargin;
  }

  int previewMarginTop(const BookSettings& settings, const int fontId) const {
    int oT = 0;
    int oR = 0;
    int oB = 0;
    int oL = 0;
    renderer_.getOrientedViewableTRBL(&oT, &oR, &oB, &oL);
    (void)oR;
    (void)oB;
    (void)oL;

    int top = oT + settings.screenMargin;
    top -= renderer_.text.getGlyphTopInset(fontId, 'H', EpdFontFamily::REGULAR);
    return std::max(oT, top);
  }

  void renderPreviewUnavailable() {
    const char* line1 = "Page preview unavailable";
    const char* line2 = "Open the book once to rebuild cache.";
    const int msgY = drawerY_ + 44;
    const int w1 = renderer_.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, line1);
    const int w2 = renderer_.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, line2);
    renderer_.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, drawerX_ + (drawerW_ - w1) / 2, msgY, line1, true);
    renderer_.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, drawerX_ + (drawerW_ - w2) / 2,
                          msgY + renderer_.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 8, line2, true);
  }

  void drawHints() {
    const auto labels =
        owner_.mappedInput.mapLabels("Back", "Select", "Up", mode_ == HomeDrawerMode::Recents ? "Del" : "");
    owner_.renderButtonHints(renderer_, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  void activateSelected() {
    if (mode_ == HomeDrawerMode::Main) {
      if (selected_ == 0) {
        loadRecents();
        mode_ = HomeDrawerMode::Recents;
      } else if (selected_ == 1) {
        loadAnnotations();
        mode_ = HomeDrawerMode::Annotations;
      } else {
        loadBookmarks();
        mode_ = HomeDrawerMode::Bookmarks;
      }
      selected_ = 0;
      scroll_ = 0;
      render();
      return;
    }

    if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) {
      return;
    }
    if (mode_ == HomeDrawerMode::Recents) {
      deleteSelectedRecent();
      return;
    }
    if (mode_ == HomeDrawerMode::Bookmarks) {
      const DrawerRow& row = rows_[selected_];
      mode_ = HomeDrawerMode::BookmarkDetail;
      detailText_ = row.label;
      render();
      return;
    }
    if (mode_ == HomeDrawerMode::Annotations) {
      detailText_ = rows_[selected_].sublabel.empty() ? rows_[selected_].label : rows_[selected_].sublabel;
      mode_ = HomeDrawerMode::AnnotationDetail;
      render();
    }
  }

  void loadRecents() {
    rows_.clear();
    const auto& books = RECENT_BOOKS.getBooks();
    for (size_t i = 0; i < books.size(); ++i) {
      DrawerRow row;
      row.label = bookDisplayTitle(books[i]);
      row.recentIndex = static_cast<int>(i);
      rows_.push_back(std::move(row));
    }
  }

  void deleteSelectedRecent() {
    if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) {
      return;
    }
    const int recentIndex = rows_[selected_].recentIndex;
    const auto& books = RECENT_BOOKS.getBooks();
    if (recentIndex >= 0 && recentIndex < static_cast<int>(books.size())) {
      RECENT_BOOKS.removeBook(books[static_cast<size_t>(recentIndex)].path);
      owner_.loadRecentBooks(false);
    }
    loadRecents();
    if (selected_ >= static_cast<int>(rows_.size())) {
      selected_ = std::max(0, static_cast<int>(rows_.size()) - 1);
    }
    clampScroll();
    render();
  }

  void loadBookmarks() {
    rows_.clear();
    const std::vector<std::string> caches = epubCacheDirs();
    for (const std::string& cachePath : caches) {
      const std::string path = cachePath + "/bookmarks.bin";
      FsFile f;
      if (!SdMan.openFileForRead("HMB", path, f)) {
        continue;
      }
      const uint32_t fileSize = f.fileSize();
      const int count = fileSize / sizeof(EpubActivity::Bookmark);
      const std::string bookTitle = titleForCachePath(cachePath);
      for (int i = 0; i < count && i < 64; ++i) {
        EpubActivity::Bookmark b{};
        if (f.read(&b, sizeof(b)) != sizeof(b)) {
          break;
        }
        if (!b.isValid()) {
          continue;
        }
        DrawerRow row;
        char pageLabel[24];
        std::snprintf(pageLabel, sizeof(pageLabel), " p%d", static_cast<int>(b.pageNumber) + 1);
        row.label = bookTitle + " - " + std::string(b.chapterTitle) + pageLabel;
        row.cachePath = cachePath;
        row.spine = b.spineIndex;
        row.page = b.pageNumber;
        rows_.push_back(std::move(row));
      }
      f.close();
    }
  }

  void loadAnnotations() {
    rows_.clear();
    const std::vector<std::string> caches = epubCacheDirs();
    for (const std::string& cachePath : caches) {
      const std::string annDir = cachePath + "/" + EpubAnnotations::kSubdir;
      if (!SdMan.exists(annDir.c_str())) {
        continue;
      }
      const std::vector<String> files = SdMan.listFiles(annDir.c_str());
      EpubAnnotations annotations;
      for (const String& file : files) {
        int spine = 0;
        int page = 0;
        if (std::sscanf(file.c_str(), "s_%d_p_%d.bin", &spine, &page) != 2) {
          continue;
        }
        annotations.ensurePageLoaded(cachePath, spine, page);
        for (const auto& rec : annotations.records()) {
          DrawerRow row;
          const std::string text = trimDrawerText(rec.text);
          row.label = text.empty() ? titleForCachePath(cachePath) : text;
          row.sublabel = text;
          row.cachePath = cachePath;
          row.spine = spine;
          row.page = page;
          rows_.push_back(std::move(row));
        }
      }
    }
  }
};

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

    renderer.rectangle.fill(x, y, w, h, false);
    if (ImageRender::create(renderer, imagePath).render(x, y, w, h, options)) {
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

    renderer.rectangle.fill(x, y, w, h, false, SETTINGS.bitmapRoundedCorners != 0);
    if (ImageRender::create(renderer, imagePath).render(x, y, w, h, options)) {
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

  updateRequired = true;
}

/**
 * Cleans up resources when exiting the recent activity.
 */
void RecentActivity::onExit() {
  freeRecentPageBuffer();
  removeConfirmOpen_ = false;
  removeConfirmIndex_ = -1;
  delete homeMenuDrawer_;
  homeMenuDrawer_ = nullptr;
  layoutEngine_.reset();
  layoutEngineBoundMode_ = ViewMode::Flow;
  recentBooks.clear();
  recentStats_.clear();
  thumbnailPathCache_.clear();
  coverPathCache_.clear();
  simpleUiFavorites_.clear();
  renderer.resetTransientReaderState();
  Activity::onExit();
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

/**
 * Renders the complete grid view including all visible books.
 */
void RecentActivity::renderGrid(int startY) {
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

      bool isSelected = !suppressBufferedSelection_ && (selectorIndex == bookIdx);
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

  constexpr int kCols = ICON_COLS;
  constexpr int kRowsVisible = ICON_ROWS;
  constexpr int kGap = 8;
  constexpr int kMarginX = 10;
  constexpr int kMarginY = 8;
  constexpr int kInnerPad = 4;
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
      const int boxX = row0X + col * (frameW + kGap);
      const int boxY = blockTop + visualRow * (frameH + kGap);
      const bool selected = !suppressBufferedSelection_ && (selectorIndex == bookIdx);

      const int innerX = boxX + kInnerPad;
      const int innerY = boxY + kInnerPad;
      const int innerW = std::max(8, frameW - kInnerPad * 2);
      const int innerH = std::max(8, frameH - kInnerPad * 2);
      const IconRect fittedCover = fitBookCoverRect(innerX, innerY, innerW, innerH);
      const RecentBook& b = recentBooks[static_cast<size_t>(bookIdx)];
      drawRecentCoverFitAt(fittedCover.x, fittedCover.y, fittedCover.w, fittedCover.h, b.cachePath, bookDisplayTitle(b),
                           ATKINSON_HYPERLEGIBLE_10_FONT_ID);
      drawProgressBadge(renderer, fittedCover, b.progress);
      const IconRect coverFrame = inflateIconRect(fittedCover, 5);
      if (selected) {
        renderThickIconRect(renderer, coverFrame, rr, 3);
      } else if (!rr) {
        renderer.rectangle.render(coverFrame.x, coverFrame.y, coverFrame.w, coverFrame.h, true, false);
      }
    }
  }
}

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
      renderer.rectangle.fill(barX + barW - pW - 5,
                              barY - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID) - 10, pW + 5, 30,
                              false, true);
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
    const bool selected = !suppressBufferedSelection_ && (selectorIndex == bi);

    if (selected) {
      renderer.rectangle.render(padX / 2, y + 1, screenW - padX, rowH, true, false);
    }

    const int ty = y + (rowH - thumbH) / 2;
    const int tx = padX;
    const std::string cacheDir = book.cachePath.empty() ? epubCachePathForBookPath(book.path) : book.cachePath;
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
    const std::string titleLine = renderer.text.truncate(fontTitle, dispTitle.c_str(), textW, EpdFontFamily::REGULAR);
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

void RecentActivity::GridViewLayout::paint(RecentActivity& self) { self.renderGrid(self.recentGridPaintStartY()); }

void RecentActivity::IconsViewLayout::paint(RecentActivity& self) { self.renderIcons(self.recentIconsPaintStartY()); }

void RecentActivity::CoverViewLayout::paint(RecentActivity& self) { self.renderCoverMode(); }

void RecentActivity::SimpleUiViewLayout::paint(RecentActivity& self) { self.renderSimpleUi(); }

void RecentActivity::ListViewLayout::paint(RecentActivity& self) { self.renderList(self.recentListPaintStartY()); }

void RecentActivity::FlowViewLayout::paint(RecentActivity& self) { self.renderFlow(); }

void RecentActivity::renderCoverMode() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int bodyTop = mainContentTop() + 6;
  const int bodyBottom = INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) : screenH - 36;
  const int bodyH = std::max(1, bodyBottom - bodyTop);

  if (recentBooks.empty()) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, bodyTop + bodyH / 2 - 8, "No recent books", true);
    return;
  }

  if (selectorIndex < 0) {
    selectorIndex = 0;
  }
  const int totalBooks = static_cast<int>(recentBooks.size());
  if (selectorIndex >= totalBooks) {
    selectorIndex = totalBooks - 1;
  }
  const RecentBook& b = recentBooks[static_cast<size_t>(selectorIndex)];

  constexpr int kProgressGap = 10;
  constexpr int kProgressH = 8;
  constexpr int kMinBottomPad = 12;
  const bool showProgress = b.progress >= 0.0f && b.progress <= 1.0f;
  const int progressBlockH = showProgress ? (kProgressGap + kProgressH) : 0;

  int coverW = std::max(80, screenW * 78 / 100);
  const int maxCoverW = std::max(80, screenW - GRID_SPACING * 2);
  coverW = std::min(coverW, maxCoverW);
  int coverH = coverW * COVER_HEIGHT / COVER_WIDTH;
  const int maxCoverH = std::max(90, bodyH - progressBlockH - kMinBottomPad);
  if (coverH > maxCoverH) {
    coverH = maxCoverH;
    coverW = std::max(80, coverH * COVER_WIDTH / COVER_HEIGHT);
  }

  const int coverX = (screenW - coverW) / 2;
  const int coverY = bodyTop + std::max(0, (bodyH - coverH - progressBlockH) / 2);
  const bool rr = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(coverX, coverY, coverW, coverH, false, rr);
  const std::string cdir = b.cachePath.empty() ? epubCachePathForBookPath(b.path) : b.cachePath;
  const std::string coverPath = resolveCoverPath(cdir);
  bool coverDrawn = false;
  if (!coverPath.empty()) {
    ImageRender::Options options;
    options.cropToFill = true;
    options.useDisplayCache = true;
    if (SETTINGS.bitmapRoundedCorners == 0) {
      options.roundedOutside = BitmapRender::RoundedOutside::None;
    } else if (SETTINGS.bitmapRoundedCorners == 2) {
      options.roundedOutside = BitmapRender::RoundedOutside::SubtlePaperOutside;
    } else {
      options.roundedOutside = BitmapRender::RoundedOutside::PaperOutside;
    }
    coverDrawn = ImageRender::create(renderer, coverPath).render(coverX, coverY, coverW, coverH, options);
  }
  if (!coverDrawn) {
    drawRecentNoCoverPlaceholder(renderer, coverX, coverY, coverW, coverH, bookDisplayTitle(b),
                                 ATKINSON_HYPERLEGIBLE_14_FONT_ID);
  }
  renderer.rectangle.render(coverX - 2, coverY - 2, coverW + 4, coverH + 4, true, rr);

  if (showProgress) {
    const int barY = std::min(coverY + coverH + kProgressGap, bodyBottom - kProgressH - kMinBottomPad);
    const int barW = std::max(24, coverW * 80 / 100);
    const int barX = coverX + (coverW - barW) / 2;
    renderer.rectangle.fill(barX, barY, barW, kProgressH, false);
    renderer.rectangle.render(barX, barY, barW, kProgressH, true);
    if (b.progress > 0.0f) {
      const int innerW = std::max(0, barW - 2);
      const int innerH = std::max(0, kProgressH - 2);
      const int fillW = static_cast<int>(static_cast<float>(innerW) * b.progress + 0.5f);
      if (fillW > 0) {
        renderer.rectangle.fill(barX + 1, barY + 1, fillW, innerH);
      }
    }
  }
}

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

void RecentActivity::pumpDisplayFromLoop() {
  if (!updateRequired) {
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

  syncLayoutEngineForViewMode();
  suppressBufferedSelection_ = canUseBuffer;
  layoutEngine_->paint(*this);
  suppressBufferedSelection_ = false;

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
  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
    updateRequired = true;
    return;
  }

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
void RecentActivity::renderFlow() {
  if (recentBooks.empty()) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, renderer.getScreenHeight() / 2, "No recent books");
    return;
  }

  const int screenW = renderer.getScreenWidth();
  const int startY = mainContentTop() + 5;

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

  float scale = 0.9f;
  int sideW = (int)(centerW * scale);
  int sideH = (int)(centerH * scale);
  int leftX = centerX - sideW - 20;
  int rightX = centerX + centerW + 20;
  int sideY = centerY + (centerH - sideH) / 2;

  if (currentIndex > 0) {
    const RecentBook& leftBook = recentBooks[currentIndex - 1];
    renderer.rectangle.fill(leftX, sideY, sideW, sideH, false, rr);
    drawRecentThumbnailAt(leftX, sideY, sideW, sideH, leftBook.cachePath, bookDisplayTitle(leftBook),
                          ATKINSON_HYPERLEGIBLE_10_FONT_ID, true);
  }

  if (currentIndex + 1 < totalBooks) {
    const RecentBook& rightBook = recentBooks[currentIndex + 1];
    renderer.rectangle.fill(rightX, sideY, sideW, sideH, false, rr);
    drawRecentThumbnailAt(rightX, sideY, sideW, sideH, rightBook.cachePath, bookDisplayTitle(rightBook),
                          ATKINSON_HYPERLEGIBLE_10_FONT_ID, true);
  }

  const RecentBook& currentBook = recentBooks[currentIndex];

  renderer.rectangle.fill(centerX, centerY, centerW, centerH, false, rr);
  drawRecentThumbnailAt(centerX, centerY, centerW, centerH, currentBook.cachePath, bookDisplayTitle(currentBook),
                        ATKINSON_HYPERLEGIBLE_14_FONT_ID, true);

  const CachedRecentStats& cachedStats = statsForRecentIndex(currentIndex);
  const BookReadingStats& stats = cachedStats.stats;
  const bool hasStats = cachedStats.loaded;

  const int VALUE_FONT = ATKINSON_HYPERLEGIBLE_16_FONT_ID;
  const int LABEL_FONT = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  int statsX = 30;
  int statsY = carouselY + carouselH + 25;
  renderer.line.render(0, carouselY + carouselH + 10, screenW, carouselY + carouselH + 10, true);
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
    drawRecentThumbnailAt(rx, ry, m.thumbW, m.thumbH, cdir, bookDisplayTitle(b), ATKINSON_HYPERLEGIBLE_12_FONT_ID,
                          true);
    if (sel) {
      renderer.rectangle.render(rx - 2, ry - 2, m.thumbW + 4, m.thumbH + 4, true, rr);
    } else if (!rr) {
      renderer.rectangle.render(rx, ry, m.thumbW, m.thumbH, true, false);
    }

    const int titleFont = kSimpleUiTitleFont;
    const int authorFont = kSimpleUiBodyFont;
    std::string titleStr = b.title.empty() ? formatTitle(getBaseFilename(b.path)) : b.title;
    const int textX = rx + m.thumbW + 18;
    const int maxTextW = std::max(40, screenW - textX - m.marginL);
    const std::string titleDraw = renderer.text.truncate(titleFont, titleStr.c_str(), maxTextW, EpdFontFamily::BOLD);
    const int lhTitle = renderer.text.getLineHeight(titleFont);
    const int lhAuthor = renderer.text.getLineHeight(authorFont);
    const int authorGap = 8;
    constexpr int kSimpleProgressBarH = 6;
    constexpr int kSimpleProgressBarGap = 12;
    const bool showProg = b.progress >= 0.0f && b.progress <= 1.0f;
    const int blockH = lhTitle + authorGap + lhAuthor + (showProg ? (kSimpleProgressBarGap + kSimpleProgressBarH) : 0);
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
    const int favHdrFont = kSimpleUiBodyFont;
    renderer.text.render(favHdrFont, m.marginL, m.favTop + kFavHeaderPadTop, "Favorites", true, EpdFontFamily::BOLD);
    const int hdrSepY = m.favListTop - 1;
    if (hdrSepY > m.favTop) {
      renderer.line.render(0, hdrSepY, screenW, hdrSepY, true);
    }
  }

  const int favFont = kSimpleUiBodyFont;
  const int lh = renderer.text.getLineHeight(favFont);
  constexpr int kPadY = 18;
  clampSimpleUiFavoriteScroll(m.maxVis);

  const int fc = static_cast<int>(simpleUiFavorites_.size());

  if (fc == 0) {
    const int subFont = kSimpleUiLabelFont;
    const char* line1 = "No favorites yet.";
    const char* line2 = "Long press Confirm in Library to favorite books.";
    const int w1 = renderer.text.getWidth(favFont, line1);
    const int w2 = renderer.text.getWidth(subFont, line2);
    const int lh2 = renderer.text.getLineHeight(subFont);
    const int block = lh + 12 + lh2;
    const int paneH = m.bodyBottom - m.favListTop;
    const int y0 = m.favListTop + std::max(4, (paneH - block) / 2);
    renderer.text.render(favFont, (screenW - w1) / 2, y0, line1, true, EpdFontFamily::BOLD);
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
    renderer.text.render(favFont, titleX, textY, trunc.c_str(), !rowSel);
    rowY += m.rowH;
    renderer.line.render(0, rowY, screenW, rowY, true);
  }
}
