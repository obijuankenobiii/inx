#include "EpubActivity.h"

#include <cstdio>

#include <Epub/Page.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <time.h>

#include "MenuDrawer.h"
#include "SettingsDrawer.h"
#include "state/BookProgress.h"
#include "state/BookSetting.h"
#include "state/SystemSetting.h"
#include "state/BookState.h"
#include "state/RecentBooks.h"
#include "state/Session.h"
#include "state/Statistics.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

namespace {
constexpr unsigned long skipChapterMs = 700;
constexpr unsigned long goHomeMs = 1000;
constexpr int statusBarMargin = 19;
constexpr int progressBarMarginTop = 10;
constexpr unsigned long bookmarkHoldMs = 1000;
constexpr unsigned long STATS_SAVE_INTERVAL_MS = 30000;

/**
 * MAP_NONE adds L/R to Up/Down for paging. In landscape CCW, physical left = next page and right = previous;
 * in landscape CW (and portrait), left = previous and right = next.
 */
struct ReaderBitmapStyleGuard {
  GfxRenderer& r;
  GfxRenderer::BitmapGrayRenderStyle prev;
  explicit ReaderBitmapStyleGuard(GfxRenderer& ren)
      : r(ren), prev(ren.getBitmapGrayRenderStyle()) {
    SystemSetting& s = SystemSetting::getInstance();
    r.setBitmapGrayRenderStyle(s.readerImagePresentation == SystemSetting::IMAGE_PRESENTATION_FULL_GRAY
                                   ? GfxRenderer::BitmapGrayRenderStyle::FullGray
                                   : GfxRenderer::BitmapGrayRenderStyle::Balanced);
  }
  ~ReaderBitmapStyleGuard() { r.setBitmapGrayRenderStyle(prev); }
};

void addMapNoneLandscapeLeftRightForPageTurn(const GfxRenderer::Orientation orientation,
                                            const MappedInputManager& mappedInput, bool& prev, bool& next) {
  const bool leftReleased = mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool rightReleased = mappedInput.wasReleased(MappedInputManager::Button::Right);
  if (orientation == GfxRenderer::Orientation::LandscapeCounterClockwise) {
    if (!prev) prev = rightReleased;
    if (!next) next = leftReleased;
  } else {
    if (!prev) prev = leftReleased;
    if (!next) next = rightReleased;
  }
}
}  // namespace

/**
 * @brief Structure containing viewport calculation results.
 */
struct ViewportInfo {
  int totalMarginTop;
  int totalMarginBottom;
  int totalMarginLeft;
  int totalMarginRight;
  uint16_t width;
  uint16_t height;
  int fontId;
  float lineCompression;
};

/**
 * @brief Constructs a new EpubActivity
 * @param renderer Reference to the graphics renderer
 * @param mappedInput Reference to the input manager
 * @param epub Unique pointer to the EPUB document
 * @param onGoBack Callback for returning to previous activity
 * @param onGoToRecent Callback for navigating to recent books
 */
EpubActivity::EpubActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub,
                           const std::function<void()>& onGoBack, const std::function<void()>& onGoToRecent)
    : ActivityWithSubactivity("EpubReader", renderer, mappedInput),
      epub(std::move(epub)),
      onGoBack(onGoBack),
      onGoToRecent(onGoToRecent),
      currentSpineIndex(0),
      nextPageNumber(0),
      pagesUntilFullRefresh(bookSettings.refreshFrequency),
      cachedSpineIndex(0),
      cachedChapterTotalPageCount(0),
      updateRequired(false),
      loadingProgress(0),
      showBookmarkIndicator(false),
      lastPreloadedSpineIndex(-1),
      lastPageHadImages(false),
      bookmarkLongPressProcessed(false),
      settingsDrawer(nullptr),
      settingsDrawerVisible(false),
      menuDrawer(nullptr),
      menuDrawerVisible(false),
      pageStartTime(0),
      lastSaveTime(0),
      bookProgress(nullptr) {
  bookmarks.reserve(MAX_BOOKMARKS);
  loadBookSettings();

  bookStats.totalReadingTimeMs = 0;
  bookStats.totalPagesRead = 0;
  bookStats.totalChaptersRead = 0;
  bookStats.lastReadTimeMs = 0;
  bookStats.progressPercent = 0;
  bookStats.lastSpineIndex = 0;
  bookStats.lastPageNumber = 0;
  bookStats.avgPageTimeMs = 0;
}

/**
 * @brief Static trampoline function for the display task
 * @param param Pointer to the EpubActivity instance
 */
void EpubActivity::taskTrampoline(void* param) {
  auto* self = static_cast<EpubActivity*>(param);
  self->displayTaskLoop();
}

/**
 * @brief Draws the loading screen with progress bar
 */
void EpubActivity::drawLoadingScreen() {
  const int barWidth = renderer.getScreenWidth();
  const int barHeight = 25;
  const int barX = 0;
  const int barY = renderer.getScreenHeight() - barHeight;

  renderer.fillRect(barX, barY, barWidth, barHeight, false);
  renderer.drawRect(barX, barY, barWidth, barHeight, true);

  if (loadingProgress > 0) {
    int fillWidth = barWidth * loadingProgress / 100;
    if (fillWidth > 0) {
      renderer.fillRect(barX + 2, barY + 2, fillWidth - 4, barHeight - 4, true);
    }
  }

  char percentStr[8];
  snprintf(percentStr, sizeof(percentStr), "%d%%", loadingProgress);
  int percentWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, percentStr);
  int percentX = barX + (barWidth - percentWidth) / 2;
  int percentY = barY + (barHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

  if (loadingProgress > 50) {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, percentX, percentY, percentStr, false);
  } else {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, percentX, percentY, percentStr, true);
  }

  renderer.displayBuffer();
}

/**
 * @brief Calculates the viewport dimensions based on current settings
 * @return ViewportInfo structure containing viewport dimensions and settings
 */
ViewportInfo EpubActivity::calculateViewport() {
  ViewportInfo info;

  int oT, oR, oB, oL;
  renderer.getOrientedViewableTRBL(&oT, &oR, &oB, &oL);

  info.totalMarginTop = oT + bookSettings.screenMargin;
  info.totalMarginBottom = oB + bookSettings.screenMargin;
  info.totalMarginLeft = oL + bookSettings.screenMargin;
  info.totalMarginRight = oR + bookSettings.screenMargin;

  bool hasStatusBar = (bookSettings.statusBarLeft.item != StatusBarItem::NONE ||
                       bookSettings.statusBarMiddle.item != StatusBarItem::NONE ||
                       bookSettings.statusBarRight.item != StatusBarItem::NONE);

  bool showProgressBar = (bookSettings.statusBarMiddle.item == StatusBarItem::PROGRESS_BAR ||
                          bookSettings.statusBarMiddle.item == StatusBarItem::PROGRESS_BAR_WITH_PERCENT);

  if (hasStatusBar) {
    info.totalMarginBottom +=
        statusBarMargin - bookSettings.screenMargin +
        (showProgressBar ? (ScreenComponents::BOOK_PROGRESS_BAR_HEIGHT + progressBarMarginTop) : 0);
  }

  int w = renderer.getScreenWidth() - info.totalMarginLeft - info.totalMarginRight;
  int h = renderer.getScreenHeight() - info.totalMarginTop - info.totalMarginBottom;
  constexpr int kMinViewport = 8;
  if (w < kMinViewport) w = kMinViewport;
  if (h < kMinViewport) h = kMinViewport;
  info.width = static_cast<uint16_t>(w);
  info.height = static_cast<uint16_t>(h);

  info.fontId = bookSettings.getReaderFontId();
  info.lineCompression = bookSettings.getReaderLineCompression();

  return info;
}

/**
 * @brief Builds a section file for a given spine index
 * @param spineIndex Index of the spine to build
 * @param info Viewport information for rendering
 * @param showProgress Whether to show progress during building
 * @param skipImages If true, skip processing new images
 * @return true if successful, false otherwise
 */
bool EpubActivity::buildSection(int spineIndex, const ViewportInfo& info, bool showProgress, bool skipImages) {
  if (!epub) return false;

  isDoingSomethingHeavy = true;
  std::string cachePath = epub->getCachePath();
  std::string sectionPath = cachePath + "/" + std::to_string(spineIndex) + ".sec";

  if (SdMan.exists(sectionPath.c_str())) {
    SdMan.remove(sectionPath.c_str());
  }

  std::shared_ptr<Epub> sharedEpub = std::shared_ptr<Epub>(epub.get(), [](Epub*) {});
  auto tempSection = std::unique_ptr<Section>(new Section(sharedEpub, spineIndex, renderer));

  std::function<void()> progressCallback = nullptr;
  if (showProgress) {
    progressCallback = [this]() { ScreenComponents::drawPopup(renderer, "Loading Chapter..."); };
    renderer.displayBuffer();
  }

  bool success =
      tempSection->createSectionFile(
        info.fontId, 
        FontManager::getNextFont(info.fontId),
        FontManager::getMaxFontId(info.fontId),
        info.lineCompression,
        bookSettings.extraParagraphSpacing, 
        bookSettings.paragraphAlignment, 
        info.width,
        info.height, 
        bookSettings.hyphenationEnabled, 
        progressCallback, 
        skipImages
      );

  if (isDoingSomethingHeavy && showProgress) {
    renderer.clearScreen();
    renderer.displayBuffer();
  }

  isDoingSomethingHeavy = false;

  return success;
}

/**
 * @brief Loads a section for a given spine index
 * @param spineIndex Index of the spine to load
 * @param info Viewport information for rendering
 * @return Unique pointer to the loaded section
 */
std::unique_ptr<Section> EpubActivity::loadSection(int spineIndex, const ViewportInfo& info) {
  if (!epub) return nullptr;

  std::shared_ptr<Epub> sharedEpub = std::shared_ptr<Epub>(epub.get(), [](Epub*) {});
  auto loadedSection = std::unique_ptr<Section>(new Section(sharedEpub, spineIndex, renderer));

  bool isCached = loadedSection->loadSectionFile(info.fontId, info.lineCompression, bookSettings.extraParagraphSpacing,
                                                 bookSettings.paragraphAlignment, info.width, info.height,
                                                 bookSettings.hyphenationEnabled);

  if (!isCached && loadedSection) {
    buildSection(spineIndex, info, true, false);
    loadedSection->loadSectionFile(info.fontId, info.lineCompression, bookSettings.extraParagraphSpacing,
                                   bookSettings.paragraphAlignment, info.width, info.height,
                                   bookSettings.hyphenationEnabled);
  }

  return loadedSection;
}

/**
 * @brief Sets up orientation based on book settings
 */
void EpubActivity::setupOrientation() {
  switch (bookSettings.orientation) {
    case SystemSetting::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case SystemSetting::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case SystemSetting::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case SystemSetting::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
  // Landscape CW maps logical coordinates with a 180° flip vs the panel; swap directional inputs
  // so physical up/down/left/right (and page keys) stay aligned with what is drawn.
  mappedInput.setInvertDirectionalAxes180(renderer.getOrientation() == GfxRenderer::Orientation::LandscapeClockwise);
}

void EpubActivity::syncOrientationFromGlobalIfNeeded() {
  if (!bookSettings.useCustomSettings) {
    bookSettings.orientation = SystemSetting::getInstance().orientation;
  }
}

void EpubActivity::onBookSettingsLiveLayoutSync() {
  // Do not call setupOrientation() or relayout the menu while the book settings drawer is open;
  // orientation and section rebuild run when the drawer closes (see isToggleClosed).
  // Do not set updateRequired here: the display task's renderScreen() clears the framebuffer and
  // redraws the page before the drawer, which makes the drawer flash on/off in landscape (CCW/CW)
  // while changing values. The drawer already redraws via handleInput -> renderWithRefresh.
  if (settingsDrawer) {
    settingsDrawer->relayoutForRendererChange();
  }
}

/**
 * @brief Loads progress from file using BookProgress handler
 */
void EpubActivity::loadProgress() {
  if (!bookProgress) {
    return;
  }

  BookProgress::Data data;
  int totalSpines = epub->getSpineItemsCount();

  if (bookProgress->load(data) && bookProgress->validate(data, totalSpines)) {
    currentSpineIndex = data.spineIndex;
    nextPageNumber = data.pageNumber;
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = data.chapterPageCount;
  } else {
    bookProgress->remove();
    currentSpineIndex = 0;
    nextPageNumber = 0;
    cachedSpineIndex = 0;
    cachedChapterTotalPageCount = 0;
  }
}

/**
 * @brief Saves current progress using BookProgress handler
 * @param spineIndex Current spine index
 * @param currentPage Current page number
 * @param pageCount Total pages in current chapter
 */
void EpubActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  if (!bookProgress || !epub) {
    return;
  }

  BookProgress::Data data;
  data.spineIndex = spineIndex;
  data.pageNumber = currentPage;
  data.chapterPageCount = pageCount;
  data.lastReadTimestamp = millis();

  if (pageCount > 0) {
    float spineProgress = static_cast<float>(currentPage) / static_cast<float>(pageCount);
    data.progressPercent = epub->calculateProgress(spineIndex, spineProgress) * 100.0f;
  }

  bookProgress->save(data);

  if (pageCount > 0) {
    float spineProgress = static_cast<float>(currentPage) / static_cast<float>(pageCount);
    float bookProgressValue = epub->calculateProgress(spineIndex, spineProgress);
    RECENT_BOOKS.addBook(epub->getPath(), epub->getCachePath(), epub->getTitle(), epub->getAuthor(), bookProgressValue);
  }
}

/**
 * @brief Ensures thumbnail exists, generates if needed
 */
void EpubActivity::ensureThumbnailExists() {
  std::string thumbPath = epub->getThumbBmpPath();
  if (!SdMan.exists(thumbPath.c_str())) {
    epub->generateThumbBmp();
  }
}

/**
 * @brief Displays cover if it exists, otherwise shows title
 */
void EpubActivity::displayCoverOrTitle() {
  std::string coverPath = epub->getCoverBmpPath(false);

  if (!SdMan.exists(coverPath.c_str())) {
    epub->generateCoverBmp(false);
  }

  FsFile coverFile;
  if (SdMan.openFileForRead("EBP", coverPath, coverFile)) {
    Bitmap coverBmp(coverFile);
    if (coverBmp.parseHeaders() == BmpReaderError::Ok) {
      ReaderBitmapStyleGuard bitmapStyleGuard(renderer);
      renderer.clearScreen();
      renderer.drawBitmap(coverBmp, 0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
      renderer.displayBuffer();
    }
    coverFile.close();
  } else {
    displayBookTitle();
  }
}

/**
 * @brief Loads and sets up the current section
 */
void EpubActivity::loadCurrentSection() {
  ViewportInfo info = calculateViewport();
  auto newSection = loadSection(currentSpineIndex, info);

  if (newSection) {
    section = std::move(newSection);
    if (nextPageNumber == UINT16_MAX) {
      section->currentPage = section->pageCount - 1;
    } else {
      section->currentPage = nextPageNumber;
    }
    if (section->currentPage >= section->pageCount) section->currentPage = 0;

    if (cachedChapterTotalPageCount > 0 && currentSpineIndex == cachedSpineIndex &&
        section->pageCount != cachedChapterTotalPageCount) {
      float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
      int newPage = static_cast<int>(progress * section->pageCount);
      section->currentPage = std::min(newPage, section->pageCount - 1);
      cachedChapterTotalPageCount = 0;
    }
  }
}

/**
 * @brief Preloads next few chapters
 */
void EpubActivity::preloadChapters() {
  ViewportInfo info = calculateViewport();
  int totalSpineItems = epub->getSpineItemsCount();
  int chaptersToCache = std::min(8, totalSpineItems);

  for (int i = 1; i < chaptersToCache; i++) {
    buildSection(i, info, false, false);
  }

  int nextChapterIndex = currentSpineIndex + 1;
  if (nextChapterIndex < epub->getSpineItemsCount()) {
    buildSection(nextChapterIndex, info, false, false);
  }
}

/**
 * @brief Updates recent books and app state
 */
void EpubActivity::updateExternalState() {
  APP_STATE.lastRead = "";
  APP_STATE.saveToFile();

  float spineProgress = section ? static_cast<float>(section->currentPage) / section->pageCount : 0;
  float bookProgressValue = epub->calculateProgress(currentSpineIndex, spineProgress);
  RECENT_BOOKS.addBook(epub->getPath(), epub->getCachePath(), epub->getTitle(), epub->getAuthor(), bookProgressValue);
}

/**
 * @brief Fast path for books that were opened before
 */
void EpubActivity::fastPath() {
  loadProgress();

  int totalSpineItems = epub->getSpineItemsCount();
  if (currentSpineIndex >= totalSpineItems) {
    currentSpineIndex = 0;
    nextPageNumber = 0;
    cachedSpineIndex = 0;
    cachedChapterTotalPageCount = 0;
  }

  loadCurrentSection();
  statusBar = std::unique_ptr<StatusBar>(new StatusBar(renderer, *epub, bookSettings));

  xTaskCreate(&EpubActivity::taskTrampoline, "EpubActivityTask", 16384, this, 1, &displayTaskHandle);
}

/**
 * @brief Slow path for new books
 */
void EpubActivity::slowPath() {
  displayCoverOrTitle();
  loadingProgress = 10;
  drawLoadingScreen();
  vTaskDelay(pdMS_TO_TICKS(50));

  ensureThumbnailExists();
  currentSpineIndex = epub->getSpineIndexForTextReference();
  nextPageNumber = 0;

  BOOK_STATE.addOrUpdateBook(epub->getPath(), epub->getTitle(), epub->getAuthor());

  preloadChapters();
  loadingProgress = 50;
  drawLoadingScreen();

  loadCurrentSection();

  loadingProgress = 100;
  drawLoadingScreen();

  statusBar = std::unique_ptr<StatusBar>(new StatusBar(renderer, *epub, bookSettings));

  xTaskCreate(&EpubActivity::taskTrampoline, "EpubActivityTask", 16384, this, 1, &displayTaskHandle);
}

/**
 * @brief Called when entering the activity
 */
void EpubActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  epub->setupCacheDir();
  renderer.clearScreen(0xff);
  renderer.displayBuffer();

  syncOrientationFromGlobalIfNeeded();
  setupOrientation();

  bookProgress.reset(new BookProgress(epub->getCachePath()));
  bool hasProgress = bookProgress->exists();
  const auto* book = BOOK_STATE.findBookByPath(epub->getPath());
  updateExternalState();
  loadBookmarks();
  initStats();

  if (book && hasProgress) {
    fastPath();
  } else {
    ScreenComponents::drawPopup(renderer, "Preparing book...");
    renderer.displayBuffer();
    slowPath();
  }
  updateRequired = true;
  lastAutoPageTurnTime = millis();
  bookLayoutAppliedOrientation_ = bookSettings.orientation;
}

/**
 * @brief Called when exiting the activity
 */
void EpubActivity::onExit() {
  if (menuDrawer) {
    menuDrawer->hide();
    delete menuDrawer;
    menuDrawer = nullptr;
  }

  if (settingsDrawer) {
    delete settingsDrawer;
    settingsDrawer = nullptr;
  }

  if (displayTaskHandle) {
    pauseDisplayTaskForRebuild = false;
    displayRebuildPausedAck = false;
    epubScreenRenderBusy = false;
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (pageStartTime > 0) {
    endPageTimer();
  }

  if (epub) {
    saveBookStats();

    if (section) {
      float spineProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
      float bookProgressValue = epub->calculateProgress(currentSpineIndex, spineProgress);
      RECENT_BOOKS.addBook(epub->getPath(), epub->getCachePath(), epub->getTitle(), epub->getAuthor(),
                           bookProgressValue);

      saveProgress(currentSpineIndex, section->currentPage, section->pageCount);
    }
  }

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  mappedInput.setInvertDirectionalAxes180(false);

  APP_STATE.lastRead = epub->getPath();
  APP_STATE.saveToFile();
  section.reset();
  bookProgress.reset();
  statusBar.reset();
  epub.reset();

  ActivityWithSubactivity::onExit();
}

/**
 * @brief Main loop function called repeatedly while activity is active
 */
void EpubActivity::loop() {
  if (isDoingSomethingHeavy || epubScreenRenderBusy) {
    return;
  }

  if (menuDrawerVisible && menuDrawer && !menuDrawer->isDismissed()) {
    menuDrawer->handleInput(mappedInput);
    return;
  }

  if (subActivity) {
    subActivity->loop();
    return;
  }

  MappedInputManager::Button menuBtn;
  switch (SETTINGS.readerMenuButton) {
    case SystemSetting::READER_MENU_BUTTON::MENU_DOWN:
      menuBtn = MappedInputManager::Button::Down;
      break;
    case SystemSetting::READER_MENU_BUTTON::MENU_LEFT:
      menuBtn = MappedInputManager::Button::Left;
      break;
    case SystemSetting::READER_MENU_BUTTON::MENU_RIGHT:
      menuBtn = MappedInputManager::Button::Right;
      break;
    default:
      menuBtn = MappedInputManager::Button::Up;
      break;
  }

  if (settingsDrawerVisible && settingsDrawer) {
    settingsDrawer->handleInput(mappedInput);
    if (settingsDrawer->isDismissed()) {
      saveBookSettings();
      settingsDrawerVisible = false;
      vTaskDelay(pdMS_TO_TICKS(100));
      isToggleClosed = true;
      updateRequired = true;
      lastAutoPageTurnTime = millis();
    }
    return;
  }

  if (isToggleClosed) {
    isToggleClosed = false;
    syncOrientationFromGlobalIfNeeded();
    const bool layoutNeedsRebuild =
        (settingsDrawer && settingsDrawer->shouldUpdate()) ||
        (bookSettings.orientation != bookLayoutAppliedOrientation_);
    if (layoutNeedsRebuild) {
      applyBookSettings();
      if (settingsDrawer) {
        settingsDrawer->clearUpdateFlag();
      }
    } else {
      setupOrientation();
      bookLayoutAppliedOrientation_ = bookSettings.orientation;
    }
    startPageTimer();
    return;
  }

  if (mappedInput.isPressed(menuBtn) && mappedInput.getHeldTime() >= 500) {
    endPageTimer();
    toggleSettingsDrawer();
    return;
  }

  bool prev = false;
  bool next = false;

  if (!mappedInput.isPressed(menuBtn)) {
    if (SETTINGS.readerDirectionMapping == SystemSetting::READER_DIRECTION_MAPPING::MAP_NONE) {
      prev = mappedInput.wasReleased(MappedInputManager::Button::Up);
      next = mappedInput.wasReleased(MappedInputManager::Button::Down);

      addMapNoneLandscapeLeftRightForPageTurn(renderer.getOrientation(), mappedInput, prev, next);

    } else {
      switch (SETTINGS.readerDirectionMapping) {
        case SystemSetting::READER_DIRECTION_MAPPING::MAP_RIGHT_LEFT:
          prev = mappedInput.wasReleased(MappedInputManager::Button::Right);
          next = mappedInput.wasReleased(MappedInputManager::Button::Left);
          break;

        case SystemSetting::READER_DIRECTION_MAPPING::MAP_UP_DOWN:
          prev = mappedInput.wasReleased(MappedInputManager::Button::Up);
          next = mappedInput.wasReleased(MappedInputManager::Button::Down);
          break;

        case SystemSetting::READER_DIRECTION_MAPPING::MAP_DOWN_UP:
          prev = mappedInput.wasReleased(MappedInputManager::Button::Down);
          next = mappedInput.wasReleased(MappedInputManager::Button::Up);
          break;

        default:
          prev = mappedInput.wasReleased(MappedInputManager::Button::Left);
          next = mappedInput.wasReleased(MappedInputManager::Button::Right);
          break;
      }
    }
  }

  const bool skipChapter = mappedInput.getHeldTime() >= skipChapterMs;

  if (skipChapter && (prev || next)) {
    endPageTimer();

    if (next) {
      if (currentSpineIndex < epub->getSpineItemsCount() - 1) {
        currentSpineIndex++;
        nextPageNumber = 0;
        section.reset();
      }
    } else if (prev) {
      if (currentSpineIndex > 0) {
        currentSpineIndex--;
        nextPageNumber = 0;
        section.reset();
      }
    }

    startPageTimer();
    lastAutoPageTurnTime = millis();
    updateRequired = true;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.readerShortPwrBtn == SystemSetting::READER_SHORT_PWRBTN::READER_PAGE_TURN) {
    endPageTimer();
    pageTurn(true);
    lastAutoPageTurnTime = millis();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.readerShortPwrBtn == SystemSetting::READER_SHORT_PWRBTN::READER_PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    updateRequired = true;
    return;
  }

  if (prev) {
    endPageTimer();
    pageTurn(false);
    lastAutoPageTurnTime = millis();
    return;
  }

  if (next) {
    endPageTimer();
    pageTurn(true);
    lastAutoPageTurnTime = millis();
    return;
  }

  if (bookSettings.pageAutoTurnSeconds > 0 && !menuDrawerVisible && !settingsDrawerVisible && !isDoingSomethingHeavy &&
      !epubScreenRenderBusy) {
    if (lastAutoPageTurnTime == 0) {
      lastAutoPageTurnTime = millis();
    }

    unsigned long elapsed = millis() - lastAutoPageTurnTime;
    if (elapsed >= (bookSettings.pageAutoTurnSeconds * 1000UL)) {
      lastAutoPageTurnTime = millis();
      endPageTimer();
      pageTurn(true);
      updateRequired = true;
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() < bookmarkHoldMs) {
    endPageTimer();
    toggleMenuDrawer();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= bookmarkHoldMs) {
    addBookmark();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() >= 300) {
      vTaskDelay(pdMS_TO_TICKS(100));
      onGoBack();
    }
    return;
  }
}

/**
 * @brief Callback when a chapter is selected from TOC
 * @param spineIndex The spine index to navigate to
 */
void EpubActivity::onTocChapterSelected(int spineIndex) {
  toggleMenuDrawer();
  currentSpineIndex = spineIndex;
  nextPageNumber = 0;
  section.reset();
  updateRequired = true;

  startPageTimer();
}

void EpubActivity::onBookmarkDrawerSelected(int storageIndex) {
  toggleMenuDrawer();
  goToBookmark(storageIndex);
  startPageTimer();
}

/**
 * @brief Toggles the menu drawer visibility
 */
void EpubActivity::toggleMenuDrawer() {
  if (!menuDrawer) {
    menuDrawer = new MenuDrawer(
        renderer,
        [this](MenuDrawer::MenuAction action) {
          switch (action) {
            case MenuDrawer::MenuAction::SHOW_BOOKMARKS:
              break;
            case MenuDrawer::MenuAction::SELECT_CHAPTER:
              break;
            case MenuDrawer::MenuAction::GO_HOME:
              goHome();
              break;
            case MenuDrawer::MenuAction::DELETE_CACHE:
              deleteCache();
              break;
            case MenuDrawer::MenuAction::DELETE_PROGRESS:
              deleteProgress();
              break;
            case MenuDrawer::MenuAction::DELETE_BOOK:
              deleteBook();
              break;
            case MenuDrawer::MenuAction::GENERATE_FULL_DATA:
              generateFullData();
              break;
          }
        },
        [this]() {
          updateRequired = true;
          startPageTimer();
        });
    menuDrawer->setMappedInputForHints(&mappedInput);
    if (epub) {
      menuDrawer->setEpub(epub.get());
      menuDrawer->setTocSelectionCallback([this](int spineIndex) { onTocChapterSelected(spineIndex); });
      menuDrawer->setBookmarkListProvider([this]() {
        std::vector<MenuDrawer::BookmarkNavItem> rows;
        rows.reserve(bookmarks.size());
        for (size_t i = 0; i < bookmarks.size(); ++i) {
          const auto& b = bookmarks[i];
          char line[160];
          snprintf(line, sizeof(line), "%s (%d/%d)", b.chapterTitle, static_cast<int>(b.pageNumber) + 1,
                   static_cast<int>(b.pageCount));
          MenuDrawer::BookmarkNavItem row;
          row.label = line;
          row.storageIndex = static_cast<int>(i);
          const int curPage = section ? section->currentPage : nextPageNumber;
          row.isCurrentPosition = (b.spineIndex == static_cast<uint16_t>(currentSpineIndex)) &&
                                   (b.pageNumber == static_cast<uint16_t>(curPage));
          rows.push_back(std::move(row));
        }
        return rows;
      });
      menuDrawer->setBookmarkSelectCallback([this](const int storageIndex) { onBookmarkDrawerSelected(storageIndex); });
      menuDrawer->setBookmarkDeleteCallback([this](const int storageIndex) { removeBookmark(storageIndex); });
    }
  }

  menuDrawerVisible = !menuDrawerVisible;

  if (menuDrawerVisible) {
    menuDrawer->setBookTitle(epub->getTitle());
    menuDrawer->show();
  } else {
    menuDrawer->hide();
  }
}

/**
 * @brief Toggles the settings drawer visibility
 */
void EpubActivity::toggleSettingsDrawer() {
  if (!settingsDrawer) {
    settingsDrawer = new SettingsDrawer(renderer, bookSettings, [this]() { onBookSettingsLiveLayoutSync(); });
  }

  settingsDrawerVisible = !settingsDrawerVisible;

  if (settingsDrawerVisible) {
    syncOrientationFromGlobalIfNeeded();
    // Keep current renderer orientation until the drawer is dismissed; apply on close.
    settingsDrawer->show();
    return;
  }
}

/**
 * @brief Go home
 */
void EpubActivity::goHome() {
  ScreenComponents::drawPopup(renderer, "Closing book");
  vTaskDelay(pdMS_TO_TICKS(100));
  onGoBack();
}

/**
 * @brief Deletes the book cache
 */
void EpubActivity::deleteCache() {
  ScreenComponents::drawPopup(renderer, "Deleting all book data...");
  vTaskDelay(pdMS_TO_TICKS(100));

  std::string bookPath = epub->getPath();

  epub->clearCache();

  if (bookProgress) {
    bookProgress.reset();
  }

  if (section) {
    section.reset();
  }

  if (!bookPath.empty()) {
    RECENT_BOOKS.removeBook(bookPath);
  }

  BOOK_STATE.setReading(bookPath, false);
  APP_STATE.lastRead = "";
  APP_STATE.saveToFile();
  RECENT_BOOKS.removeBook(bookPath);

  currentSpineIndex = 0;
  nextPageNumber = 0;
  cachedSpineIndex = 0;
  cachedChapterTotalPageCount = 0;

  onGoBack();
}

/**
 * @brief Deletes the reading progress
 */
void EpubActivity::deleteProgress() {
  ScreenComponents::drawPopup(renderer, "Removing progress");
  vTaskDelay(pdMS_TO_TICKS(100));
  if (!epub) {
    return;
  }

  int currentSpine = currentSpineIndex;
  int currentPage = section ? section->currentPage : 0;

  if (bookProgress) {
    bookProgress->remove();
  }

  APP_STATE.lastRead = "";
  APP_STATE.saveToFile();

  int newSpineIndex = epub->getSpineIndexForTextReference();

  if (currentSpine != newSpineIndex || currentPage != 0) {
    currentSpineIndex = newSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }

  updateRequired = true;
}

/**
 * @brief Deletes the entire book
 */
void EpubActivity::deleteBook() {
  ScreenComponents::drawPopup(renderer, "Deleting book...");
  vTaskDelay(pdMS_TO_TICKS(100));

  if (!epub) {
    onGoBack();
    return;
  }

  std::string bookPath = epub->getPath();
  std::string cacheDir = epub->getCachePath();

  if (!bookPath.empty()) {
    BOOK_STATE.setReading(bookPath, false);
    RECENT_BOOKS.removeBook(bookPath);
  }

  section.reset();
  bookProgress.reset();
  epub.reset();

  APP_STATE.lastRead = "";
  APP_STATE.saveToFile();

  vTaskDelay(pdMS_TO_TICKS(50));

  bool cacheDeleted = false;
  if (!cacheDir.empty() && SdMan.exists(cacheDir.c_str())) {
    cacheDeleted = SdMan.removeDir(cacheDir.c_str());

    if (!cacheDeleted) {
      cacheDeleted = SdMan.remove(cacheDir.c_str());
    }

    if (!cacheDeleted) {
      std::vector<String> files = SdMan.listFiles(cacheDir.c_str(), 100);
      for (const auto& file : files) {
        std::string fullPath = cacheDir + "/" + std::string(file.c_str());
        SdMan.remove(fullPath.c_str());
        vTaskDelay(pdMS_TO_TICKS(5));
      }

      vTaskDelay(pdMS_TO_TICKS(20));
      cacheDeleted = SdMan.removeDir(cacheDir.c_str());
    }
  } else {
    cacheDeleted = true;
  }

  bool bookDeleted = false;
  if (!bookPath.empty() && SdMan.exists(bookPath.c_str())) {
    bookDeleted = SdMan.remove(bookPath.c_str());
  } else {
    bookDeleted = true;
  }

  const char* resultMsg;
  if (cacheDeleted && bookDeleted) {
    resultMsg = "Book deleted";
  } else if (!cacheDeleted && !bookDeleted) {
    resultMsg = "Delete failed";
  } else {
    resultMsg = "Partially deleted";
  }

  ScreenComponents::drawPopup(renderer, resultMsg);
  vTaskDelay(pdMS_TO_TICKS(1500));

  onGoBack();
}

/**
 * @brief Generates full book data
 */
void EpubActivity::generateFullData() {
  ViewportInfo info = calculateViewport();
  int totalSpineItems = epub->getSpineItemsCount();

  for (int i = 0; i < totalSpineItems; i++) {
    loadingProgress = (i * 100) / totalSpineItems;
    drawLoadingScreen();
    buildSection(i, info, false);
    vTaskDelay(10);
  }

  loadingProgress = 100;
  drawLoadingScreen();
}

/**
 * @brief Handles page turning logic
 * @param forward True for forward page turn, false for backward
 */
void EpubActivity::pageTurn(bool forward) {
  if (!epub) {
    updateRequired = true;
    return;
  }

  if (!section) {
    updateRequired = true;
    return;
  }

  if (section->pageCount == 0) {
    section.reset();
    updateRequired = true;
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    section->currentPage = 0;
  }

  bool needSectionReset = false;
  int newSpineIndex = currentSpineIndex;
  int newNextPageNumber = nextPageNumber;

  if (forward) {
    if (section->currentPage < section->pageCount - 1) {
      section->currentPage++;
    } else {
      int totalSpines = epub->getSpineItemsCount();
      if (currentSpineIndex < totalSpines - 1) {
        bookStats.totalChaptersRead++;
        newSpineIndex = currentSpineIndex + 1;
        newNextPageNumber = 0;
        needSectionReset = true;
      } else {
        newSpineIndex = totalSpines;
        needSectionReset = true;
      }
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else if (currentSpineIndex > 0) {
      newSpineIndex = currentSpineIndex - 1;
      newNextPageNumber = UINT16_MAX;
      needSectionReset = true;
    }
  }

  if (needSectionReset) {
    currentSpineIndex = newSpineIndex;
    nextPageNumber = newNextPageNumber;
    section.reset();
  }

  startPageTimer();
  updateRequired = true;
}

/**
 * @brief Main display task loop running on separate thread
 */
[[noreturn]] void EpubActivity::displayTaskLoop() {
  while (true) {
    if (pauseDisplayTaskForRebuild) {
      displayRebuildPausedAck = true;
      while (pauseDisplayTaskForRebuild) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      displayRebuildPausedAck = false;
      continue;
    }
    if (updateRequired) {
      updateRequired = false;
      renderScreen();
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Renders the current screen content
 */
void EpubActivity::renderScreen() {
  struct EpubScreenRenderGuard {
    volatile bool* flag;
    explicit EpubScreenRenderGuard(volatile bool* f) : flag(f) { *f = true; }
    ~EpubScreenRenderGuard() { *flag = false; }
    EpubScreenRenderGuard(const EpubScreenRenderGuard&) = delete;
    EpubScreenRenderGuard& operator=(const EpubScreenRenderGuard&) = delete;
  } renderBusyGuard(&epubScreenRenderBusy);

  if (!epub) return;

  renderer.clearScreen(0xFF);

  int totalSpine = epub->getSpineItemsCount();
  if (totalSpine <= 0) {
    return;
  }

  if (currentSpineIndex >= totalSpine) {
    displayBookStats();
    BOOK_STATE.setFinished(epub->getPath(), true);
    return;
  }

  if (currentSpineIndex < 0 || currentSpineIndex >= totalSpine) {
    currentSpineIndex = 0;
    nextPageNumber = 0;
    section.reset();
  }

  ViewportInfo info = calculateViewport();

  if (!section) {
    section = loadSection(currentSpineIndex, info);
    if (!section) {
      renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 300, "Error loading chapter", true,
                                EpdFontFamily::BOLD);
      updateRequired = true;
      return;
    }

    section->currentPage = (nextPageNumber == UINT16_MAX)          ? section->pageCount - 1
                           : (nextPageNumber < section->pageCount) ? nextPageNumber
                                                                   : 0;

    if (cachedChapterTotalPageCount > 0 && currentSpineIndex == cachedSpineIndex &&
        section->pageCount != cachedChapterTotalPageCount) {
      float progress = (float)section->currentPage / cachedChapterTotalPageCount;
      section->currentPage = std::min((int)(progress * section->pageCount), section->pageCount - 1);
      cachedChapterTotalPageCount = 0;
    }
  }

  if (section->pageCount == 0) {
    section.reset();
    currentSpineIndex = (currentSpineIndex + 1 < totalSpine) ? currentSpineIndex + 1 : totalSpine;
    nextPageNumber = 0;
    updateRequired = true;
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    section->currentPage = 0;
  }

  auto page = section->loadPageFromSectionFile();
  if (!page) {
    section.reset();
    updateRequired = true;
    return;
  }

  renderContents(std::move(page), info.totalMarginTop, info.totalMarginRight, info.totalMarginBottom,
                 info.totalMarginLeft);

  if (settingsDrawerVisible && settingsDrawer) settingsDrawer->render();
  if (menuDrawerVisible && menuDrawer) menuDrawer->render();

  saveProgress(currentSpineIndex, section->currentPage, section->pageCount);
}

/**
 * @brief Renders the page contents with margins and status bar
 * @param page Page to render
 * @param orientedMarginTop Top margin
 * @param orientedMarginRight Right margin
 * @param orientedMarginBottom Bottom margin
 * @param orientedMarginLeft Left margin
 */
void EpubActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                  const int orientedMarginRight, const int orientedMarginBottom,
                                  const int orientedMarginLeft) {
  if (!page) return;
  ReaderBitmapStyleGuard bitmapStyleGuard(renderer);
  renderer.resetBitmapGrayscaleDetection();
  const int fontId = bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);

  if (SETTINGS.readerSmartRefreshOnImages && page->hasImages() && !isBookmarking) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  if (SETTINGS.readerSmartRefreshOnImages && !page->hasImages() && lastPageHadImages && !isBookmarking) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  page->render(
    renderer, 
    fontId,
    headerFontId,
    orientedMarginLeft, 
    orientedMarginTop,
    false
  );

  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  if (isCurrentPageBookmarked()) {
    drawBookmarkIndicator();
  }

  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = bookSettings.refreshFrequency;
    lastPageHadImages = false;
    return;
  }

  pagesUntilFullRefresh--;

  const bool needsImageGrayscale =
      SETTINGS.readerImageGrayscale && page->hasImages() && renderer.needsBitmapGrayscale();

  // First push of the composed frame; grayscale pass (if any) expects FAST here, not HALF.
  renderer.displayBuffer();

  if (needsImageGrayscale) {
    const bool storedBwBuffer = renderer.storeBwBuffer();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
    if (storedBwBuffer) {
      renderer.restoreBwBuffer();
    } else {
      renderer.cleanupGrayscaleWithFrameBuffer();
    }
  }

  lastPageHadImages = page->hasImages();
}

/**
 * @brief Renders the status bar with configurable sections
 * @param orientedMarginRight Right margin
 * @param orientedMarginBottom Bottom margin
 * @param orientedMarginLeft Left margin
 */
void EpubActivity::renderStatusBar(const int orientedMarginRight, const int orientedMarginBottom,
                                   const int orientedMarginLeft) const {
  if (statusBar && section) {
    statusBar->render(section.get(), currentSpineIndex, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  }
}

/**
 * @brief Displays the book title on screen when cover is not available
 */
void EpubActivity::displayBookTitle() {
  renderer.clearScreen();

  std::string bookTitle = epub->getTitle();

  int maxWidth = renderer.getScreenWidth() * 0.6;

  int titleWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, bookTitle.c_str());

  if (titleWidth > maxWidth) {
    bookTitle = renderer.truncatedText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, bookTitle.c_str(), maxWidth);
  }

  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, renderer.getScreenHeight() / 2, bookTitle.c_str(), true,
                            EpdFontFamily::BOLD);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

/**
 * @brief Loads bookmarks from file
 */
void EpubActivity::loadBookmarks() {
  bookmarks.clear();

  std::string bookmarksPath = epub->getCachePath() + "/" + BOOKMARKS_FILENAME;
  FsFile f;

  if (SdMan.openFileForRead("ERS", bookmarksPath, f)) {
    uint32_t fileSize = f.fileSize();
    int numBookmarks = fileSize / sizeof(Bookmark);

    if (numBookmarks > 0 && numBookmarks <= MAX_BOOKMARKS) {
      bookmarks.resize(numBookmarks);
      f.read(bookmarks.data(), fileSize);

      bookmarks.erase(
          std::remove_if(bookmarks.begin(), bookmarks.end(), [](const Bookmark& b) { return !b.isValid(); }),
          bookmarks.end());
    }
    f.close();
  }
}

/**
 * @brief Saves bookmarks to file
 */
void EpubActivity::saveBookmarks() {
  std::string bookmarksPath = epub->getCachePath() + "/" + BOOKMARKS_FILENAME;
  FsFile f;

  if (SdMan.openFileForWrite("ERS", bookmarksPath, f)) {
    if (!bookmarks.empty()) {
      f.write(bookmarks.data(), bookmarks.size() * sizeof(Bookmark));
    } else {
      f.close();
      SdMan.remove(bookmarksPath.c_str());
      return;
    }
    f.close();
  }
}

/**
 * @brief Adds a bookmark at the current position
 */
void EpubActivity::addBookmark() {
  if (!section) return;
  isBookmarking = true;

  auto it = std::find_if(bookmarks.begin(), bookmarks.end(), [this](const Bookmark& bookmark) {
    return bookmark.spineIndex == currentSpineIndex && bookmark.pageNumber == section->currentPage;
  });

  if (it != bookmarks.end()) {
    bookmarks.erase(it);
    saveBookmarks();
    showBookmarkIndicator = false;
    updateRequired = true;
    return;
  }

  if (bookmarks.size() >= MAX_BOOKMARKS) {
    ScreenComponents::drawPopup(renderer, "Maximum bookmarks reached");
    return;
  }

  Bookmark bookmark;
  bookmark.spineIndex = currentSpineIndex;
  bookmark.pageNumber = section->currentPage;
  bookmark.pageCount = section->pageCount;
  bookmark.timestamp = static_cast<uint32_t>(time(nullptr));

  std::string title = getCurrentChapterTitle();
  strncpy(bookmark.chapterTitle, title.c_str(), sizeof(bookmark.chapterTitle) - 1);
  bookmark.chapterTitle[sizeof(bookmark.chapterTitle) - 1] = '\0';

  bookmarks.push_back(bookmark);
  saveBookmarks();

  showBookmarkIndicator = true;
  updateRequired = true;
}

/**
 * @brief Removes a bookmark at the specified index
 * @param index Index of the bookmark to remove
 */
void EpubActivity::removeBookmark(int index) {
  if (index >= 0 && index < static_cast<int>(bookmarks.size())) {
    bookmarks.erase(bookmarks.begin() + index);
    saveBookmarks();
  }
}

/**
 * @brief Checks if the current page is bookmarked
 * @return true if bookmarked, false otherwise
 */
bool EpubActivity::isCurrentPageBookmarked() const {
  if (!section) return false;

  return std::any_of(bookmarks.begin(), bookmarks.end(), [this](const Bookmark& bookmark) {
    return bookmark.spineIndex == currentSpineIndex && bookmark.pageNumber == section->currentPage;
  });
}

/**
 * @brief Navigates to a bookmarked position
 * @param index Index of the bookmark to navigate to
 */
void EpubActivity::goToBookmark(int index) {
  if (index >= 0 && index < static_cast<int>(bookmarks.size())) {
    const auto& bookmark = bookmarks[index];

    if (currentSpineIndex != bookmark.spineIndex) {
      currentSpineIndex = bookmark.spineIndex;
      nextPageNumber = bookmark.pageNumber;
      section.reset();
    } else if (section) {
      section->currentPage = bookmark.pageNumber;
    }

    updateRequired = true;
  }
}

/**
 * @brief Gets the title of the current chapter
 * @return Chapter title string
 */
std::string EpubActivity::getCurrentChapterTitle() const {
  int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex != -1) {
    return epub->getTocItem(tocIndex).title;
  }
  return "Chapter " + std::to_string(currentSpineIndex + 1);
}

/**
 * @brief Draws a bookmark indicator on the current page
 */
void EpubActivity::drawBookmarkIndicator() {
  const int bookmarkWidth = 15;
  const int bookmarkHeight = 25;
  const int bookmarkX = renderer.getScreenWidth() - bookmarkWidth - 15;
  const int bookmarkY = 15;
  const int notchDepth = bookmarkHeight / 4;
  const int centerX = bookmarkX + bookmarkWidth / 2;

  const int xPoints[5] = {bookmarkX, bookmarkX + bookmarkWidth, bookmarkX + bookmarkWidth, centerX, bookmarkX};
  const int yPoints[5] = {bookmarkY, bookmarkY, bookmarkY + bookmarkHeight, bookmarkY + bookmarkHeight - notchDepth,
                          bookmarkY + bookmarkHeight};

  renderer.fillPolygon(xPoints, yPoints, 5, true);
}

/**
 * @brief Loads book settings from file
 */
void EpubActivity::loadBookSettings() {
  if (epub) {
    bool loaded = bookSettings.loadFromFile(epub->getCachePath());
    if (!loaded) {
      bookSettings.loadFromGlobalSettings();
      bookSettings.useCustomSettings = false;
    }
  }
}

/**
 * @brief Saves book settings to file
 */
void EpubActivity::saveBookSettings() {
  std::string cachePath = epub->getCachePath();
  if (cachePath.empty()) {
    return;
  }

  bookSettings.saveToFile(cachePath);
}

void EpubActivity::beginExclusiveRebuildWithDisplayTask() {
  if (!displayTaskHandle) {
    return;
  }
  displayRebuildPausedAck = false;
  pauseDisplayTaskForRebuild = true;
  const uint32_t start = millis();
  while (!displayRebuildPausedAck) {
    vTaskDelay(pdMS_TO_TICKS(1));
    if (millis() - start > 3000U) {
      break;
    }
  }
}

void EpubActivity::endExclusiveRebuildWithDisplayTask() { pauseDisplayTaskForRebuild = false; }

/**
 * @brief Applies current book settings and rebuilds affected sections
 */
void EpubActivity::applyBookSettings() {
  int currentPage = 0;
  int currentSpine = currentSpineIndex;

  if (section) {
    currentPage = section->currentPage;
    cachedChapterTotalPageCount = section->pageCount;
    cachedSpineIndex = currentSpine;
  } else {
    currentPage = nextPageNumber;
    cachedChapterTotalPageCount = 0;
  }

  if (!epub) {
    return;
  }

  syncOrientationFromGlobalIfNeeded();
  setupOrientation();

  ViewportInfo info = calculateViewport();

  int totalSpineItems = epub->getSpineItemsCount();
  if (totalSpineItems <= 0) {
    return;
  }

  struct ExclusiveRebuildScope {
    EpubActivity* self;
    explicit ExclusiveRebuildScope(EpubActivity* s) : self(s) { self->beginExclusiveRebuildWithDisplayTask(); }
    ~ExclusiveRebuildScope() { self->endExclusiveRebuildWithDisplayTask(); }
    ExclusiveRebuildScope(const ExclusiveRebuildScope&) = delete;
    ExclusiveRebuildScope& operator=(const ExclusiveRebuildScope&) = delete;
  } exclusiveRebuild(this);

  int startSpine = std::max(0, currentSpine - 5);
  int endSpine = std::min(totalSpineItems - 1, currentSpine + 5);

  int total = endSpine - startSpine + 1;
  int rebuilt = 0;

  for (int spineIdx = startSpine; spineIdx <= endSpine; spineIdx++) {
    rebuilt++;

    if (rebuilt % 2 == 0) {
      char progressStr[32];
      snprintf(progressStr, sizeof(progressStr), "Updating %d/%d", rebuilt, total);
      ScreenComponents::drawPopup(renderer, progressStr);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    buildSection(spineIdx, info, false, true);
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (currentSpine < startSpine || currentSpine > endSpine) {
    vTaskDelay(pdMS_TO_TICKS(10));
    buildSection(currentSpine, info, false, true);
  }

  currentSpineIndex = currentSpine;
  nextPageNumber = currentPage;

  bookLayoutAppliedOrientation_ = bookSettings.orientation;
  updateRequired = true;
}

/**
 * @brief Initializes reading statistics
 */
void EpubActivity::initStats() {
  if (!epub) return;

  if (loadBookStats(epub->getCachePath().c_str(), bookStats)) {
    bookStats.sessionCount++;
  } else {
    bookStats.path = epub->getCachePath();
    bookStats.title = epub->getTitle();
    bookStats.author = epub->getAuthor();
    bookStats.totalReadingTimeMs = 0;
    bookStats.totalPagesRead = 0;
    bookStats.totalChaptersRead = 0;
    bookStats.lastReadTimeMs = millis();
    bookStats.progressPercent = 0;
    bookStats.lastSpineIndex = currentSpineIndex;
    bookStats.lastPageNumber = section ? section->currentPage : 0;
    bookStats.avgPageTimeMs = 0;
    bookStats.sessionCount = 1;
  }

  bookStats.lastReadTimeMs = millis();
  pageStartTime = millis();
  lastSaveTime = millis();
}

/**
 * @brief Starts the page timer for statistics
 */
void EpubActivity::startPageTimer() { pageStartTime = millis(); }

/**
 * @brief Ends the page timer and updates statistics
 */
void EpubActivity::endPageTimer() {
  if (pageStartTime == 0) return;

  uint32_t currentTime = millis();
  uint32_t timeSpent = currentTime - pageStartTime;

  if (timeSpent < 1000) {
    pageStartTime = 0;
    return;
  }

  if (section) {
    bookStats.totalReadingTimeMs += timeSpent;
    bookStats.totalPagesRead++;
    bookStats.lastReadTimeMs = currentTime;
    bookStats.lastSpineIndex = currentSpineIndex;
    bookStats.lastPageNumber = section->currentPage;

    if (section->pageCount > 0 && epub) {
      float spineProgress = static_cast<float>(section->currentPage) / section->pageCount;
      float bookProgressValue = epub->calculateProgress(currentSpineIndex, spineProgress) * 100.0f;
      bookStats.progressPercent = bookProgressValue;
    }

    if (bookStats.totalPagesRead > 0) {
      bookStats.avgPageTimeMs = bookStats.totalReadingTimeMs / bookStats.totalPagesRead;
    }

    uint32_t now = millis();
    if (now - lastSaveTime >= STATS_SAVE_INTERVAL_MS) {
      saveBookStats();
      lastSaveTime = now;
    }
  }

  pageStartTime = 0;
}

/**
 * @brief Saves reading statistics to file
 */
void EpubActivity::saveBookStats() {
  if (!epub) return;

  bookStats.lastReadTimeMs = millis();
  ::saveBookStats(epub->getCachePath().c_str(), bookStats);
}

void EpubActivity::displayBookStats() {
  if (!epub) return;
  renderer.clearScreen(0xff);
  BookReadingStats stats;
  bool hasStats = loadBookStats(epub->getCachePath().c_str(), stats);
  if (!hasStats) return;

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int VALUE_FONT = ATKINSON_HYPERLEGIBLE_18_FONT_ID;
  const int LABEL_FONT = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  int statsX = (screenW - 250) / 2;
  int statsY = (screenH - 300) / 2;
  int currentY = statsY;
  char buffer[32];

  renderer.drawText(ATKINSON_HYPERLEGIBLE_18_FONT_ID, statsX, statsY - 90, "End of book", true, EpdFontFamily::BOLD);

  uint32_t readingTime = stats.totalReadingTimeMs;
  std::string timeStr = formatTime(readingTime);
  renderer.drawText(VALUE_FONT, statsX, currentY, timeStr.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(LABEL_FONT, statsX, currentY + 45, "Reading Time", true);
  currentY += 87;

  uint32_t pagesRead = stats.totalPagesRead;
  snprintf(buffer, sizeof(buffer), "%u", pagesRead);
  renderer.drawText(VALUE_FONT, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.drawText(LABEL_FONT, statsX, currentY + 45, "Pages", true);
  currentY += 87;

  uint32_t chaptersRead = stats.totalChaptersRead;
  snprintf(buffer, sizeof(buffer), "%u", chaptersRead);
  renderer.drawText(VALUE_FONT, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.drawText(LABEL_FONT, statsX, currentY + 45, "Chapters", true);
  currentY += 87;

  uint32_t avgPageTime = stats.avgPageTimeMs;
  if (avgPageTime > 0) {
    snprintf(buffer, sizeof(buffer), "%us", avgPageTime / 1000);
  } else {
    snprintf(buffer, sizeof(buffer), "-");
  }
  renderer.drawText(VALUE_FONT, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.drawText(LABEL_FONT, statsX, currentY + 45, "Average / Page", true);

  currentY += 87;
  snprintf(buffer, sizeof(buffer), "%u", stats.sessionCount);
  renderer.drawText(VALUE_FONT, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.drawText(LABEL_FONT, statsX, currentY + 45, "Reading Sessions", true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

std::string EpubActivity::formatTime(uint32_t timeMs) {
  uint32_t seconds = timeMs / 1000;
  uint32_t minutes = seconds / 60;
  uint32_t hours = minutes / 60;

  if (hours > 0) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%uh %um", hours, minutes % 60);
    return std::string(buffer);
  } else if (minutes > 0) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%um", minutes);
    return std::string(buffer);
  } else {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%us", seconds);
    return std::string(buffer);
  }
}