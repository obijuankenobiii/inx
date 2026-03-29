#pragma once

#include <functional>
#include <vector>
#include <string>
#include <map>
#include <GfxRenderer.h>
#include "system/MappedInputManager.h"
#include "state/BookSetting.h"

/**
 * @class SettingsDrawer
 * @brief A drawer UI component for modifying book reading settings
 * 
 * Provides an expandable/collapsible menu interface for adjusting font,
 * layout, system, and status bar settings while reading.
 */
class SettingsDrawer {
public:
    /**
     * @brief Constructs a new SettingsDrawer
     * @param renderer Reference to the graphics renderer
     * @param settings Reference to book settings to modify
     * @param onSettingsChanged Callback triggered when settings are changed
     */
    SettingsDrawer(GfxRenderer& renderer, BookSettings& settings, std::function<void()> onSettingsChanged);
    
    /**
     * @brief Destructor
     */
    ~SettingsDrawer();
    
    /**
     * @brief Shows the settings drawer
     */
    void show();
    
    /**
     * @brief Hides the settings drawer
     */
    void hide();
    
    /**
     * @brief Renders the settings drawer
     */
    void render();
    
    /**
     * @brief Handles input for the settings drawer
     * @param input Reference to the input manager
     */
    void handleInput(MappedInputManager& input);
    
    /**
     * @brief Checks if the drawer has been dismissed
     * @return true if dismissed, false otherwise
     */
    bool isDismissed() const { return dismissed; }
    
    /**
     * @brief Checks if settings have been updated
     * @return true if settings were changed, false otherwise
     */
    bool shouldUpdate() const { return settingsUpdated; }
    
    /**
     * @brief Clears the settings updated flag
     */
    void clearUpdateFlag() { settingsUpdated = false; }
    
private:
    /**
     * @enum GroupType
     * @brief Types of setting groups for organization
     */
    enum class GroupType {
        FONT,        ///< Font-related settings
        LAYOUT,      ///< Layout and spacing settings
        CONTROLS,    ///< System and control settings
        STATUS_BAR   ///< Status bar configuration
    };
    
    /**
     * @enum MenuItem
     * @brief All available menu items in the settings drawer
     */
    enum class MenuItem {
        // Separators
        Separator,           ///< Generic separator for Font, Layout, Controls groups
        StatusBarSeparator,  ///< Special separator for Status Bar group
        
        // Font Group
        FontFamily,          ///< Font style selection
        FontSize,            ///< Font size selection
        
        // Layout Group
        LineSpacing,         ///< Line spacing adjustment
        Alignment,           ///< Paragraph alignment
        ExtraParagraphSpacing, ///< Additional spacing between paragraphs
        ScreenMargin,        ///< Screen margin size
        ReadingOrientation,  ///< Screen orientation
        
        // Controls Group
        Hyphenation,         ///< Hyphenation toggle
        AntiAliasing,        ///< Text anti-aliasing toggle
        RefreshRate,         ///< Display refresh frequency
        ChapterSkip,         ///< Long-press chapter skip toggle
        NavigationLock,      ///< Navigation lock setting
        
        // Status Bar Group
        StatusBarLeft,       ///< Left status bar section content
        StatusBarMiddle,     ///< Middle status bar section content
        StatusBarRight       ///< Right status bar section content
    };
    
    /**
     * @struct MenuEntry
     * @brief Represents a single menu item in the settings drawer
     */
    struct MenuEntry {
        MenuItem item;                                ///< Type of menu item
        GroupType group;                              ///< Group this item belongs to
        const char* name;                             ///< Display name
        std::function<const char*(const BookSettings&)> getValueText; ///< Gets current value as text
        std::function<void(BookSettings&, int)> change; ///< Applies delta change to setting
    };
    
    GfxRenderer& renderer;                    ///< Graphics renderer reference
    BookSettings& settings;                    ///< Book settings reference
    std::function<void()> onSettingsChanged;   ///< Settings change callback
    
    bool visible = false;                      ///< Drawer visibility state
    bool dismissed = false;                    ///< Drawer dismissed state
    int selectedIndex = 0;                      ///< Currently selected menu index
    int drawerHeight;                           ///< Height of the drawer
    int drawerY;                                ///< Y-coordinate of drawer top
    int itemHeight = 60;                         ///< Height of each menu item
    int itemsPerPage;                            ///< Number of items visible per page
    int scrollOffset = 0;                         ///< Current scroll position
    uint32_t lastInputTime;                       ///< Last input timestamp for debouncing
    
    bool settingsUpdated = false;                 ///< Flag indicating settings were changed
    
    std::map<GroupType, bool> groupExpanded;      ///< Expansion state for each group
    std::vector<MenuEntry> menuItems;             ///< Current menu items
    
    /**
     * @brief Renders the drawer with specified refresh mode
     * @param mode Display refresh mode to use
     */
    void renderWithRefresh(HalDisplay::RefreshMode mode);
    
    /**
     * @brief Sets up the menu structure based on expansion states
     */
    void setupMenu();
    
    /**
     * @brief Draws the background panel
     */
    void drawBackground();
    
    /**
     * @brief Draws all menu items
     */
    void drawMenuItems();
    
    /**
     * @brief Draws the scroll indicator
     */
    void drawScrollIndicator();
    
    /**
     * @brief Applies a delta change to the selected menu item
     * @param delta Change amount (-1 or 1)
     */
    void applyChange(int delta);
    
    /**
     * @brief Toggles expansion state of a group
     * @param group Group to toggle
     */
    void toggleGroup(GroupType group);
    
    /**
     * @brief Gets display name for a status bar item
     * @param item The status bar item
     * @return String representation for display
     */
    static const char* getStatusBarItemName(StatusBarItem item);
};