#pragma once

/**
 * @file CategorySettingsActivity.h
 * @brief Public interface and types for CategorySettingsActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../Menu.h"
#include "activity/ActivityWithSubactivity.h"
#include "state/SystemSetting.h"
#include "system/UiTheme.h"

class SystemSetting;

enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, SEPARATOR, INFO };

enum class GroupType {
  NONE,
  FONT,
  LAYOUT,
  READER_CONTROLS,
  SYSTEM,
  STATUS_BAR,
  DEVICE_DISPLAY,
  CLOCK,
  DEVICE_BUTTONS,
  DEVICE_ADVANCED,
  DEVICE_ACTIONS,
  IMAGE,
};

struct ValueRange {
  uint8_t min;
  uint8_t max;
  uint8_t step;

  /** Constructs a zeroed value range. */
  ValueRange() : min(0), max(0), step(0) {}
  /** Constructs a value range with the given min, max, and step. */
  ValueRange(uint8_t minVal, uint8_t maxVal, uint8_t stepVal) : min(minVal), max(maxVal), step(stepVal) {}
};

struct SettingInfo {
  const char* name;
  SettingType type;
  uint8_t SystemSetting::* valuePtr;
  std::vector<std::string> enumValues;
  std::vector<uint8_t> enumOptionValues;
  ValueRange valueRange;
  GroupType group;

  /** Constructs an empty separator-typed setting info. */
  SettingInfo()
      : name(nullptr), type(SettingType::SEPARATOR), valuePtr(nullptr), valueRange(), group(GroupType::NONE) {}

  /** Constructs a setting info with a name, type, backing member pointer, and group. */
  SettingInfo(const char* n, SettingType t, uint8_t SystemSetting::* ptr, GroupType g)
      : name(n), type(t), valuePtr(ptr), valueRange(), group(g) {}

  /** Constructs a setting info with enum option values. */
  SettingInfo(const char* n, SettingType t, uint8_t SystemSetting::* ptr, const std::vector<std::string>& values,
              GroupType g)
      : name(n), type(t), valuePtr(ptr), enumValues(values), valueRange(), group(g) {}

  /** Constructs a setting info with enum labels and explicit stored values. */
  SettingInfo(const char* n, SettingType t, uint8_t SystemSetting::* ptr, const std::vector<std::string>& values,
              const std::vector<uint8_t>& optionValues, GroupType g)
      : name(n), type(t), valuePtr(ptr), enumValues(values), enumOptionValues(optionValues), valueRange(), group(g) {}

  /** Constructs a setting info with a numeric value range. */
  SettingInfo(const char* n, SettingType t, uint8_t SystemSetting::* ptr, const ValueRange& range, GroupType g)
      : name(n), type(t), valuePtr(ptr), valueRange(range), group(g) {}

  /** Creates a toggle-type setting info bound to a boolean member. */
  static SettingInfo Toggle(const char* name, uint8_t SystemSetting::* ptr, GroupType group = GroupType::NONE) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::TOGGLE;
    info.valuePtr = ptr;
    info.valueRange = ValueRange();
    info.group = group;
    return info;
  }

  /** Creates an enum-type setting info bound to a member with a list of option labels. */
  static SettingInfo Enum(const char* name, uint8_t SystemSetting::* ptr, const std::vector<std::string>& values,
                          GroupType group = GroupType::NONE) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::ENUM;
    info.valuePtr = ptr;
    info.enumValues = values;
    info.valueRange = ValueRange();
    info.group = group;
    return info;
  }

  /** Creates an enum setting whose option indexes map to explicit stored values. */
  static SettingInfo Enum(const char* name, uint8_t SystemSetting::* ptr, const std::vector<std::string>& values,
                          const std::vector<uint8_t>& optionValues, GroupType group = GroupType::NONE) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::ENUM;
    info.valuePtr = ptr;
    info.enumValues = values;
    info.enumOptionValues = optionValues;
    info.valueRange = ValueRange();
    info.group = group;
    return info;
  }

  /** Creates an action-type setting info that triggers a handler when selected. */
  static SettingInfo Action(const char* name, GroupType group = GroupType::NONE) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::ACTION;
    info.valuePtr = nullptr;
    info.valueRange = ValueRange();
    info.group = group;
    return info;
  }

  /** Creates a numeric value-type setting info bound to a member with a min/max/step range. */
  static SettingInfo Value(const char* name, uint8_t SystemSetting::* ptr, const ValueRange& valueRange,
                           GroupType group = GroupType::NONE) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::VALUE;
    info.valuePtr = ptr;
    info.valueRange = valueRange;
    info.group = group;
    return info;
  }

  /** Creates a separator row used to divide and expand/collapse a group. */
  static SettingInfo Separator(const char* name, GroupType group) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::SEPARATOR;
    info.valueRange = ValueRange();
    info.group = group;
    return info;
  }

  /** Read-only row; `value` is shown on the right (e.g. firmware version). */
  static SettingInfo Info(const char* name, const char* value, GroupType group = GroupType::NONE) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::INFO;
    info.valuePtr = nullptr;
    info.enumValues = {std::string(value ? value : "")};
    info.group = group;
    return info;
  }
};

extern const int LIST_ITEM_HEIGHT;

class CategorySettingsActivity final : public ActivityWithSubactivity, public Menu {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  bool halfRefreshOnLoadApplied_ = false;
  bool selectorOpen = false;
  uint8_t selectorMode = 0;
  int selectedIndex = 0;
  int scrollOffset = 0;
  int itemsPerPage = 0;
  int selectorSourceIndex = -1;
  int selectorSelectedIndex = 0;
  int selectorScrollOffset = 0;
  const char* categoryName;
  std::vector<SettingInfo> settingsStorage;
  const SettingInfo* settingsList;
  int settingsCount;
  const std::function<void()> onGoBack;
  const std::function<void()> onIndexLibrary;
  const std::function<void()> onAboutPanel;
  const char* backButtonLabel;
  const std::function<void()> onTabRecent;
  const std::function<void()> onTabLibrary;
  const std::function<void()> onTabSync;
  const std::function<void()> onTabStatistics;

  struct MenuEntry {
    const char* name;
    SettingType type;
    uint8_t SystemSetting::* valuePtr;
    ValueRange valueRange;
    GroupType group;
    const SettingInfo* setting;
    std::function<const char*()> getValueText;
    std::function<void(int)> change;
  };

  static constexpr size_t kGroupCount = static_cast<size_t>(GroupType::IMAGE) + 1;
  static constexpr size_t groupIndex(const GroupType group) { return static_cast<size_t>(group); }
  bool isGroupExpanded(GroupType group) const { return groupExpanded_[groupIndex(group)]; }

  std::vector<MenuEntry> menuItems;
  std::vector<std::string> selectorOptions;
  std::vector<std::string> selectorValues;
  std::array<bool, kGroupCount> groupExpanded_{};

  /** FreeRTOS task entry point that forwards to displayTaskLoop. */
  static void taskTrampoline(void* param);
  /** Background task loop that redraws the menu when required. */
  void displayTaskLoop();
  /** Draws the category settings screen, including any open selector overlay. */
  void render();
  /** Rebuilds the flattened menu item list based on current group expansion state. */
  void setupMenu();
  /** Applies a delta change to the currently selected menu item. */
  void applyChange(int delta);
  /** Opens the value/enum picker overlay for the currently selected item. */
  void openSelectorForSelected();
  /** Opens the sleep image picker overlay populated from the sleep image index. */
  void openSleepImageSelector();
  /** Rebuilds the on-disk sleep image index from files under /sleep. */
  bool rebuildSleepImageIndex();
  /** Loads the sleep image index rows into the selector option lists. */
  void loadSleepImageIndexRows();
  /** Applies the currently selected sleep image option to settings. */
  void applySleepImageSelection();
  /** Moves the selector highlight by delta rows, wrapping and scrolling as needed. */
  void moveSelector(int delta);
  /** Moves the selector highlight by whole pages. */
  void selectorPage(int delta);
  /** Closes the selector overlay, optionally saving the chosen option. */
  void closeSelector(bool save);
  /** Draws the value/enum/sleep-image selector overlay. */
  void renderSelectorOverlay();
  /** Returns the option index corresponding to the entry's current value. */
  int selectedOptionIndex(const MenuEntry& entry) const;
  /** Applies the chosen option index to the entry's backing setting. */
  void applySelectedOption(MenuEntry& entry, int optionIndex);
  /** Toggles expansion state of a settings group. */
  void toggleGroup(GroupType group);

  /** Handles tab-bar navigation to another top-level activity. */
  void navigateToSelectedMenu() override;

 public:
  CategorySettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* categoryName,
                           std::vector<SettingInfo> settings, const std::function<void()>& onGoBack,
                           std::function<void()> indexLibraryHandler = nullptr,
                           std::function<void()> aboutPanelHandler = nullptr, const char* backLabel = nullptr,
                           std::function<void()> tabNavigateRecent = nullptr,
                           std::function<void()> tabNavigateLibrary = nullptr,
                           std::function<void()> tabNavigateSync = nullptr,
                           std::function<void()> tabNavigateStatistics = nullptr)
      : ActivityWithSubactivity("CategorySettings", renderer, mappedInput),
        Menu(),
        categoryName(categoryName),
        settingsStorage(std::move(settings)),
        settingsList(settingsStorage.data()),
        settingsCount(static_cast<int>(settingsStorage.size())),
        onGoBack(onGoBack),
        onIndexLibrary(std::move(indexLibraryHandler)),
        onAboutPanel(std::move(aboutPanelHandler)),
        backButtonLabel(backLabel),
        onTabRecent(std::move(tabNavigateRecent)),
        onTabLibrary(std::move(tabNavigateLibrary)),
        onTabSync(std::move(tabNavigateSync)),
        onTabStatistics(std::move(tabNavigateStatistics)) {
    tabSelectorIndex = 2;
    const int contentTop = mainContentTop() + TAB_BAR_HEIGHT;
    const int contentBottom =
        INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) : renderer.getScreenHeight() - 80;
    itemsPerPage = (contentBottom - contentTop) / UiTheme::DRAWER_LIST_ITEM_HEIGHT;
    if (itemsPerPage < 1) itemsPerPage = 1;

    groupExpanded_.fill(false);
  }
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
