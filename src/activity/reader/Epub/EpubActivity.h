#pragma once

/**
 * @file EpubActivity.h
 * @brief Public interface and types for EpubActivity.
 */

#include <Epub.h>
#include <Epub/Section.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "MenuDrawer.h"
#include "StatusBar.h"
#include "activity/ActivityWithSubactivity.h"
#include "state/BookSetting.h"
#include "state/BookProgress.h"
#include "SettingsDrawer.h"
#include "system/ScreenComponents.h"

#include "EpubAnnotationUi.h"
#include "EpubReadingStats.h"

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
 * Main activity for reading EPUB books.
 * Handles page navigation, bookmarks, settings, and reading statistics.
 */
class EpubActivity final : public ActivityWithSubactivity {
  friend class EpubAnnotationUi;

public:
    /**
     * Represents a bookmark in the book.
     */
    struct Bookmark {
        uint16_t spineIndex;      
        uint16_t pageNumber;      
        uint16_t pageCount;       
        char chapterTitle[64];    
        uint32_t timestamp;       
        
        /**
         * Validates if the bookmark contains reasonable data.
         * 
         * @return true if bookmark is valid
         */
        bool isValid() const { return spineIndex != 0xFFFF && pageNumber != 0xFFFF; }
    };

    /**
     * Constructs a new EpubActivity.
     * 
     * @param renderer Reference to the graphics renderer
     * @param mappedInput Reference to the input manager
     * @param epub Unique pointer to the EPUB document
     * @param onGoBack Callback for returning to previous activity
     * @param onGoToRecent Callback for navigating to recent books
     */
    explicit EpubActivity(GfxRenderer& renderer, 
                                MappedInputManager& mappedInput, 
                                std::unique_ptr<Epub> epub,
                                const std::function<void()>& onGoBack, 
                                const std::function<void()>& onGoToRecent);
    
    void onEnter() override;
    void onExit() override;
    void loop() override;
    /** Match Crosspoint-style reader: pump display from loop (no separate FreeRTOS render task). */
    bool skipLoopDelay() override { return true; }

private:
    int currentFontId;
    int nextFontId;
    bool isToggleClosed = false;
    bool settingsChanged = false;
    bool isBookmarking = false;
    bool isDoingSomethingHeavy = false;
    std::unique_ptr<Epub> epub;
    std::unique_ptr<Section> section = nullptr;
    std::unique_ptr<BookProgress> bookProgress = nullptr;
    std::unique_ptr<StatusBar> statusBar = nullptr;
    int currentSpineIndex = 0;
    int nextPageNumber = 0;
    int pagesUntilFullRefresh = 0;
    bool pendingPercentJump = false;
    float pendingSpineProgress = 0.0f;
    int cachedSpineIndex = 0;
    int cachedChapterTotalPageCount = 0;
    bool updateRequired = false;
    bool bookmarkLongPressProcessed = false;
    bool leftLongPressProcessed = false;
    int loadingProgress = 0;
    unsigned long lastAutoPageTurnTime = 0;
    
    const std::function<void()> onGoBack;
    const std::function<void()> onGoToRecent;

    static constexpr int MAX_BOOKMARKS = 200;
    static constexpr const char* BOOKMARKS_FILENAME = "bookmarks.bin";
    
    std::vector<Bookmark> bookmarks;
    bool showBookmarkIndicator = false;
    int lastPreloadedSpineIndex = -1;
    bool lastPageHadImages = false;
    /** Previous page: image union bbox at least half screen in both dimensions (for smart HALF refresh). */
    bool lastPageHadLargeImage = false;

    int lastGoodSpineIndex_ = 0;
    int lastGoodPageNumber_ = 0;
    bool chapterRecoveryAttempted_ = false;

    SettingsDrawer* settingsDrawer = nullptr;
    bool settingsDrawerVisible = false;
    MenuDrawer* menuDrawer = nullptr;
    bool menuDrawerVisible = false;
    BookSettings bookSettings;
    /** Last orientation value used for a full layout/section rebuild; used to detect drift after global sync. */
    uint8_t bookLayoutAppliedOrientation_ = 0xFF;
    bool leftButtonLongPressProcessed = false;

    EpubReadingStats readingStats_;

    /** @param clearFramebuffer When false, skips clearScreen before compositing (same-page annotation overlay refresh). */
    void renderScreen(bool clearFramebuffer = true);
    
    /**
     * Handles page turning logic for forward/backward navigation.
     * Manages chapter transitions and end-of-book detection.
     * 
     * @param forward True for forward page turn, false for backward
     */
    void pageTurn(bool forward);
    
    /**
     * Renders page contents with margins and status bar.
     * 
     * @param page Page to render
     * @param orientedMarginTop Top margin
     * @param orientedMarginRight Right margin
     * @param orientedMarginBottom Bottom margin
     * @param orientedMarginLeft Left margin
     */
    void renderContents(std::unique_ptr<Page> page, 
                        int orientedMarginTop, 
                        int orientedMarginRight,
                        int orientedMarginBottom, 
                        int orientedMarginLeft);
    
    /**
     * Renders the status bar with configurable sections.
     * 
     * @param orientedMarginRight Right margin
     * @param orientedMarginBottom Bottom margin
     * @param orientedMarginLeft Left margin
     */
    void renderStatusBar(int orientedMarginRight, 
                         int orientedMarginBottom, 
                         int orientedMarginLeft) const;
    
    /**
     * Saves current reading progress to file using BookProgress handler.
     * 
     * @param spineIndex Current spine index
     * @param currentPage Current page number
     * @param pageCount Total pages in current chapter
     */
    void saveProgress(int spineIndex, int currentPage, int pageCount, bool saveRecentNow = true);
    
    /**
     * Loads progress from file using BookProgress handler.
     */
    void loadProgress();
    
    /**
     * Toggles the menu drawer visibility.
     */
    void toggleMenuDrawer();
    
    /**
     * Toggles the settings drawer visibility.
     */
    void toggleSettingsDrawer();
    
    /**
     * Callback when a chapter is selected from TOC.
     *
     * @param spineIndex The spine index to navigate to
     */
    void onTocChapterSelected(int spineIndex);

    /** User picked a bookmark from the reader menu drawer (same UX as TOC). */
    void onBookmarkDrawerSelected(int storageIndex);

    /** User picked a footnote line from the reader menu drawer. */
    void onFootnoteDrawerSelected(int storageIndex);

    /** User picked an annotated page from the reader menu drawer (storageIndex encodes spine/page). */
    void onAnnotationDrawerSelected(int storageIndex);

    void goToAnnotationPage(int spineIndex, int pageNumber);
    
    /**
     * Deletes the book cache.
     */
    void deleteCache();
    
    /**
     * Go home.
     */
    void goHome();
    
    /**
     * Deletes the reading progress.
     */
    void deleteProgress();
    
    /**
     * Deletes the entire book.
     */
    void deleteBook();
    
    /**
     * Generates full book data.
     */
    void generateFullData();
    void regenerateThumbnail();

    /** Opens KOReader sync as a sub-activity (from menu). */
    void openKOReaderSyncFromMenu();

    /** Opens percent selection as a sub-activity (from menu). */
    void openPercentSelectionFromMenu();
    void jumpToPercent(int percent);
        
    void displayBookTitle();
    void drawLoadingScreen();
    void preloadNextSection();

    /** Hides reader menu and settings drawers (if open). Optionally repaints the reader (skip during error popups). */
    void dismissMenuDrawerForBlockingWork(bool repaintReaderScreen = true);

    /** Close drawers (if open), then show a centered popup message. */
    void readerPopup(const char* message);

    /** After a failed chapter load: popup, revert once to last good chapter, then clear cache and exit if still broken. */
    void handleChapterLoadFailure();

    /** Close drawers (if open), then show the bottom loading progress panel. */
    ScreenComponents::LoadingProgressLayout loadingProgressShow(const char* message, int progressPercent0to100);
    
    void loadBookmarks();
    void saveBookmarks();
    void addBookmark();
    void removeBookmark(int index);
    bool isCurrentPageBookmarked() const;
    void goToBookmark(int index);
    std::string getCurrentChapterTitle() const;
    void drawBookmarkIndicator();

    EpubAnnotationUi annUi_;
    
    /**
     * Applies current book settings and rebuilds affected sections.
     */
    void applyBookSettings();
    
    void saveBookSettings();
    void loadBookSettings();
    
    void initStats();
    void maybeCommitReadingSessionCount();
    void startPageTimer();
    void endPageTimer();
    void saveBookStats();
    
    /**
     * Calculates the viewport dimensions based on current settings.
     * 
     * @return ViewportInfo structure containing viewport dimensions and settings
     */
    ViewportInfo calculateViewport();
    
    /**
     * Builds a section file for a given spine index.
     * 
     * @param spineIndex Index of the spine to build
     * @param info Viewport information for rendering
     * @param showProgress Whether to show progress during building
     * @param skipImages If true, skip processing new images and only use existing cached images
     * @return true if successful, false otherwise
     */
    bool buildSection(int spineIndex, const ViewportInfo& info, bool showProgress = false, bool skipImages = false);
    
    /**
     * Loads a section for a given spine index.
     * 
     * @param spineIndex Index of the spine to load
     * @param info Viewport information for rendering
     * @return Unique pointer to the loaded section
     */
    std::unique_ptr<Section> loadSection(int spineIndex, const ViewportInfo& info);
    
    void setupOrientation();
    /** Copies device reading orientation into book settings when the book follows global defaults. */
    void syncOrientationFromGlobalIfNeeded();
    /** Settings drawer callback: keep renderer, drawer, and menu layout in sync while editing. */
    void onBookSettingsLiveLayoutSync();
    void ensureThumbnailExists();
    void displayCoverOrTitle();
    void loadCurrentSection();
    void preloadChapters();
    void updateExternalState();
    void fastPath();
    void slowPath();
    void displayBookStats();
};
