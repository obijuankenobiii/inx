#pragma once

/**
 * @file SettingsDrawer.h
 * @brief Public interface and types for SettingsDrawer.
 */

#include <GfxRenderer.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "state/BookSetting.h"
#include "system/MappedInputManager.h"

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

  /** Recompute drawer geometry and menu rows after renderer orientation or screen size changes. */
  void relayoutForRendererChange();

  /** Enables Navigation → Button Layout for bottom hint row (see MappedInputManager::mapLabels). */
  void setMappedInputForHints(MappedInputManager* input) { mappedInputForHints_ = input; }

  /**
   * @brief Renders the drawer inside a fixed sub-region instead of the default full-screen overlay.
   *
   * Used by the preset editor to place the settings list in the bottom half of the screen. While an
   * embedded region is set, the drawer does NOT push its own display refresh or draw button hints —
   * the host composites the rest of the screen and pushes once via the invalidate callback.
   */
  void setEmbeddedRegion(int x, int y, int w, int h);

  /** Largest height <= maxHeight that fits a whole number of menu rows (no dead space below the list). */
  int snapEmbeddedHeight(int maxHeight) const;

  /** Host callback invoked (instead of displayBuffer) whenever the embedded drawer needs the screen pushed. */
  void setEmbeddedInvalidate(std::function<void()> cb) { onEmbeddedInvalidate_ = std::move(cb); }

  /** True while an embedded region is active. */
  bool isEmbedded() const { return embedded_; }

 private:
  /**
   * @enum GroupType
   * @brief Types of setting groups for organization
   */
  enum class GroupType {
    FONT,       ///< Font-related settings
    LAYOUT,     ///< Layout and spacing settings
    IMAGE,      ///< Book bitmap appearance (global reader image options)
    CONTROLS,   ///< System and control settings
    STATUS_BAR  ///< Status bar configuration
  };
  static constexpr size_t kGroupCount = 5;
  static constexpr size_t groupIndex(const GroupType group) { return static_cast<size_t>(group); }
  bool isGroupExpanded(GroupType group) const { return groupExpanded_[groupIndex(group)]; }

  /**
   * @enum MenuItem
   * @brief All available menu items in the settings drawer
   */
  enum class MenuItem {

    Separator,           ///< Generic separator for Font, Layout, Controls groups
    StatusBarSeparator,  ///< Special separator for Status Bar group
    PresetPicker,        ///< Per-book: pick and immediately apply a saved preset

    FontFamily,  ///< Font style selection
    FontSize,    ///< Font size selection

    LineHeight,             ///< Line height (% of natural, 10-200)
    TextSpace,              ///< Word spacing (% of natural, 10-200)
    Alignment,              ///< Paragraph alignment
    ExtraParagraphSpacing,  ///< Additional spacing between paragraphs
    ParagraphCssIndent,     ///< CSS `text-indent` on/off (labeled "Indent" in UI)
    ScreenMargin,           ///< Screen margin size
    ReadingOrientation,     ///< Screen orientation

    Hyphenation,        ///< Hyphenation toggle
    BionicReading,      ///< Bionic Reading toggle
    AntiAliasing,       ///< Text anti-aliasing toggle
    RefreshRate,        ///< Display refresh frequency
    ReaderPowerButton,  ///< Reader short power button behavior
    ChapterSkip,        ///< Long-press chapter skip toggle
    NavigationLock,     ///< Navigation lock setting

    StatusBarLeft,    ///< Left status bar section content
    StatusBarMiddle,  ///< Middle status bar section content
    StatusBarRight,   ///< Right status bar section content

    ReaderImageGrayscale,     ///< Global: EPUB image grayscale pass
    ReaderSmartImageRefresh,  ///< Global: half refresh on image pages
    PageAutoTurn
  };

  /**
   * @struct MenuEntry
   * @brief Represents a single menu item in the settings drawer
   */
  struct MenuEntry {
    MenuItem item;                                                 ///< Type of menu item
    GroupType group;                                               ///< Group this item belongs to
    const char* name;                                              ///< Display name
    std::function<const char*(const BookSettings&)> getValueText;  ///< Gets current value as text
    std::function<void(BookSettings&, int)> change;                ///< Applies delta change to setting
  };

  GfxRenderer& renderer;                    ///< Graphics renderer reference
  BookSettings& settings;                   ///< Book settings reference
  std::function<void()> onSettingsChanged;  ///< Settings change callback
  MappedInputManager* mappedInputForHints_ = nullptr;

  bool embedded_ = false;                       ///< Render within a fixed sub-region (preset editor)
  std::function<void()> onEmbeddedInvalidate_;  ///< Host push callback used instead of displayBuffer when embedded
  int presetPickIndex_ = 0;                     ///< Highlighted preset for the per-book PresetPicker row
  bool presetAppliedInDrawer_ = false;          ///< True after a preset has been applied during this drawer session

  bool visible = false;    ///< Drawer visibility state
  bool dismissed = false;  ///< Drawer dismissed state
  int selectedIndex = 0;   ///< Currently selected menu index
  int drawerHeight;        ///< Height of the drawer
  int drawerY;             ///< Y-coordinate of drawer top
  int drawerX = 0;         ///< Left edge (right half in landscape)
  int drawerWidth = 0;     ///< Panel width
  int itemHeight = 60;     ///< Height of each menu item
  int itemsPerPage;        ///< Number of items visible per page
  int scrollOffset = 0;    ///< Current scroll position
  uint32_t lastInputTime;  ///< Last input timestamp for debouncing

  bool settingsUpdated = false;  ///< Flag indicating settings were changed

  std::array<bool, kGroupCount> groupExpanded_{};  ///< Expansion state for each group, no heap nodes.
  std::vector<MenuEntry> menuItems;                ///< Current menu items

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

  void drawMenuItemRow(int visibleRow, int menuIndex);

  /**
   * @brief Draws the scroll indicator
   */
  void drawScrollIndicator();

  void clearScrollIndicatorArea();

  void syncLayoutFromRenderer();

  void refreshSelectionRows(int previousIndex, bool redrawScrollIndicator);

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
