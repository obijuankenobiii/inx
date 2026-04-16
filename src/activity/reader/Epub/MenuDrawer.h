#pragma once

#include "GfxRenderer.h"
#include "system/MappedInputManager.h"

#include <functional>
#include <string>
#include <vector>

// Forward declaration
class Epub;

/**
 * @brief Menu drawer that slides up from the bottom of the screen
 */
class MenuDrawer {
public:
    enum class MenuAction {
        SHOW_BOOKMARKS,
        SELECT_CHAPTER,
        GO_HOME,
        DELETE_CACHE,
        DELETE_PROGRESS,
        DELETE_BOOK,
        GENERATE_FULL_DATA
    };

    /** One row in the bookmark drawer (same role as a TOC line). */
    struct BookmarkNavItem {
        std::string label;
        int storageIndex = 0;
        bool isCurrentPosition = false;
    };

    using ActionCallback = std::function<void(MenuAction)>;
    using DismissCallback = std::function<void()>;
    using TocSelectionCallback = std::function<void(int spineIndex)>;
    using BookmarkListProvider = std::function<std::vector<BookmarkNavItem>()>;
    using BookmarkSelectCallback = std::function<void(int storageIndex)>;
    using BookmarkDeleteCallback = std::function<void(int storageIndex)>;

    /**
     * @brief Constructs a new MenuDrawer
     * @param renderer Reference to the graphics renderer
     * @param onAction Callback when a menu action is selected
     * @param onDismiss Callback when the drawer is dismissed
     */
    MenuDrawer(GfxRenderer& renderer, ActionCallback onAction, DismissCallback onDismiss = nullptr);
    
    ~MenuDrawer();

    /**
     * @brief Shows the menu drawer
     */
    void show();
    
    /**
     * @brief Hides the menu drawer
     */
    void hide();
    
    /**
     * @brief Checks if the drawer is visible
     * @return true if visible
     */
    bool isVisible() const { return visible; }
    
    /**
     * @brief Checks if the drawer has been dismissed
     * @return true if dismissed
     */
    bool isDismissed() const { return dismissed; }
    
    /**
     * @brief Renders the menu drawer
     */
    void render();
    
    /**
     * @brief Handles input for the menu drawer
     * @param input Reference to the input manager
     */
    void handleInput(MappedInputManager& input);
    
    /**
     * @brief Sets the book title to display in the header
     * @param title Book title
     */
    void setBookTitle(const std::string& title) { bookTitle = title; }
    
    /**
     * @brief Sets the EPUB reader for TOC access
     * @param epub Pointer to the EPUB reader
     */
    void setEpub(Epub* epub) { this->epub = epub; }
    
    /**
     * @brief Sets the callback for TOC chapter selection
     * @param callback Function to call when a chapter is selected
     */
    void setTocSelectionCallback(TocSelectionCallback callback) { tocSelectionCallback = callback; }

    void setBookmarkListProvider(BookmarkListProvider provider) { bookmarkListProvider = std::move(provider); }

    void setBookmarkSelectCallback(BookmarkSelectCallback callback) { bookmarkSelectCallback = std::move(callback); }

    void setBookmarkDeleteCallback(BookmarkDeleteCallback callback) { bookmarkDeleteCallback = std::move(callback); }

    /** Used for layout-aware bookmark drawer button labels (Up / Del). */
    void setMappedInputForHints(MappedInputManager* input) { mappedInputForHints = input; }

    /** Recompute drawer geometry after renderer orientation or screen size changes. */
    void relayoutForRendererChange();

private:
    /**
     * @brief Renders the drawer with specified refresh mode
     * @param mode Display refresh mode
     */
    void renderWithRefresh();

    /**
     * @brief Draws the background panel
     */
    void drawBackground();
    
    /**
     * @brief Draws all menu items
     */
    void drawMenuItems();
    
    /**
     * @brief Draws scroll indicator when needed
     */
    void drawScrollIndicator();
    
    /**
     * @brief Renders the Table of Contents view as a drawer
     */
    void renderToc();

    void renderBookmarks();

    /**
     * @brief Draws the TOC background with drawer effect
     */
    void drawTocBackground();

    /**
     * @brief Handles input when TOC is shown
     * @param input Reference to the input manager (const to avoid modification)
     */
    void handleTocInput(const MappedInputManager& input);

    void handleBookmarksInput(const MappedInputManager& input);

    /**
     * @brief Exits TOC view and returns to main menu
     */
    void exitToc();

    void exitBookmarks();

    void refreshBookmarkEntriesFromProvider();

    /**
     * @brief Gets the number of TOC items that fit on screen
     * @return Number of items per page
     */
    int getTocPageItems() const;
    
    GfxRenderer& renderer;
    ActionCallback onAction;
    DismissCallback onDismiss;
    TocSelectionCallback tocSelectionCallback;
    BookmarkListProvider bookmarkListProvider;
    BookmarkSelectCallback bookmarkSelectCallback;
    BookmarkDeleteCallback bookmarkDeleteCallback;
    MappedInputManager* mappedInputForHints = nullptr;

    std::string bookTitle;
    Epub* epub = nullptr;
    
    struct MenuItem {
        std::string label;
        MenuAction action;
    };
    
    std::vector<MenuItem> menuItems;
    
    int drawerHeight;
    int drawerY;
    /** Left edge and width of drawer panel (full width in portrait, right half in landscape). */
    int drawerX = 0;
    int drawerWidth = 0;
    int tocDrawerX = 0;
    int tocDrawerWidth = 0;
    int itemHeight;
    int itemsPerPage;
    int selectedIndex;
    int scrollOffset;
    bool visible;
    bool dismissed;
    uint32_t lastInputTime;
    
    // TOC display state
    bool showingToc = false;
    bool showingBookmarks = false;
    bool isFromToc = false;
    int tocSelectedIndex = 0;
    int tocScrollOffset = 0;
    int tocDrawerHeight = 0;      // Height of TOC drawer (80% of screen in portrait, full in landscape)
    int tocDrawerY = 0;           // Y position of TOC drawer

    void syncLayoutFromRenderer();
    void drawDrawerHintRow(const char* btn1, const char* btn2, const char* btn3, const char* btn4);
    static bool isLandscapeDrawer(const GfxRenderer& gfx);

    std::vector<BookmarkNavItem> bookmarkEntries;
    int bookmarkSelectedIndex = 0;
};