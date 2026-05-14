/**
 * @file CategorySettingsActivity.cpp
 * @brief Definitions for CategorySettingsActivity.
 */

#include "CategorySettingsActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "CalibreSettingsActivity.h"
#include "ClearCacheActivity.h"
#include "SleepImagePickerActivity.h"
#include "KOReaderSettingsActivity.h"
#include "OtaUpdateActivity.h"
#include "ReaderFontSettingsDraw.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

#include <EpdFontFamily.h>

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

  halfRefreshOnLoadApplied_ = false;
  selectedIndex = 0;
  scrollOffset = 0;
  updateRequired = true;

  if (categoryName != nullptr && strcmp(categoryName, "Reader") == 0) {
    FontManager::scanSDFonts("/fonts", true);
    FontManager::clampReaderFontFamilySlot(SETTINGS.fontFamily);
  }

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

void CategorySettingsActivity::navigateToSelectedMenu() {
  if (tabSelectorIndex == 0 && onTabRecent) {
    onTabRecent();
    return;
  }
  if (tabSelectorIndex == 1 && onTabLibrary) {
    onTabLibrary();
    return;
  }
  if (tabSelectorIndex == 3 && onTabSync) {
    onTabSync();
    return;
  }
  if (tabSelectorIndex == 4 && onTabStatistics) {
    onTabStatistics();
    return;
  }
}

/**
 * @brief Toggles expansion state of a group
 */
void CategorySettingsActivity::toggleGroup(GroupType group) {
  groupExpanded[group] = !groupExpanded[group];
  setupMenu();

  
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
      entry.valuePtr = nullptr;
      entry.enumValues.clear();
      entry.valueRange = {0, 0, 0};
      const GroupType sepGroup = setting.group;
      entry.getValueText = [this, sepGroup]() -> const char* {
        static char indicator[4];
        snprintf(indicator, sizeof(indicator), "%s", groupExpanded[sepGroup] ? "-" : "+");
        return indicator;
      };
      entry.change = [](int) {};
      menuItems.push_back(entry);
    } else {
      if (setting.group == GroupType::NONE || groupExpanded[setting.group]) {
        MenuEntry entry;
        entry.name = setting.name;
        entry.type = setting.type;
        entry.valuePtr = setting.valuePtr;
        entry.enumValues = setting.enumValues;
        entry.valueRange = setting.valueRange;
        entry.group = setting.group;

        if (setting.type == SettingType::INFO) {
          const SettingInfo infoRow = setting;
          entry.getValueText = [infoRow]() -> const char* {
            return infoRow.enumValues.empty() ? "" : infoRow.enumValues[0].c_str();
          };
          entry.change = [](int) {};
        }
        if (setting.type == SettingType::TOGGLE) {
          entry.getValueText = [this, setting]() -> const char* {
            return (SETTINGS.*(setting.valuePtr)) ? "ON" : "OFF";
          };
          entry.change = [this, setting](int) {
            SETTINGS.*(setting.valuePtr) = !(SETTINGS.*(setting.valuePtr));
            SETTINGS.saveToFile();
            updateRequired = true;
          };
        }
        if (setting.type == SettingType::ENUM) {
          if (setting.name != nullptr && strcmp(setting.name, "Font Family") == 0) {
            entry.enumValues = FontManager::readerFontFamilyEnumLabels();
            entry.getValueText = [this, setting]() -> const char* {
              thread_local std::string tls;
              tls = FontManager::readerFontFamilyLabel(SETTINGS.*(setting.valuePtr));
              return tls.c_str();
            };
            entry.change = [this, setting](int delta) {
              int current = SETTINGS.*(setting.valuePtr);
              const int n = static_cast<int>(FontManager::readerFontFamilyOptionCount());
              if (n <= 0) {
                return;
              }
              int newVal = current + delta;
              if (newVal < 0) {
                newVal = n - 1;
              }
              if (newVal >= n) {
                newVal = 0;
              }
              SETTINGS.*(setting.valuePtr) = static_cast<uint8_t>(newVal);
              SETTINGS.saveToFile();
              updateRequired = true;
            };
          } else {
            entry.getValueText = [this, setting]() -> const char* {
              int index = SETTINGS.*(setting.valuePtr);
              if (index >= 0 && index < (int)setting.enumValues.size()) {
                return setting.enumValues[index].c_str();
              }
              return "Unknown";
            };
            entry.change = [this, setting](int delta) {
              int current = SETTINGS.*(setting.valuePtr);
              int newVal = current + delta;
              if (newVal < 0) newVal = setting.enumValues.size() - 1;
              if (newVal >= (int)setting.enumValues.size()) newVal = 0;
              SETTINGS.*(setting.valuePtr) = newVal;
              SETTINGS.saveToFile();
              updateRequired = true;
            };
          }
        }
        if (setting.type == SettingType::VALUE) {
          entry.getValueText = [this, setting]() -> const char* {
            static char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d", SETTINGS.*(setting.valuePtr));
            return buffer;
          };
          entry.change = [this, setting](int delta) {
            int current = SETTINGS.*(setting.valuePtr);
            int newVal = current + (delta * setting.valueRange.step);
            if (newVal < setting.valueRange.min) newVal = setting.valueRange.max;
            if (newVal > setting.valueRange.max) newVal = setting.valueRange.min;
            SETTINGS.*(setting.valuePtr) = newVal;
            SETTINGS.saveToFile();
            updateRequired = true;
          };
        }
        if (setting.type == SettingType::ACTION) {
          entry.getValueText = []() -> const char* { return ""; };
          entry.change = [this, setting](int) {
            if (strcmp(setting.name, "Index your library") == 0) {
              if (onIndexLibrary) {
                onIndexLibrary();
              }
              return;
            }
            if (strcmp(setting.name, "About") == 0) {
              if (onAboutPanel) {
                onAboutPanel();
              }
              return;
            }
            if (strcmp(setting.name, "KOReader Sync") == 0) {
              exitActivity();
              enterNewActivity(new KOReaderSettingsActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
            }
            if (strcmp(setting.name, "OPDS Browser") == 0) {
              exitActivity();
              enterNewActivity(new CalibreSettingsActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
            }
            if (strcmp(setting.name, "Reset device") == 0) {
              exitActivity();
              enterNewActivity(new ClearCacheActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
            }
            if (strcmp(setting.name, "Choose sleep image") == 0) {
              exitActivity();
              enterNewActivity(new SleepImagePickerActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
            }
            if (strcmp(setting.name, "Check for updates") == 0) {
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
  if (selected.type == SettingType::SEPARATOR) return;
  
  if (selected.type == SettingType::ACTION) return;
  selected.change(delta);
}

/**
 * @brief Main loop handling input and state updates
 */
void CategorySettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    updateRequired = true;
    return;
  }

  const bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);
  const bool leftPressed = mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool rightPressed = mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool confirmPressed = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);

  
  if (leftPressed) {
    int newTabIndex = (tabSelectorIndex - 1 + TAB_COUNT) % TAB_COUNT;
    tabSelectorIndex = newTabIndex;

    if (newTabIndex != 2) {
      SETTINGS.saveToFile();
      navigateToSelectedMenu();
      return;
    }
    
    updateRequired = true;
    return;
  }

  if (rightPressed) {
    int newTabIndex = (tabSelectorIndex + 1) % TAB_COUNT;
    tabSelectorIndex = newTabIndex;

    if (newTabIndex != 2) {
      SETTINGS.saveToFile();
      navigateToSelectedMenu();
      return;
    }
    updateRequired = true;
    return;
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
  } else if (confirmPressed) {
    if (selectedIndex >= 0 && selectedIndex < (int)menuItems.size()) {
      const auto& selected = menuItems[selectedIndex];
      if (selected.type == SettingType::SEPARATOR) {
        toggleGroup(selected.group);
        needRedraw = true;
      } else if (selected.type == SettingType::ACTION) {
        selected.change(0);
        needRedraw = true;
      } else if (selected.type == SettingType::INFO) {
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
        if (!halfRefreshOnLoadApplied_) {
          halfRefreshOnLoadApplied_ = true;
          SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Settings);
        }
        xSemaphoreGive(renderingMutex);
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Render the category settings screen
 */
void CategorySettingsActivity::render() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  renderTabBar(renderer);

  const int headerY = TAB_BAR_HEIGHT;
  const int headerHeight = TAB_BAR_HEIGHT;
  const int headerTextY =
      headerY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;

  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, headerTextY, categoryName, true, EpdFontFamily::BOLD);

  const int dividerY = headerY + headerHeight;
  renderer.line.render(0, dividerY, pageWidth, dividerY);

  const int startY = dividerY;
  const int itemHeight = LIST_ITEM_HEIGHT;

  int visibleCount = 0;
  for (int i = 0; i < itemsPerPage && (i + scrollOffset) < (int)menuItems.size(); i++) {
    int index = i + scrollOffset;
    const auto& entry = menuItems[index];

    
    if (entry.type == SettingType::SEPARATOR && (entry.name == nullptr || entry.name[0] == '\0')) {
      continue;
    }

    int itemY = startY + (visibleCount * itemHeight);
    bool isSelected = (index == selectedIndex);

    if (entry.type == SettingType::SEPARATOR) {
      if (isSelected) {
        renderer.rectangle.fill(0, itemY, pageWidth, itemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
      }

      int textX = 20;
      int textY = itemY + (itemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, entry.name, !isSelected);

      const char* indicator = entry.getValueText();
      if (indicator && indicator[0] != '\0') {
        int indicatorW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, indicator);
        renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageWidth - indicatorW - 30, textY, indicator, !isSelected);
      }

      renderer.line.render(0, itemY + itemHeight - 1, pageWidth, itemY + itemHeight - 1, true);
      visibleCount++;
      continue;
    }

    if (isSelected) {
      renderer.rectangle.fill(0, itemY, pageWidth, itemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    int textX = 28;
    int textY = itemY + (itemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, entry.name, !isSelected);

    const bool useCheckbox = (entry.type == SettingType::TOGGLE && entry.valuePtr);
    if (useCheckbox) {
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, pageWidth - 24, itemY, itemHeight, isSelected,
                                                 SETTINGS.*(entry.valuePtr) != 0);
    } else if (entry.type == SettingType::ENUM && entry.name && strcmp(entry.name, "Font Family") == 0) {
      const char* val = entry.getValueText();
      if (val && val[0] != '\0') {
        ReaderFontSettingsDraw::drawFontFamilyRowValue(renderer, SETTINGS.fontFamily, pageWidth - 24, itemY,
                                                       itemHeight, isSelected, val);
      }
    } else if (entry.type == SettingType::ENUM && entry.name && strcmp(entry.name, "Font Size") == 0) {
      const int valueAreaLeft = std::max(textX + 88, pageWidth * 38 / 100);
      ReaderFontSettingsDraw::drawFontSizeSliderRowValue(renderer, SETTINGS.fontFamily, SETTINGS.fontSize,
                                                         valueAreaLeft, pageWidth - 24, itemY, itemHeight, isSelected);
    } else {
      const char* val = entry.getValueText();
      if (val && val[0] != '\0') {
        int valW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, val);
        renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageWidth - valW - 30, textY, val, !isSelected);
      }
    }

    renderer.line.render(0, itemY + itemHeight - 1, pageWidth, itemY + itemHeight - 1, true);
    visibleCount++;
  }

  
  if ((int)menuItems.size() > itemsPerPage) {
    int listHeight = itemsPerPage * itemHeight;
    int thumbH = (itemsPerPage * listHeight) / menuItems.size();
    int thumbY = startY + (scrollOffset * listHeight) / menuItems.size();
    renderer.rectangle.fill(pageWidth - 4, thumbY, 2, thumbH, true);
  }

  {
    const GfxRenderer::Orientation orientationBeforeHints = renderer.getOrientation();
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    const int pageHeight = renderer.getScreenHeight();
    constexpr int fontId = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
    const int lineH = renderer.text.getLineHeight(fontId);
    constexpr int kHintBarInsetFromBottom = 40;
    constexpr int kGapAboveHints = 8;
    const int versionRowTop = pageHeight - kHintBarInsetFromBottom - kGapAboveHints - lineH;
    renderer.text.centered(fontId, versionRowTop, INX_VERSION, true, EpdFontFamily::REGULAR);
    renderer.setOrientation(orientationBeforeHints);
  }

  const char* backLbl = backButtonLabel ? backButtonLabel : "\xC2\xAB Back";
  const auto labels = mappedInput.mapLabels(backLbl, "Toggle", "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
