#pragma once

#include "GfxRenderer.h"
#include "system/MappedInputManager.h"
#include <functional>
#include <vector>
#include <string>

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

    using ActionCallback = std::function<void(MenuAction)>;
    using DismissCallback = std::function<void()>;
    using TocSelectionCallback = std::function<void(int spineIndex)>;

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

private:
    /**
     * @brief Renders the drawer with specified refresh mode
     * @param mode Display refresh mode
     */
    void renderWithRefresh(HalDisplay::RefreshMode mode);
    
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
    
    /**
     * @brief Draws the TOC background with drawer effect
     */
    void drawTocBackground();
    
    /**
     * @brief Handles input when TOC is shown
     * @param input Reference to the input manager (const to avoid modification)
     */
    void handleTocInput(const MappedInputManager& input);
    
    /**
     * @brief Exits TOC view and returns to main menu
     */
    void exitToc();
    
    /**
     * @brief Gets the number of TOC items that fit on screen
     * @return Number of items per page
     */
    int getTocPageItems() const;
    
    GfxRenderer& renderer;
    ActionCallback onAction;
    DismissCallback onDismiss;
    TocSelectionCallback tocSelectionCallback;
    
    std::string bookTitle;
    Epub* epub = nullptr;
    
    struct MenuItem {
        std::string label;
        MenuAction action;
    };
    
    std::vector<MenuItem> menuItems;
    
    int drawerHeight;
    int drawerY;
    int itemHeight;
    int itemsPerPage;
    int selectedIndex;
    int scrollOffset;
    bool visible;
    bool dismissed;
    uint32_t lastInputTime;
    
    // TOC display state
    bool showingToc = false;
    bool isFromToc = false;
    int tocSelectedIndex = 0;
    int tocScrollOffset = 0;
    int tocDrawerHeight = 0;      // Height of TOC drawer (80% of screen)
    int tocDrawerY = 0;           // Y position of TOC drawer
};