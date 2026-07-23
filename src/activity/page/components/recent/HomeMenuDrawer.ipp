namespace {
constexpr int kHomeDrawerRowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT;
constexpr int kHomeDrawerMainRowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT;
constexpr int kHomeDrawerHeaderH = UiTheme::MAIN_TAB_BAR_HEIGHT;
constexpr int kHomeDrawerHeaderFont = ATKINSON_HYPERLEGIBLE_14_FONT_ID;
constexpr int kHomeDrawerPageHeaderExtraH = 14;
constexpr int kHomeDrawerPadX = 20;
constexpr int kHomeDrawerMainBottomPad = 70;
constexpr uint16_t kRecentDrawerIndexVersion = 1;
constexpr char kRecentDrawerIndexFile[] = "/.metadata/recent_drawer_index.bin";

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

const RecentBook* findRecentBookByPath(const std::string& path, int* index = nullptr) {
  const auto& books = RECENT_BOOKS.getBooks();
  for (size_t i = 0; i < books.size(); ++i) {
    if (books[i].path == path) {
      if (index) {
        *index = static_cast<int>(i);
      }
      return &books[i];
    }
  }
  return nullptr;
}

const RecentBook* findRecentBookByCachePath(const std::string& cachePath, int* index = nullptr) {
  const auto& books = RECENT_BOOKS.getBooks();
  for (size_t i = 0; i < books.size(); ++i) {
    if (cachePathForRecentBook(books[i]) == cachePath) {
      if (index) {
        *index = static_cast<int>(i);
      }
      return &books[i];
    }
  }
  return nullptr;
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
      if (mode_ == HomeDrawerMode::RecentsDeleteConfirm) {
        mode_ = HomeDrawerMode::Recents;
        render(HalDisplay::FAST_REFRESH);
        return;
      }
      if (mode_ == HomeDrawerMode::RecentsActions) {
        loadRecents();
        mode_ = HomeDrawerMode::Recents;
        selected_ = 0;
        scroll_ = 0;
        render(HalDisplay::FAST_REFRESH);
        return;
      }
      hide();
      return;
    }

    if (mode_ == HomeDrawerMode::BookmarkDetail || mode_ == HomeDrawerMode::AnnotationDetail) {
      return;
    }

    if (mode_ == HomeDrawerMode::RecentsDeleteConfirm) {
      if (input.wasReleased(MappedInputManager::Button::Confirm)) {
        confirmQuickDeleteRecent();
      }
      return;
    }

    if (mode_ == HomeDrawerMode::Recents && input.wasPressed(MappedInputManager::Button::Left)) {
      openQuickDeleteRecentConfirm();
      return;
    }

    const int count = itemCount();
    if (count == 0) {
      return;
    }

    bool changed = false;
    const int oldSelected = selected_;
    const int oldScroll = scroll_;
    if (input.wasPressed(MappedInputManager::Button::Up)) {
      selected_ = (selected_ + count - 1) % count;
      changed = true;
    } else if (input.wasPressed(MappedInputManager::Button::Down)) {
      selected_ = (selected_ + 1) % count;
      changed = true;
    }

    if (changed) {
      clampScroll();
      if (oldScroll == scroll_) {
        renderSelectionChange(oldSelected, selected_);
      } else {
        render();
      }
      return;
    }

    if (input.wasReleased(MappedInputManager::Button::Confirm)) {
      activateSelected();
      return;
    }

    (void)input;
  }

  void render(const HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH) {
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

    const int headerH = headerHeight();
    if (mode_ == HomeDrawerMode::Main) {
      const int titleY = drawerY_ + (headerH - renderer_.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
      renderer_.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, drawerX_ + kHomeDrawerPadX, titleY, title(), true,
                            EpdFontFamily::BOLD);
    } else {
      const int titleY = drawerY_ + (headerH - renderer_.text.getLineHeight(kHomeDrawerHeaderFont)) / 2 + 4;
      renderer_.text.render(kHomeDrawerHeaderFont, drawerX_ + kHomeDrawerPadX, titleY, title(), true,
                            EpdFontFamily::BOLD);
    }
    renderer_.line.render(drawerX_, drawerY_ + headerH, drawerX_ + drawerW_, drawerY_ + headerH, true);

    if (mode_ == HomeDrawerMode::AnnotationDetail) {
      renderDetail();
    } else if (mode_ == HomeDrawerMode::RecentsDeleteConfirm) {
      renderQuickDeleteConfirm();
    } else {
      renderRows();
    }

    drawHints();
    renderer_.displayBuffer(refreshMode);
  }

 private:
  enum class HomeDrawerMode {
    Main,
    Recents,
    RecentsActions,
    RecentsDeleteConfirm,
    Favorites,
    Bookmarks,
    Annotations,
    BookmarkDetail,
    AnnotationDetail
  };

  enum class RecentBookAction {
    MarkDone,
    RemoveDeleteCache,
    DeleteBookMetadata,
    ResetProgressStats,
  };

  struct DrawerRow {
    std::string label;
    std::string sublabel;
    std::string bookPath;
    std::string bookTitle;
    std::string bookAuthor;
    std::string cachePath;
    int recentIndex = -1;
    int actionId = -1;
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
  DrawerRow selectedBookRow_;
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
      drawerH_ = kHomeDrawerHeaderH + 4 * kHomeDrawerMainRowH + kHomeDrawerMainBottomPad;
      drawerY_ = sh - drawerH_;
    } else {
      drawerX_ = 0;
      drawerW_ = sw;
      drawerH_ = kHomeDrawerHeaderH + 4 * kHomeDrawerMainRowH + kHomeDrawerMainBottomPad;
      drawerY_ = sh - drawerH_;
    }
    const int rowH = mode_ == HomeDrawerMode::Main ? kHomeDrawerMainRowH : kHomeDrawerRowH;
    rowsPerPage_ = mode_ == HomeDrawerMode::Main ? 4 : std::max(1, (drawerH_ - headerHeight() - 12 - 46) / rowH);
  }

  int headerHeight() const {
    return mode_ == HomeDrawerMode::Main ? kHomeDrawerHeaderH : kHomeDrawerHeaderH + kHomeDrawerPageHeaderExtraH;
  }

  const char* title() const {
    switch (mode_) {
      case HomeDrawerMode::Recents:
        return "Recents";
      case HomeDrawerMode::RecentsActions:
        return "Book options";
      case HomeDrawerMode::RecentsDeleteConfirm:
        return "Remove recent";
      case HomeDrawerMode::Favorites:
        return "Favorites";
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
      return 4;
    }
    return static_cast<int>(rows_.size());
  }

  std::string mainLabel(const int index) const {
    switch (index) {
      case 0:
        return "Recents";
      case 1:
        return "Bookmarks";
      case 2:
        return "Annotations";
      case 3:
      default:
        return "Favorites";
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
      const char* empty = mode_ == HomeDrawerMode::Recents          ? "No metadata books"
                          : mode_ == HomeDrawerMode::RecentsActions ? "No book options"
                          : mode_ == HomeDrawerMode::Favorites      ? "No favorites"
                          : mode_ == HomeDrawerMode::Bookmarks      ? "No bookmarks"
                          : mode_ == HomeDrawerMode::Annotations    ? "No annotations"
                                                                    : "";
      const int msgY = drawerY_ + headerHeight() + 42;
      renderer_.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, msgY, empty, true);
      return;
    }

    const int rowH = mode_ == HomeDrawerMode::Main ? kHomeDrawerMainRowH : kHomeDrawerRowH;
    const int headerH = headerHeight();
    for (int row = 0; row < rowsPerPage_; ++row) {
      const int itemIndex = scroll_ + row;
      if (itemIndex >= count) {
        break;
      }
      renderRowSlot(row);
    }
  }

  void renderRowSlot(const int slot) {
    const int itemIndex = scroll_ + slot;
    if (itemIndex < 0 || itemIndex >= itemCount()) {
      return;
    }
    const int rowH = mode_ == HomeDrawerMode::Main ? kHomeDrawerMainRowH : kHomeDrawerRowH;
    const int itemY = drawerY_ + headerHeight() + 1 + slot * rowH;
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

  void renderSelectionChange(const int oldSelected, const int newSelected) {
    if (oldSelected >= scroll_ && oldSelected < scroll_ + rowsPerPage_) {
      renderRowSlot(oldSelected - scroll_);
    }
    if (newSelected != oldSelected && newSelected >= scroll_ && newSelected < scroll_ + rowsPerPage_) {
      renderRowSlot(newSelected - scroll_);
    }
    renderer_.displayBuffer(HalDisplay::FAST_REFRESH);
  }

  void renderLoading(const char* header, const char* message) {
    syncLayout();
    renderer_.clearScreen();
    const int headerH = kHomeDrawerHeaderH + kHomeDrawerPageHeaderExtraH;
    const int titleY = (headerH - renderer_.text.getLineHeight(kHomeDrawerHeaderFont)) / 2 + 4;
    renderer_.text.render(kHomeDrawerHeaderFont, kHomeDrawerPadX, titleY, header, true, EpdFontFamily::BOLD);
    renderer_.line.render(0, headerH, renderer_.getScreenWidth(), headerH, true);

    const int y = headerH + (renderer_.getScreenHeight() - headerH) / 2 -
                  renderer_.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID) / 2;
    renderer_.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, y, message, true, EpdFontFamily::BOLD);
    renderer_.displayBuffer(HalDisplay::FAST_REFRESH);
  }

  void renderDetail() {
    if (mode_ == HomeDrawerMode::BookmarkDetail) {
      renderBookmarkPreview();
      return;
    }

    int y = drawerY_ + headerHeight() + 18;
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

  void renderQuickDeleteConfirm() {
    const int contentTop = drawerY_ + headerHeight();
    const int centerY = contentTop + (drawerH_ - headerHeight() - 46) / 2;
    const std::string title =
        selectedBookRow_.label.empty() ? "Selected book" : renderer_.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID,
                                                                                    selectedBookRow_.label.c_str(),
                                                                                    drawerW_ - kHomeDrawerPadX * 2);

    renderer_.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, centerY - 34, "Remove from recents?", true,
                            EpdFontFamily::BOLD);
    renderer_.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY - 4, title.c_str(), true,
                            EpdFontFamily::REGULAR);
    renderer_.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, centerY + 24, "Book metadata and cache will stay.", true,
                            EpdFontFamily::REGULAR);
  }

  void renderQuickDeleteConfirmOnly() {
    renderer_.rectangle.fill(drawerX_, drawerY_ + headerHeight() + 1, drawerW_,
                             drawerH_ - headerHeight() - 1, false);
    renderQuickDeleteConfirm();
    drawHints();
    renderer_.displayBuffer(HalDisplay::FAST_REFRESH);
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
    const auto labels = owner_.mappedInput.mapLabels("Back", "Select", "Up", "");
    if (mode_ == HomeDrawerMode::Recents) {
      const auto recentLabels = owner_.mappedInput.mapLabels("Back", "Select", "Remove", "");
      renderer_.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, recentLabels.btn1, recentLabels.btn2,
                               recentLabels.btn3, recentLabels.btn4);
      return;
    }
    if (mode_ == HomeDrawerMode::RecentsDeleteConfirm) {
      const auto confirmLabels = owner_.mappedInput.mapLabels("Cancel", "Remove", "", "");
      renderer_.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, confirmLabels.btn1, confirmLabels.btn2,
                               confirmLabels.btn3, confirmLabels.btn4);
      return;
    }
    renderer_.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  void activateSelected() {
    if (mode_ == HomeDrawerMode::Main) {
      if (selected_ == 0) {
        renderLoading("Recents", "Loading Books");
        loadRecents();
        mode_ = HomeDrawerMode::Recents;
      } else if (selected_ == 1) {
        renderLoading("Bookmarks", "Loading Bookmarks");
        loadBookmarks();
        mode_ = HomeDrawerMode::Bookmarks;
      } else if (selected_ == 2) {
        renderLoading("Annotations", "Loading Annotations");
        loadAnnotations();
        mode_ = HomeDrawerMode::Annotations;
      } else {
        renderLoading("Favorites", "Loading Books");
        loadFavorites();
        mode_ = HomeDrawerMode::Favorites;
      }
      selected_ = 0;
      scroll_ = 0;
      render(HalDisplay::HALF_REFRESH);
      return;
    }

    if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) {
      return;
    }
    if (mode_ == HomeDrawerMode::Recents) {
      openSelectedRecentActions();
      return;
    }
    if (mode_ == HomeDrawerMode::RecentsActions) {
      applySelectedRecentAction();
      return;
    }
    if (mode_ == HomeDrawerMode::Favorites) {
      openSelectedFavorite();
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
    const bool hadIndex = loadRecentIndex();
    size_t loadedIndexCount = rows_.size();

    auto hasPath = [&](const std::string& path) {
      return !path.empty() &&
             std::any_of(rows_.begin(), rows_.end(), [&](const DrawerRow& row) { return row.bookPath == path; });
    };
    auto hasCache = [&](const std::string& cachePath) {
      return !cachePath.empty() &&
             std::any_of(rows_.begin(), rows_.end(), [&](const DrawerRow& row) { return row.cachePath == cachePath; });
    };

    auto addRow = [&](const std::string& path, const std::string& title, const std::string& author,
                      const std::string& cachePath, const int recentIndex) {
      if (hasPath(path)) {
        return;
      }
      if (hasCache(cachePath)) {
        return;
      }

      DrawerRow row;
      row.label = title.empty() ? formatTitle(getBaseFilename(path.empty() ? cachePath : path)) : title;
      row.sublabel = author;
      row.bookPath = path;
      row.bookTitle = title;
      row.bookAuthor = author;
      row.cachePath = cachePath;
      row.recentIndex = recentIndex;
      rows_.push_back(std::move(row));
    };

    for (const auto& book : BOOK_STATE.books) {
      if (book.path.empty()) {
        continue;
      }
      int recentIndex = -1;
      const RecentBook* recent = findRecentBookByPath(book.path, &recentIndex);
      const std::string cachePath = recent ? cachePathForRecentBook(*recent) : epubCachePathForBookPath(book.path);
      std::string title = recent && !recent->title.empty() ? recent->title : book.title;
      std::string author = recent && !recent->author.empty() ? recent->author : book.author;
      if (!hadIndex && (title.empty() || author.empty()) && !cachePath.empty()) {
        BookReadingStats stats;
        if (loadBookStats(cachePath.c_str(), stats)) {
          if (title.empty()) {
            title = stats.title;
          }
          if (author.empty()) {
            author = stats.author;
          }
        }
      }
      addRow(book.path, title, author, cachePath, recentIndex);
    }

    const auto& books = RECENT_BOOKS.getBooks();
    for (size_t i = 0; i < books.size(); ++i) {
      addRow(books[i].path, bookDisplayTitle(books[i]), books[i].author, cachePathForRecentBook(books[i]),
             static_cast<int>(i));
    }

    if (!hadIndex) {
      const std::vector<std::string> caches = epubCacheDirs();
      for (const std::string& cachePath : caches) {
        if (hasCache(cachePath)) {
          continue;
        }
        int recentIndex = -1;
        const RecentBook* recent = findRecentBookByCachePath(cachePath, &recentIndex);
        if (recent) {
          addRow(recent->path, bookDisplayTitle(*recent), recent->author, cachePath, recentIndex);
          continue;
        }

        BookReadingStats stats;
        const bool hasStats = loadBookStats(cachePath.c_str(), stats);
        addRow("", hasStats && !stats.title.empty() ? stats.title : titleForCachePath(cachePath),
               hasStats ? stats.author : "", cachePath, -1);
      }
    }

    if (!hadIndex || rows_.size() != loadedIndexCount) {
      saveRecentIndex();
    }
  }

  bool loadRecentIndex() {
    FsFile file;
    if (!SdMan.openFileForRead("RDI", kRecentDrawerIndexFile, file)) {
      return false;
    }

    uint16_t version = 0;
    uint16_t count = 0;
    serialization::readPod(file, version);
    serialization::readPod(file, count);
    if (version != kRecentDrawerIndexVersion || count > 4096) {
      file.close();
      rows_.clear();
      return false;
    }

    rows_.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
      DrawerRow row;
      serialization::readString(file, row.bookPath);
      serialization::readString(file, row.cachePath);
      serialization::readString(file, row.bookTitle);
      serialization::readString(file, row.bookAuthor);
      row.label = row.bookTitle.empty()
                      ? formatTitle(getBaseFilename(row.bookPath.empty() ? row.cachePath : row.bookPath))
                      : row.bookTitle;
      row.sublabel = row.bookAuthor;
      rows_.push_back(std::move(row));
      if ((i % 128u) == 127u) {
        yield();
      }
    }
    file.close();
    return true;
  }

  void saveRecentIndex() const {
    SdMan.mkdir("/.metadata");

    FsFile file;
    if (!SdMan.openFileForWrite("RDI", kRecentDrawerIndexFile, file)) {
      return;
    }

    serialization::writePod(file, kRecentDrawerIndexVersion);
    const uint16_t count = static_cast<uint16_t>(std::min<size_t>(rows_.size(), 4096));
    serialization::writePod(file, count);
    for (uint16_t i = 0; i < count; ++i) {
      const DrawerRow& row = rows_[i];
      serialization::writeString(file, row.bookPath);
      serialization::writeString(file, row.cachePath);
      serialization::writeString(file, row.bookTitle.empty() ? row.label : row.bookTitle);
      serialization::writeString(file, row.bookAuthor.empty() ? row.sublabel : row.bookAuthor);
      if ((i % 128u) == 127u) {
        yield();
      }
    }
    file.close();
  }

  void removeSelectedFromRecentIndex() {
    FsFile file;
    if (!SdMan.openFileForRead("RDI", kRecentDrawerIndexFile, file)) {
      return;
    }

    uint16_t version = 0;
    uint16_t count = 0;
    serialization::readPod(file, version);
    serialization::readPod(file, count);
    if (version != kRecentDrawerIndexVersion || count > 4096) {
      file.close();
      SdMan.remove(kRecentDrawerIndexFile);
      return;
    }

    std::vector<DrawerRow> kept;
    kept.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
      DrawerRow row;
      serialization::readString(file, row.bookPath);
      serialization::readString(file, row.cachePath);
      serialization::readString(file, row.bookTitle);
      serialization::readString(file, row.bookAuthor);
      const bool samePath = !selectedBookRow_.bookPath.empty() && row.bookPath == selectedBookRow_.bookPath;
      const bool sameCache = !selectedBookRow_.cachePath.empty() && row.cachePath == selectedBookRow_.cachePath;
      if (!samePath && !sameCache) {
        row.label = row.bookTitle.empty()
                        ? formatTitle(getBaseFilename(row.bookPath.empty() ? row.cachePath : row.bookPath))
                        : row.bookTitle;
        row.sublabel = row.bookAuthor;
        kept.push_back(std::move(row));
      }
      if ((i % 128u) == 127u) {
        yield();
      }
    }
    file.close();

    rows_.swap(kept);
    saveRecentIndex();
    rows_.clear();
  }

  void loadFavorites() {
    rows_.clear();
    const std::vector<BookState::Book> favorites = BOOK_STATE.getFavoriteBooks();
    for (const auto& book : favorites) {
      DrawerRow row;
      row.label = book.title.empty() ? formatTitle(getBaseFilename(book.path)) : book.title;
      row.sublabel = book.author;
      row.bookPath = book.path;
      row.bookTitle = book.title;
      row.bookAuthor = book.author;
      rows_.push_back(std::move(row));
    }
  }

  void openSelectedRecentActions() {
    if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) {
      return;
    }
    selectedBookRow_ = rows_[selected_];
    rows_.clear();

    DrawerRow markDone;
    markDone.label = "Mark as done";
    markDone.actionId = static_cast<int>(RecentBookAction::MarkDone);
    rows_.push_back(std::move(markDone));

    DrawerRow removeCache;
    removeCache.label = "Remove & delete cache";
    removeCache.actionId = static_cast<int>(RecentBookAction::RemoveDeleteCache);
    rows_.push_back(std::move(removeCache));

    DrawerRow deleteBook;
    deleteBook.label = "Delete book & metadata";
    deleteBook.actionId = static_cast<int>(RecentBookAction::DeleteBookMetadata);
    rows_.push_back(std::move(deleteBook));

    DrawerRow reset;
    reset.label = "Reset Progress & Statistics";
    reset.actionId = static_cast<int>(RecentBookAction::ResetProgressStats);
    rows_.push_back(std::move(reset));

    mode_ = HomeDrawerMode::RecentsActions;
    selected_ = 0;
    scroll_ = 0;
    render();
  }

  void openQuickDeleteRecentConfirm() {
    if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) {
      return;
    }
    selectedBookRow_ = rows_[selected_];
    mode_ = HomeDrawerMode::RecentsDeleteConfirm;
    renderQuickDeleteConfirmOnly();
  }

  void confirmQuickDeleteRecent() {
    removeSelectedRecentEntryOnly();
    owner_.loadRecentBooks(false);
    loadRecents();
    mode_ = HomeDrawerMode::Recents;
    selected_ = 0;
    scroll_ = 0;
    render(HalDisplay::FAST_REFRESH);
  }

  void removeSelectedRecentEntryOnly() {
    removeSelectedFromRecentIndex();
    if (!selectedBookRow_.bookPath.empty()) {
      RECENT_BOOKS.removeBook(selectedBookRow_.bookPath);
    }
  }

  void applySelectedRecentAction() {
    if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) {
      return;
    }
    switch (static_cast<RecentBookAction>(rows_[selected_].actionId)) {
      case RecentBookAction::MarkDone:
        markSelectedBookDone();
        break;
      case RecentBookAction::RemoveDeleteCache:
        removeSelectedBookAndCache(false);
        break;
      case RecentBookAction::DeleteBookMetadata:
        removeSelectedBookAndCache(true);
        break;
      case RecentBookAction::ResetProgressStats:
        resetSelectedBookProgressAndStats();
        break;
      default:
        break;
    }
    owner_.loadRecentBooks(false);
    loadRecents();
    mode_ = HomeDrawerMode::Recents;
    selected_ = 0;
    scroll_ = 0;
    render(HalDisplay::FAST_REFRESH);
  }

  void markSelectedBookDone() {
    if (selectedBookRow_.bookPath.empty()) {
      return;
    }
    BOOK_STATE.setFinished(selectedBookRow_.bookPath, true);
    RECENT_BOOKS.updateProgress(selectedBookRow_.bookPath, 1.0f);
  }

  void resetSelectedBookProgressAndStats() {
    if (!selectedBookRow_.cachePath.empty()) {
      BookProgress(selectedBookRow_.cachePath).remove();
      const std::string statsPath = selectedBookRow_.cachePath + "/statistics.bin";
      SdMan.remove(statsPath.c_str());
    }
    if (!selectedBookRow_.bookPath.empty()) {
      BOOK_STATE.setReading(selectedBookRow_.bookPath, false);
      BOOK_STATE.setFinished(selectedBookRow_.bookPath, false);
      RECENT_BOOKS.updateProgress(selectedBookRow_.bookPath, 0.0f);
      clearLastReadIfSelected();
    }
    saveGlobalStats(generateGlobalStats());
  }

  void removeSelectedBookAndCache(const bool deleteBookFile) {
    const std::string bookPath = selectedBookRow_.bookPath;
    const std::string cachePath = selectedBookRow_.cachePath;
    removeSelectedFromRecentIndex();

    if (!bookPath.empty()) {
      RECENT_BOOKS.removeBook(bookPath);
      if (deleteBookFile) {
        BOOK_STATE.removeBook(bookPath);
      } else {
        BOOK_STATE.setReading(bookPath, false);
      }
      if (deleteBookFile && SdMan.exists(bookPath.c_str())) {
        SdMan.remove(bookPath.c_str());
      }
      clearLastReadIfSelected();
    }

    removeCacheDir(cachePath);
    saveGlobalStats(generateGlobalStats());
  }

  void clearLastReadIfSelected() {
    if (!selectedBookRow_.bookPath.empty() && APP_STATE.lastRead == selectedBookRow_.bookPath) {
      APP_STATE.lastRead.clear();
      APP_STATE.saveToFile();
    }
  }

  bool removeCacheDir(const std::string& cachePath) {
    if (cachePath.empty() || !SdMan.exists(cachePath.c_str())) {
      return true;
    }
    if (SdMan.removeDir(cachePath.c_str()) || SdMan.remove(cachePath.c_str())) {
      return true;
    }

    std::vector<String> files = SdMan.listFiles(cachePath.c_str(), 100);
    for (const auto& file : files) {
      const std::string fullPath = cachePath + "/" + std::string(file.c_str());
      SdMan.remove(fullPath.c_str());
    }
    return SdMan.removeDir(cachePath.c_str());
  }

  void openSelectedFavorite() {
    if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) {
      return;
    }
    const DrawerRow& row = rows_[selected_];
    owner_.openBookPath(row.bookPath, row.bookTitle, row.bookAuthor, true);
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
