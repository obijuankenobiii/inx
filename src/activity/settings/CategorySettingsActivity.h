#pragma once

/**
 * @file CategorySettingsActivity.h
 * @brief Public interface and types for CategorySettingsActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>
#include <map>

#include "activity/ActivityWithSubactivity.h"
#include "../Menu.h"
#include "state/SystemSetting.h"

class SystemSetting;

enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, SEPARATOR };

enum class GroupType {
    NONE,
    FONT,
    LAYOUT,
    READER_CONTROLS,
    SYSTEM,
    STATUS_BAR,
    DEVICE_DISPLAY,
    DEVICE_BUTTONS,
    DEVICE_ADVANCED,
    DEVICE_ACTIONS,
    IMAGE,
};

struct ValueRange {
    uint8_t min;
    uint8_t max;
    uint8_t step;
    
    ValueRange() : min(0), max(0), step(0) {}
    ValueRange(uint8_t minVal, uint8_t maxVal, uint8_t stepVal) : min(minVal), max(maxVal), step(stepVal) {}
};

struct SettingInfo {
    const char* name;
    SettingType type;
    uint8_t SystemSetting::* valuePtr;
    std::vector<std::string> enumValues;
    ValueRange valueRange;
    GroupType group;

    SettingInfo() : name(nullptr), type(SettingType::SEPARATOR), valuePtr(nullptr), valueRange(), group(GroupType::NONE) {}
    
    SettingInfo(const char* n, SettingType t, uint8_t SystemSetting::* ptr, GroupType g)
        : name(n), type(t), valuePtr(ptr), valueRange(), group(g) {}
    
    SettingInfo(const char* n, SettingType t, uint8_t SystemSetting::* ptr, const std::vector<std::string>& values, GroupType g)
        : name(n), type(t), valuePtr(ptr), enumValues(values), valueRange(), group(g) {}
    
    SettingInfo(const char* n, SettingType t, uint8_t SystemSetting::* ptr, const ValueRange& range, GroupType g)
        : name(n), type(t), valuePtr(ptr), valueRange(range), group(g) {}

    static SettingInfo Toggle(const char* name, uint8_t SystemSetting::* ptr, GroupType group = GroupType::NONE) {
        SettingInfo info;
        info.name = name;
        info.type = SettingType::TOGGLE;
        info.valuePtr = ptr;
        info.valueRange = ValueRange();
        info.group = group;
        return info;
    }

    static SettingInfo Enum(const char* name, uint8_t SystemSetting::* ptr, const std::vector<std::string>& values, GroupType group = GroupType::NONE) {
        SettingInfo info;
        info.name = name;
        info.type = SettingType::ENUM;
        info.valuePtr = ptr;
        info.enumValues = values;
        info.valueRange = ValueRange();
        info.group = group;
        return info;
    }

    static SettingInfo Action(const char* name, GroupType group = GroupType::NONE) { 
        SettingInfo info;
        info.name = name;
        info.type = SettingType::ACTION;
        info.valuePtr = nullptr;
        info.valueRange = ValueRange();
        info.group = group;
        return info;
    }

    static SettingInfo Value(const char* name, uint8_t SystemSetting::* ptr, const ValueRange& valueRange, GroupType group = GroupType::NONE) {
        SettingInfo info;
        info.name = name;
        info.type = SettingType::VALUE;
        info.valuePtr = ptr;
        info.valueRange = valueRange;
        info.group = group;
        return info;
    }
    
    static SettingInfo Separator(const char* name, GroupType group) {
        SettingInfo info;
        info.name = name;
        info.type = SettingType::SEPARATOR;
        info.valueRange = ValueRange();
        info.group = group;
        return info;
    }
};

extern const int LIST_ITEM_HEIGHT;

class CategorySettingsActivity final : public ActivityWithSubactivity, public Menu {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  int selectedIndex = 0;
  int scrollOffset = 0;
  int itemsPerPage = 0;
  const char* categoryName;
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
      std::vector<std::string> enumValues;
      ValueRange valueRange;
      GroupType group;
      std::function<const char*()> getValueText;
      std::function<void(int)> change;
  };
  
  std::vector<MenuEntry> menuItems;
  std::map<GroupType, bool> groupExpanded;
  
  static void taskTrampoline(void* param);
  void displayTaskLoop();
  void render();
  void setupMenu();
  void applyChange(int delta);
  void toggleGroup(GroupType group);
  
  void navigateToSelectedMenu() override;

 public:
  CategorySettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* categoryName,
                           const SettingInfo* settingsList, int settingsCount, const std::function<void()>& onGoBack,
                           std::function<void()> indexLibraryHandler = nullptr,
                           std::function<void()> aboutPanelHandler = nullptr, const char* backLabel = nullptr,
                           std::function<void()> tabNavigateRecent = nullptr,
                           std::function<void()> tabNavigateLibrary = nullptr,
                           std::function<void()> tabNavigateSync = nullptr,
                           std::function<void()> tabNavigateStatistics = nullptr)
      : ActivityWithSubactivity("CategorySettings", renderer, mappedInput),
        Menu(),
        categoryName(categoryName),
        settingsList(settingsList),
        settingsCount(settingsCount),
        onGoBack(onGoBack),
        onIndexLibrary(std::move(indexLibraryHandler)),
        onAboutPanel(std::move(aboutPanelHandler)),
        backButtonLabel(backLabel),
        onTabRecent(std::move(tabNavigateRecent)),
        onTabLibrary(std::move(tabNavigateLibrary)),
        onTabSync(std::move(tabNavigateSync)),
        onTabStatistics(std::move(tabNavigateStatistics)) {
    tabSelectorIndex = 2;
    itemsPerPage = (renderer.getScreenHeight() - TAB_BAR_HEIGHT * 2 - 100) / LIST_ITEM_HEIGHT;
    if (itemsPerPage < 1) itemsPerPage = 1;

    groupExpanded[GroupType::FONT] = false;
    groupExpanded[GroupType::LAYOUT] = false;
    groupExpanded[GroupType::SYSTEM] = false;
    groupExpanded[GroupType::STATUS_BAR] = false;
    groupExpanded[GroupType::READER_CONTROLS] = false;
    groupExpanded[GroupType::DEVICE_DISPLAY] = false;
    groupExpanded[GroupType::DEVICE_BUTTONS] = false;
    groupExpanded[GroupType::DEVICE_ADVANCED] = false;
    groupExpanded[GroupType::DEVICE_ACTIONS] = false;
    groupExpanded[GroupType::IMAGE] = false;
  }
  void onEnter() override;
  void onExit() override;
  void loop() override;
};