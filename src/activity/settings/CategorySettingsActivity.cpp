#include "CategorySettingsActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>

#include <algorithm>
#include <cstring>

#include "CalibreSettingsActivity.h"
#include "ClearCacheActivity.h"
#include "KOReaderSettingsActivity.h"
#include "OtaUpdateActivity.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

/**
 * @brief Static trampoline function for task creation
 */
void CategorySettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<CategorySettingsActivity*>(param);
  self->displayTaskLoop();
}

/**
 * @brief Initialize activity state and create display task
 */
void CategorySettingsActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();

  selectedIndex = 0;
  scrollOffset = 0;
  updateRequired = true;

  setupMenu();

  xTaskCreate(&CategorySettingsActivity::taskTrampoline, "CategorySettingsActivityTask", 4096, this, 1,
              &displayTaskHandle);
}

/**
 * @brief Clean up resources and delete display task
 */
void CategorySettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
}

/**
 * @brief Toggles expansion state of a group
 */
void CategorySettingsActivity::toggleGroup(GroupType group) {
  groupExpanded[group] = !groupExpanded[group];
  setupMenu();

  // Find the separator for this group and set selected index to it
  for (size_t i = 0; i < menuItems.size(); i++) {
    if (menuItems[i].type == SettingType::SEPARATOR && menuItems[i].group == group) {
      selectedIndex = i;
      if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
      } else if (selectedIndex >= scrollOffset + itemsPerPage) {
        scrollOffset = selectedIndex - itemsPerPage + 1;
      }
      break;
    }
  }
  updateRequired = true;
}

/**
 * @brief Sets up the menu structure based on expansion states
 */
void CategorySettingsActivity::setupMenu() {
  menuItems.clear();

  for (int i = 0; i < settingsCount; i++) {
    const auto& setting = settingsList[i];

    if (setting.type == SettingType::SEPARATOR) {
      MenuEntry entry;
      entry.name = setting.name;
      entry.type = SettingType::SEPARATOR;
      entry.group = setting.group;
      entry.uint8Ptr = nullptr;
      entry.intPtr = nullptr;
      entry.stringPtr = nullptr;
      entry.enumValues.clear();
      entry.valueRange = ValueRange(0, 0, 0);
      entry.getValueText = [this, group = setting.group]() -> const char* {
        static char indicator[4];
        bool expanded = false;
        auto it = groupExpanded.find(group);
        if (it != groupExpanded.end()) {
          expanded = it->second;
        }
        snprintf(indicator, sizeof(indicator), "%s", expanded ? "-" : "+");
        return indicator;
      };
      entry.change = [](int) {};
      menuItems.push_back(entry);
    } else {
      // Check if this item should be visible based on group expansion
      bool shouldShow = (setting.group == GroupType::NONE);
      if (!shouldShow) {
        auto it = groupExpanded.find(setting.group);
        shouldShow = (it != groupExpanded.end() && it->second);
      }
      
      if (shouldShow) {
        MenuEntry entry;
        entry.name = setting.name;
        entry.type = setting.type;
        entry.uint8Ptr = setting.uint8Ptr;
        entry.intPtr = setting.intPtr;
        entry.stringPtr = setting.stringPtr;
        entry.enumValues = setting.enumValues;
        entry.valueRange = setting.valueRange;
        entry.group = setting.group;
        entry.dynamicOptionsGetter = setting.dynamicOptionsGetter;

        if (setting.type == SettingType::TOGGLE) {
          entry.getValueText = [this, setting]() -> const char* {
            return (SETTINGS.*(setting.uint8Ptr)) ? "ON" : "OFF";
          };
          entry.change = [this, setting](int) {
            SETTINGS.*(setting.uint8Ptr) = !(SETTINGS.*(setting.uint8Ptr));
            SETTINGS.saveToFile();
            updateRequired = true;
          };
        }
        else if (setting.type == SettingType::ENUM) {
          entry.getValueText = [this, setting]() -> const char* {
            int index = SETTINGS.*(setting.uint8Ptr);
            if (index >= 0 && index < (int)setting.enumValues.size()) {
              return setting.enumValues[index].c_str();
            }
            return "Unknown";
          };
          entry.change = [this, setting](int delta) {
            int current = SETTINGS.*(setting.uint8Ptr);
            int newVal = current + delta;
            if (newVal < 0) newVal = setting.enumValues.size() - 1;
            if (newVal >= (int)setting.enumValues.size()) newVal = 0;
            SETTINGS.*(setting.uint8Ptr) = newVal;
            SETTINGS.saveToFile();
            updateRequired = true;
          };
        }
        else if (setting.type == SettingType::DYNAMIC_ENUM) {
          entry.getValueText = [this, setting]() -> const char* {
            static std::string cachedValue;
            if (setting.stringPtr) {
              cachedValue = SETTINGS.*(setting.stringPtr);
              return cachedValue.c_str();
            }
            return "Unknown";
          };
          entry.change = [this, setting](int delta) {
            if (!setting.stringPtr || !setting.dynamicOptionsGetter) return;
            
            auto options = setting.dynamicOptionsGetter();
            if (options.empty()) return;
            
            // Find current index
            std::string currentValue = SETTINGS.*(setting.stringPtr);
            int currentIndex = 0;
            for (size_t i = 0; i < options.size(); i++) {
              if (options[i] == currentValue) {
                currentIndex = i;
                break;
              }
            }
            
            // Apply delta
            int newIndex = currentIndex + delta;
            if (newIndex < 0) newIndex = options.size() - 1;
            if (newIndex >= (int)options.size()) newIndex = 0;
            
            SETTINGS.*(setting.stringPtr) = options[newIndex];
            SETTINGS.saveToFile();
            updateRequired = true;
            
            // If font family changed, refresh the menu to update font size options
            if (strcmp(setting.name, "Font Family") == 0) {
              setupMenu();
            }
          };
        }
        else if (setting.type == SettingType::VALUE) {
          entry.getValueText = [this, setting]() -> const char* {
            static char buffer[32];
            if (setting.intPtr) {
              int value = SETTINGS.*(setting.intPtr);
              snprintf(buffer, sizeof(buffer), "%d", value);
            } else if (setting.uint8Ptr) {
              int value = SETTINGS.*(setting.uint8Ptr);
              snprintf(buffer, sizeof(buffer), "%d", value);
            } else {
              snprintf(buffer, sizeof(buffer), "0");
            }
            return buffer;
          };
          entry.change = [this, setting](int delta) {
            int newVal = 0;
            if (setting.intPtr) {
              int current = SETTINGS.*(setting.intPtr);
              newVal = current + (delta * setting.valueRange.step);
              if (newVal < setting.valueRange.min) newVal = setting.valueRange.max;
              if (newVal > setting.valueRange.max) newVal = setting.valueRange.min;
              SETTINGS.*(setting.intPtr) = newVal;
            } else if (setting.uint8Ptr) {
              int current = SETTINGS.*(setting.uint8Ptr);
              newVal = current + (delta * setting.valueRange.step);
              if (newVal < setting.valueRange.min) newVal = setting.valueRange.max;
              if (newVal > setting.valueRange.max) newVal = setting.valueRange.min;
              SETTINGS.*(setting.uint8Ptr) = newVal;
            }
            SETTINGS.saveToFile();
            updateRequired = true;
          };
        }
        else if (setting.type == SettingType::ACTION) {
          entry.getValueText = []() -> const char* { 
            return "→"; 
          };
          entry.change = [this, setting](int) {
            // Handle actions
            if (strcmp(setting.name, "KOReader Sync") == 0) {
              exitActivity();
              enterNewActivity(new KOReaderSettingsActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
            }
            else if (strcmp(setting.name, "OPDS Browser") == 0) {
              exitActivity();
              enterNewActivity(new CalibreSettingsActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
            }
            else if (strcmp(setting.name, "Clear Cache") == 0) {
              exitActivity();
              enterNewActivity(new ClearCacheActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
            }
            else if (strcmp(setting.name, "Check for updates") == 0) {
              exitActivity();
              enterNewActivity(new OtaUpdateActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
            }
            updateRequired = true;
          };
        }

        menuItems.push_back(entry);
      }
    }
  }
}

/**
 * @brief Applies a delta change to the currently selected menu item
 */
void CategorySettingsActivity::applyChange(int delta) {
  if (selectedIndex < 0 || selectedIndex >= (int)menuItems.size()) return;
  const auto& selected = menuItems[selectedIndex];
  if (selected.type != SettingType::SEPARATOR) {
    selected.change(delta);
  }
}

/**
 * @brief Main loop handling input and state updates
 */
void CategorySettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  const bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);
  const bool leftPressed = mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool rightPressed = mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool confirmPressed = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);

  // Handle tab navigation
  if (leftPressed) {
    int newTabIndex = (tabSelectorIndex - 1 + TAB_COUNT) % TAB_COUNT;
    tabSelectorIndex = newTabIndex;

    if (newTabIndex != 2) {
      SETTINGS.saveToFile();
      navigateToSelectedMenu();
      return;
    }
  }

  if (rightPressed) {
    int newTabIndex = (tabSelectorIndex + 1) % TAB_COUNT;
    tabSelectorIndex = newTabIndex;

    if (newTabIndex != 2) {
      SETTINGS.saveToFile();
      navigateToSelectedMenu();
      return;
    }
  }

  if (backPressed) {
    SETTINGS.saveToFile();
    onGoBack();
    return;
  }

  bool needRedraw = false;

  if (upPressed) {
    if (selectedIndex > 0) {
      selectedIndex--;
      if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
      needRedraw = true;
    }
  } else if (downPressed) {
    if (selectedIndex < (int)menuItems.size() - 1) {
      selectedIndex++;
      int maxScroll = std::max(0, (int)menuItems.size() - itemsPerPage);
      if (selectedIndex > scrollOffset + itemsPerPage - 1) {
        scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      }
      needRedraw = true;
    }
  } else if (leftPressed) {
    applyChange(-1);
    needRedraw = true;
  } else if (rightPressed) {
    applyChange(1);
    needRedraw = true;
  } else if (confirmPressed) {
    if (selectedIndex >= 0 && selectedIndex < (int)menuItems.size()) {
      const auto& selected = menuItems[selectedIndex];
      if (selected.type == SettingType::SEPARATOR) {
        toggleGroup(selected.group);
        needRedraw = true;
      } else {
        applyChange(1);
        needRedraw = true;
      }
    }
  }

  if (needRedraw) {
    updateRequired = true;
  }
}

/**
 * @brief Display task loop for periodic rendering
 */
void CategorySettingsActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      if (renderingMutex) {
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        render();
        xSemaphoreGive(renderingMutex);
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Render the category settings screen
 */
void CategorySettingsActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderTabBar(renderer);

  const int headerY = TAB_BAR_HEIGHT;
  const int headerHeight = TAB_BAR_HEIGHT;
  int headerTextY = headerY + (headerHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;

  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, headerTextY - 10, categoryName, true, EpdFontFamily::BOLD);

  const char* subtitleText = "Adjust your preferences.";
  int subtitleY = headerY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, pageWidth, dividerY);

  const int startY = dividerY;
  const int itemHeight = LIST_ITEM_HEIGHT;

  int visibleCount = 0;
  for (int i = 0; i < itemsPerPage && (i + scrollOffset) < (int)menuItems.size(); i++) {
    int index = i + scrollOffset;
    const auto& entry = menuItems[index];

    // Skip separators with empty names
    if (entry.type == SettingType::SEPARATOR && (entry.name == nullptr || entry.name[0] == '\0')) {
      continue;
    }

    int itemY = startY + (visibleCount * itemHeight);
    bool isSelected = (index == selectedIndex);

    if (entry.type == SettingType::SEPARATOR) {
      if (isSelected) {
        renderer.fillRect(0, itemY, pageWidth, itemHeight);
      }

      int textX = 15;
      int textY = itemY + (itemHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, entry.name, !isSelected);

      const char* indicator = entry.getValueText();
      if (indicator && indicator[0] != '\0') {
        int indicatorW = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, indicator);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageWidth - indicatorW - 30, textY, indicator, !isSelected);
      }

      renderer.drawLine(0, itemY + itemHeight - 1, pageWidth, itemY + itemHeight - 1, true);
      visibleCount++;
      continue;
    }

    if (isSelected) {
      renderer.fillRect(0, itemY, pageWidth, itemHeight);
    }

    int textX = 23;
    int textY = itemY + (itemHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, entry.name, !isSelected);

    const char* val = entry.getValueText();
    if (val && val[0] != '\0') {
      int valW = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, val);
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageWidth - valW - 30, textY, val, !isSelected);
    }

    renderer.drawLine(0, itemY + itemHeight - 1, pageWidth, itemY + itemHeight - 1, true);
    visibleCount++;
  }

  if ((int)menuItems.size() > itemsPerPage) {
    int listHeight = itemsPerPage * itemHeight;
    int thumbH = (itemsPerPage * listHeight) / menuItems.size();
    int thumbY = startY + (scrollOffset * listHeight) / menuItems.size();
    renderer.fillRect(pageWidth - 4, thumbY, 2, thumbH, true);
  }

  const auto labels = mappedInput.mapLabels("« Back", "Toggle", "«", "»");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}