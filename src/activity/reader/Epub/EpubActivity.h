#pragma once
#include <Epub.h>
#include <Epub/Section.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <vector>
#include <memory>
#include <string>

#include "MenuDrawer.h"
#include "StatusBar.h"
#include "activity/ActivityWithSubactivity.h"
#include "state/BookSetting.h"
#include "state/BookProgress.h"
#include "SettingsDrawer.h"
#include "state/Statistics.h"

struct ViewportInfo;

/**
 * Main activity for reading EPUB books.
 * Handles page navigation, bookmarks, settings, and reading statistics.
 */
class EpubActivity final : public ActivityWithSubactivity {
public:
    /**
     * Represents a bookmark in the book.
     */
    struct Bookmark {
        uint16_t spineIndex;      // Chapter index in spine
        uint16_t pageNumber;      // Page number within chapter
        uint16_t pageCount;       // Total pages in chapter for validation
        char chapterTitle[64];    // Title of the chapter
        uint32_t timestamp;       // When bookmark was created
        
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

private:
    bool isToggleClosed = false;
    bool settingsChanged = false;
    bool isBookmarking = false;
    bool isDoingSomethingHeavy = false;
    std::unique_ptr<Epub> epub;
    std::unique_ptr<Section> section = nullptr;
    std::unique_ptr<BookProgress> bookProgress = nullptr;
    std::unique_ptr<StatusBar> statusBar = nullptr;
    TaskHandle_t displayTaskHandle = nullptr;
    int currentSpineIndex = 0;
    int nextPageNumber = 0;
    int pagesUntilFullRefresh = 0;
    int cachedSpineIndex = 0;
    int cachedChapterTotalPageCount = 0;
    bool updateRequired = false;
    bool bookmarkLongPressProcessed = false;
    bool leftLongPressProcessed = false;
    int loadingProgress = 0;
    
    const std::function<void()> onGoBack;
    const std::function<void()> onGoToRecent;

    static constexpr int MAX_BOOKMARKS = 200;
    static constexpr const char* BOOKMARKS_FILENAME = "bookmarks.bin";
    
    std::vector<Bookmark> bookmarks;
    bool showBookmarkIndicator = false;
    int lastPreloadedSpineIndex = -1;
    bool lastPageHadImages = false;

    SettingsDrawer* settingsDrawer = nullptr;
    bool settingsDrawerVisible = false;
    MenuDrawer* menuDrawer = nullptr;
    bool menuDrawerVisible = false;
    BookSettings bookSettings;
    bool leftButtonLongPressProcessed = false;

    BookReadingStats bookStats;
    uint32_t pageStartTime;
    uint32_t lastSaveTime;
    
    static void taskTrampoline(void* param);
    [[noreturn]] void displayTaskLoop();
    void renderScreen();
    
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
    void saveProgress(int spineIndex, int currentPage, int pageCount);
    
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
     * Shows the bookmark menu.
     */
    void showBookmarkMenu();
    
    /**
     * Callback when a chapter is selected from TOC.
     * 
     * @param spineIndex The spine index to navigate to
     */
    void onTocChapterSelected(int spineIndex);
    
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
        
    void displayBookTitle();
    void drawLoadingScreen();
    void preloadNextSection();
    
    void loadBookmarks();
    void saveBookmarks();
    void addBookmark();
    void removeBookmark(int index);
    bool isCurrentPageBookmarked() const;
    void goToBookmark(int index);
    void showBookmarkMenuActivity();
    std::string getCurrentChapterTitle() const;
    void drawBookmarkIndicator();
    
    /**
     * Applies current book settings and rebuilds affected sections.
     */
    void applyBookSettings();
    
    void saveBookSettings();
    void loadBookSettings();
    
    void initStats();
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
    void ensureThumbnailExists();
    void displayCoverOrTitle();
    void loadCurrentSection();
    void preloadChapters();
    void updateExternalState();
    void fastPath();
    void slowPath();
    void displayBookStats();
    std::string formatTime(uint32_t timeMs);
};