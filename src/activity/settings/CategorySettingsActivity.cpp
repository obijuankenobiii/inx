/**
 * @file CategorySettingsActivity.cpp
 * @brief Definitions for CategorySettingsActivity.
 */

#include "CategorySettingsActivity.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "CalibreSettingsActivity.h"
#include "ClearCacheActivity.h"
#include "ClockStylePickerActivity.h"
#include "activity/page/components/global/PopUp.h"
#include "images/Close.h"
#include "ReaderFontSettingsDraw.h"
#include "SleepImagePickerActivity.h"
#include "ThumbnailGeneratorActivity.h"
#include "TimeSyncActivity.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/UiTheme.h"
#include "util/StringUtils.h"

namespace {
constexpr const char* kSleepImageIndexPath = "/.system/sleep_images.idx";
constexpr uint8_t kSelectorModeSetting = 0;
constexpr uint8_t kSelectorModeSleepImage = 1;
constexpr const char* kSleepImageRefreshValue = "__refresh";

bool isSupportedSleepImageFile(const std::string& filename) {
  return StringUtils::checkFileExtension(filename, ".bmp") || StringUtils::checkFileExtension(filename, ".jpg") ||
         StringUtils::checkFileExtension(filename, ".jpeg");
}

void writeString(FsFile& file, const std::string& s) {
  file.write(reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
}
}  // namespace

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
  selectedIndex = embedded ? -1 : 0;
  scrollOffset = 0;
  updateRequired = true;

  if (categoryName != nullptr && strcmp(categoryName, "Reader") == 0) {
    FontManager::scanSDFonts("/fonts", true);
    FontManager::clampReaderFontFamilySlot(READER_SETTINGS.fontFamily);
  }

  setupMenu();

  if (embedded) {
    render();
    updateRequired = false;
    return;
  }

  xTaskCreate(&CategorySettingsActivity::taskTrampoline, "CategorySettingsActivityTask", 4096, this, 1,
              &displayTaskHandle);
}

/**
 * @brief Clean up resources and delete display task
 */
void CategorySettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // The display task holds renderingMutex across the whole render() — including the long displayBuffer() SPI/panel
  // transaction. Deleting it mid-render aborts that transaction, leaving the panel stuck on the half-written
  // settings frame (which then ghosts onto later screens). Take the mutex first so we block until the task has
  // finished its current frame and is idle, then delete it safely between frames.
  if (renderingMutex) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
  }

  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (renderingMutex) {
    xSemaphoreGive(renderingMutex);
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
  if (embedded) {
    openGroup(group);
    return;
  }

  groupExpanded_[groupIndex(group)] = !groupExpanded_[groupIndex(group)];
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

void CategorySettingsActivity::openGroup(const GroupType group) {
  detailGroup = group;
  groupOpen = true;
  detailScroll = 0;
  groupExpanded_.fill(false);
  groupExpanded_[groupIndex(group)] = true;
  setupMenu();
  selectedIndex = -1;
  updateRequired = true;
}

void CategorySettingsActivity::closeGroup() {
  groupOpen = false;
  detailGroup = GroupType::NONE;
  detailScroll = 0;
  groupExpanded_.fill(false);
  setupMenu();
  selectedIndex = -1;
  scrollOffset = 0;
  updateRequired = true;
}

void CategorySettingsActivity::detailRows(std::vector<int>& rows) const {
  rows.clear();
  for (int i = 0; i < static_cast<int>(menuItems.size()); ++i) {
    const MenuEntry& entry = menuItems[static_cast<size_t>(i)];
    if (entry.group == detailGroup && entry.type != SettingType::SEPARATOR) {
      rows.push_back(i);
    }
  }
}

bool CategorySettingsActivity::groupInput() {
  if (!groupOpen || selectorOpen || subActivity) return false;

  std::vector<int> rows;
  detailRows(rows);
  const int rowHeight = UiLayout::LIST_ITEM_HEIGHT;
  const int listTop = navigation::Menu::height + 20;
  const int visible = std::max(1, (renderer.getScreenHeight() - listTop - 10) / rowHeight);
  const int maxScroll = std::max(0, static_cast<int>(rows.size()) - visible);

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    closeGroup();
    return true;
  }

  const bool upPressed = mappedInput.wasPressed(itemPrevButton());
  const bool downPressed = mappedInput.wasPressed(itemNextButton());
  if (upPressed || downPressed) {
    if (rows.empty()) return true;
    int position = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
      if (rows[static_cast<size_t>(i)] == selectedIndex) {
        position = i;
        break;
      }
    }
    position = upPressed ? (position <= 0 ? static_cast<int>(rows.size()) - 1 : position - 1)
                  : (position < 0 || position + 1 >= static_cast<int>(rows.size()) ? 0 : position + 1);
    selectedIndex = rows[static_cast<size_t>(position)];
    detailScroll = std::max(0, std::min(maxScroll, position - visible + 1));
    updateRequired = true;
    return true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && selectedIndex >= 0) {
    const MenuEntry& selected = menuItems[static_cast<size_t>(selectedIndex)];
    if (selected.type == SettingType::TOGGLE) {
      selected.change(0);
    } else if (selected.type == SettingType::ENUM || selected.type == SettingType::VALUE) {
      openSelectorForSelected();
    } else if (selected.type == SettingType::ACTION) {
      selected.change(0);
    }
    selectedIndex = -1;
    return true;
  }
  return false;
}

void CategorySettingsActivity::renderGroupPage() {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  constexpr int rowHeight = UiLayout::LIST_ITEM_HEIGHT;
  constexpr int titleFont = MONTSERRAT_16_FONT_ID;
  const int itemFont = systemFontId();
  const int listTop = navigation::Menu::height + 20;
  const int visible = std::max(1, (pageHeight - listTop - 10) / rowHeight);

  std::vector<int> rows;
  detailRows(rows);
  const int maxScroll = std::max(0, static_cast<int>(rows.size()) - visible);
  detailScroll = std::max(0, std::min(detailScroll, maxScroll));

  const char* title = "Settings";
  for (const MenuEntry& entry : menuItems) {
    if (entry.type == SettingType::SEPARATOR && entry.group == detailGroup && entry.name) {
      title = entry.name;
      break;
    }
  }
  renderer.text.render(titleFont, 20, 20, title, true, EpdFontFamily::BOLD);
  renderer.bitmap.icon(Close, pageWidth - 60, 20, 40, 40);

  for (int i = 0; i < visible && detailScroll + i < static_cast<int>(rows.size()); ++i) {
    const int index = rows[static_cast<size_t>(detailScroll + i)];
    const MenuEntry& entry = menuItems[static_cast<size_t>(index)];
    const int itemY = listTop + i * rowHeight;
    const bool selected = index == selectedIndex;
    if (selected) renderer.rectangle.fill(0, itemY, pageWidth, rowHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
    const int textY = itemY + (rowHeight - renderer.text.getLineHeight(itemFont)) / 2;
    renderer.text.render(itemFont, 20, textY, entry.name ? entry.name : "", !selected, EpdFontFamily::REGULAR);
    if (entry.type == SettingType::TOGGLE && entry.valuePtr) {
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, pageWidth - 24, itemY, rowHeight, selected,
                                                 SETTINGS.*(entry.valuePtr) != 0);
    } else {
      const char* value = entry.getValueText ? entry.getValueText() : "";
      if (value && value[0] != '\0') {
        const int valueWidth = renderer.text.getWidth(itemFont, value);
        renderer.text.render(itemFont, pageWidth - valueWidth - 30, textY, value, !selected,
                             EpdFontFamily::REGULAR);
      }
    }
    if (i + 1 < visible && detailScroll + i + 1 < static_cast<int>(rows.size())) {
      renderer.line.render(0, itemY + rowHeight - 1, pageWidth, itemY + rowHeight - 1, !selected,
                           LineRender::Style::Dotted);
    }
  }
}

/**
 * @brief Sets up the menu structure based on expansion states
 */
void CategorySettingsActivity::setupMenu() {
  menuItems.clear();

  for (int i = 0; i < settingsCount; i++) {
    const auto& setting = settingsList[i];
    const SettingInfo* const settingPtr = &setting;

    if (setting.type == SettingType::SEPARATOR) {
      MenuEntry entry;
      entry.name = setting.name;
      entry.type = SettingType::SEPARATOR;
      entry.group = setting.group;
      entry.valuePtr = nullptr;
      entry.valueRange = {0, 0, 0};
      entry.setting = settingPtr;
      const GroupType sepGroup = setting.group;
      if (embedded) {
        entry.getValueText = []() -> const char* { return "›"; };
      } else {
        entry.getValueText = [this, sepGroup]() -> const char* {
          static char indicator[4];
          snprintf(indicator, sizeof(indicator), "%s", isGroupExpanded(sepGroup) ? "-" : "+");
          return indicator;
        };
      }
      entry.change = [](int) {};
      menuItems.push_back(entry);
    } else {
      if (setting.group == GroupType::NONE || isGroupExpanded(setting.group)) {
        MenuEntry entry;
        entry.name = setting.name;
        entry.type = setting.type;
        entry.valuePtr = setting.valuePtr;
        entry.valueRange = setting.valueRange;
        entry.group = setting.group;
        entry.setting = settingPtr;

        if (setting.type == SettingType::INFO) {
          entry.getValueText = [settingPtr]() -> const char* {
            return settingPtr->enumValues.empty() ? "" : settingPtr->enumValues[0].c_str();
          };
          entry.change = [](int) {};
        }
        if (setting.type == SettingType::TOGGLE) {
          entry.getValueText = [settingPtr]() -> const char* {
            return (SETTINGS.*(settingPtr->valuePtr)) ? "ON" : "OFF";
          };
          entry.change = [this, settingPtr](int) {
            SETTINGS.*(settingPtr->valuePtr) = !(SETTINGS.*(settingPtr->valuePtr));
            SETTINGS.saveToFile();
            updateRequired = true;
          };
        }
        if (setting.type == SettingType::ENUM) {
          if (setting.name != nullptr && strcmp(setting.name, "Font Family") == 0) {
            entry.getValueText = [settingPtr]() -> const char* {
              thread_local std::string tls;
              tls = FontManager::readerFontFamilyLabel(SETTINGS.*(settingPtr->valuePtr));
              return tls.c_str();
            };
            entry.change = [this, settingPtr](int delta) {
              int current = SETTINGS.*(settingPtr->valuePtr);
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
              SETTINGS.*(settingPtr->valuePtr) = static_cast<uint8_t>(newVal);
              SETTINGS.saveToFile();
              updateRequired = true;
            };
          } else {
            entry.getValueText = [settingPtr]() -> const char* {
              const int current = SETTINGS.*(settingPtr->valuePtr);
              if (!settingPtr->enumOptionValues.empty() &&
                  settingPtr->enumOptionValues.size() == settingPtr->enumValues.size()) {
                for (size_t i = 0; i < settingPtr->enumOptionValues.size(); ++i) {
                  if (settingPtr->enumOptionValues[i] == current) {
                    return settingPtr->enumValues[i].c_str();
                  }
                }
                if (settingPtr->valuePtr == &SystemSetting::recentLibraryMode &&
                    (current == SystemSetting::RECENT_LIST_DEPRECATED || current == SystemSetting::RECENT_SIMPLE)) {
                  for (size_t i = 0; i < settingPtr->enumOptionValues.size(); ++i) {
                    if (settingPtr->enumOptionValues[i] == SystemSetting::RECENT_FLOW) {
                      return settingPtr->enumValues[i].c_str();
                    }
                  }
                }
                return "Unknown";
              }
              if (current >= 0 && current < (int)settingPtr->enumValues.size()) {
                return settingPtr->enumValues[current].c_str();
              }
              return "Unknown";
            };
            entry.change = [this, settingPtr](int delta) {
              if (!settingPtr->enumOptionValues.empty() &&
                  settingPtr->enumOptionValues.size() == settingPtr->enumValues.size()) {
                int currentIndex = 0;
                const int current = SETTINGS.*(settingPtr->valuePtr);
                for (size_t i = 0; i < settingPtr->enumOptionValues.size(); ++i) {
                  if (settingPtr->enumOptionValues[i] == current) {
                    currentIndex = static_cast<int>(i);
                    break;
                  }
                }
                if (settingPtr->valuePtr == &SystemSetting::recentLibraryMode &&
                    (current == SystemSetting::RECENT_LIST_DEPRECATED || current == SystemSetting::RECENT_SIMPLE)) {
                  for (size_t i = 0; i < settingPtr->enumOptionValues.size(); ++i) {
                    if (settingPtr->enumOptionValues[i] == SystemSetting::RECENT_FLOW) {
                      currentIndex = static_cast<int>(i);
                      break;
                    }
                  }
                }
                int newIndex = currentIndex + delta;
                if (newIndex < 0) newIndex = static_cast<int>(settingPtr->enumOptionValues.size()) - 1;
                if (newIndex >= static_cast<int>(settingPtr->enumOptionValues.size())) newIndex = 0;
                SETTINGS.*(settingPtr->valuePtr) = settingPtr->enumOptionValues[static_cast<size_t>(newIndex)];
              } else {
                int current = SETTINGS.*(settingPtr->valuePtr);
                int newVal = current + delta;
                if (newVal < 0) newVal = settingPtr->enumValues.size() - 1;
                if (newVal >= (int)settingPtr->enumValues.size()) newVal = 0;
                SETTINGS.*(settingPtr->valuePtr) = newVal;
              }
              SETTINGS.saveToFile();
              updateRequired = true;
            };
          }
        }
        if (setting.type == SettingType::VALUE) {
          entry.getValueText = [settingPtr]() -> const char* {
            static char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d", SETTINGS.*(settingPtr->valuePtr));
            return buffer;
          };
          entry.change = [this, settingPtr](int delta) {
            int current = SETTINGS.*(settingPtr->valuePtr);
            int newVal = current + (delta * settingPtr->valueRange.step);
            if (newVal < settingPtr->valueRange.min) newVal = settingPtr->valueRange.max;
            if (newVal > settingPtr->valueRange.max) newVal = settingPtr->valueRange.min;
            SETTINGS.*(settingPtr->valuePtr) = newVal;
            SETTINGS.saveToFile();
            updateRequired = true;
          };
        }
        if (setting.type == SettingType::ACTION) {
          entry.getValueText = []() -> const char* { return ""; };
          entry.change = [this, settingPtr](int) {
            if (strcmp(settingPtr->name, "Index your library") == 0) {
              if (onIndexLibrary) {
                onIndexLibrary();
              }
              return;
            }
            if (strcmp(settingPtr->name, "Generate thumbnails") == 0) {
              exitActivity();
              enterNewActivity(new ThumbnailGeneratorActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
              return;
            }
            if (strcmp(settingPtr->name, "About") == 0) {
              if (onAboutPanel) {
                onAboutPanel();
              }
              return;
            }
            if (strcmp(settingPtr->name, "Delete Cache") == 0) {
              exitActivity();
              enterNewActivity(new ClearCacheActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
            }
            if (strcmp(settingPtr->name, "Choose sleep image") == 0) {
              exitActivity();
              enterNewActivity(new SleepImagePickerActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
              return;
            }
            if (strcmp(settingPtr->name, "Choose clock") == 0 || strcmp(settingPtr->name, "Face") == 0) {
              exitActivity();
              enterNewActivity(new ClockStylePickerActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
            }
            if (strcmp(settingPtr->name, "Sync time via WiFi") == 0 || strcmp(settingPtr->name, "Sync") == 0) {
              exitActivity();
              enterNewActivity(new TimeSyncActivity(renderer, mappedInput, [this] {
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

int CategorySettingsActivity::selectedOptionIndex(const MenuEntry& entry) const {
  if (!entry.valuePtr) {
    return 0;
  }
  const auto* setting = entry.setting;
  const int current = SETTINGS.*(entry.valuePtr);
  if (entry.type == SettingType::VALUE) {
    const int step = std::max(1, static_cast<int>(entry.valueRange.step));
    return std::max(0, (current - static_cast<int>(entry.valueRange.min)) / step);
  }
  if (entry.type == SettingType::ENUM && setting && !setting->enumOptionValues.empty() &&
      setting->enumOptionValues.size() == setting->enumValues.size()) {
    for (size_t i = 0; i < setting->enumOptionValues.size(); ++i) {
      if (setting->enumOptionValues[i] == current) {
        return static_cast<int>(i);
      }
    }
    if (entry.valuePtr == &SystemSetting::recentLibraryMode &&
        (current == SystemSetting::RECENT_LIST_DEPRECATED || current == SystemSetting::RECENT_SIMPLE)) {
      for (size_t i = 0; i < setting->enumOptionValues.size(); ++i) {
        if (setting->enumOptionValues[i] == SystemSetting::RECENT_FLOW) {
          return static_cast<int>(i);
        }
      }
    }
    return 0;
  }
  return std::max(0, current);
}

void CategorySettingsActivity::applySelectedOption(MenuEntry& entry, const int optionIndex) {
  if (!entry.valuePtr) {
    return;
  }
  const auto* setting = entry.setting;
  if (entry.type == SettingType::VALUE) {
    const int step = std::max(1, static_cast<int>(entry.valueRange.step));
    const int value = static_cast<int>(entry.valueRange.min) + optionIndex * step;
    SETTINGS.*(entry.valuePtr) = static_cast<uint8_t>(
        std::max(static_cast<int>(entry.valueRange.min), std::min(static_cast<int>(entry.valueRange.max), value)));
  } else if (setting && !setting->enumOptionValues.empty() &&
             setting->enumOptionValues.size() == setting->enumValues.size() && optionIndex >= 0 &&
             optionIndex < static_cast<int>(setting->enumOptionValues.size())) {
    SETTINGS.*(entry.valuePtr) = setting->enumOptionValues[static_cast<size_t>(optionIndex)];
  } else {
    SETTINGS.*(entry.valuePtr) = static_cast<uint8_t>(optionIndex);
  }
  SETTINGS.saveToFile();
}

void CategorySettingsActivity::openSelectorForSelected() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(menuItems.size())) {
    return;
  }
  const auto& entry = menuItems[selectedIndex];
  if (entry.type != SettingType::ENUM && entry.type != SettingType::VALUE) {
    return;
  }

  selectorOptions.clear();
  selectorValues.clear();
  if (entry.type == SettingType::ENUM) {
    if (entry.name != nullptr && strcmp(entry.name, "Font Family") == 0) {
      selectorOptions = FontManager::readerFontFamilyEnumLabels();
    } else if (entry.setting) {
      selectorOptions = entry.setting->enumValues;
    }
  } else {
    const int step = std::max(1, static_cast<int>(entry.valueRange.step));
    for (int value = entry.valueRange.min; value <= entry.valueRange.max; value += step) {
      char buffer[16];
      std::snprintf(buffer, sizeof(buffer), "%d", value);
      selectorOptions.emplace_back(buffer);
    }
  }
  if (selectorOptions.empty()) {
    return;
  }

  selectorMode = kSelectorModeSetting;
  selectorOpen = true;
  selectorSourceIndex = selectedIndex;
  selectorSelectedIndex = std::min(selectedOptionIndex(entry), static_cast<int>(selectorOptions.size()) - 1);
  selectorScrollOffset = std::max(0, selectorSelectedIndex - 2);
  updateRequired = true;
}

bool CategorySettingsActivity::rebuildSleepImageIndex() {
  SdMan.mkdir("/.system");

  std::vector<std::pair<std::string, std::string>> images;
  auto dir = SdMan.open("/sleep");
  if (dir && dir.isDirectory()) {
    char name[256];
    while (auto file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      std::string filename = name;
      if (!filename.empty() && filename[0] != '.' && isSupportedSleepImageFile(filename)) {
        images.emplace_back(filename, filename);
      }
      file.close();
    }
    dir.close();
  }

  std::sort(images.begin(), images.end(),
            [](const std::pair<std::string, std::string>& a, const std::pair<std::string, std::string>& b) {
              return a.first < b.first;
            });

  if (SdMan.exists("/sleep.bmp")) {
    images.emplace_back("/sleep.bmp", "sleep.bmp (SD root)");
  }
  if (SdMan.exists("/sleep.jpg")) {
    images.emplace_back("/sleep.jpg", "sleep.jpg (SD root)");
  }
  if (SdMan.exists("/sleep.jpeg")) {
    images.emplace_back("/sleep.jpeg", "sleep.jpeg (SD root)");
  }

  FsFile idxFile;
  if (!SdMan.openFileForWrite("SLP", kSleepImageIndexPath, idxFile)) {
    return false;
  }
  for (const auto& image : images) {
    writeString(idxFile, image.first);
    idxFile.write('\t');
    writeString(idxFile, image.second);
    idxFile.write('\n');
  }
  idxFile.close();
  return true;
}

void CategorySettingsActivity::loadSleepImageIndexRows() {
  if (!SdMan.exists(kSleepImageIndexPath)) {
    rebuildSleepImageIndex();
  }

  FsFile idxFile;
  if (!SdMan.openFileForRead("SLP", kSleepImageIndexPath, idxFile)) {
    return;
  }

  std::string line;
  while (idxFile.available()) {
    const int c = idxFile.read();
    if (c < 0) {
      break;
    }
    if (c == '\n' || c == '\r') {
      if (!line.empty()) {
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) {
          selectorValues.push_back(line.substr(0, tab));
          selectorOptions.push_back(line.substr(tab + 1));
        }
        line.clear();
      }
      continue;
    }
    line.push_back(static_cast<char>(c));
  }
  if (!line.empty()) {
    const size_t tab = line.find('\t');
    if (tab != std::string::npos) {
      selectorValues.push_back(line.substr(0, tab));
      selectorOptions.push_back(line.substr(tab + 1));
    }
  }
  idxFile.close();
}

void CategorySettingsActivity::openSleepImageSelector() {
  selectorMode = kSelectorModeSleepImage;
  selectorSourceIndex = selectedIndex;
  selectorOptions.clear();
  selectorValues.clear();
  selectorOptions.emplace_back("Refresh");
  selectorValues.emplace_back(kSleepImageRefreshValue);
  selectorOptions.emplace_back("Random");
  selectorValues.emplace_back("");
  loadSleepImageIndexRows();

  selectorSelectedIndex = 1;
  for (size_t i = 0; i < selectorValues.size(); ++i) {
    if (selectorValues[i] == SETTINGS.sleepCustomBmp) {
      selectorSelectedIndex = static_cast<int>(i);
      break;
    }
  }
  selectorScrollOffset = std::max(0, selectorSelectedIndex - 2);
  selectorOpen = true;
  updateRequired = true;
}

void CategorySettingsActivity::applySleepImageSelection() {
  if (selectorSelectedIndex < 0 || selectorSelectedIndex >= static_cast<int>(selectorValues.size())) {
    return;
  }
  const std::string value = selectorValues[selectorSelectedIndex];
  SETTINGS.setSleepCustomBmpFromInput(value.c_str());
  SETTINGS.saveToFile();
}

void CategorySettingsActivity::moveSelector(const int delta) {
  if (!selectorOpen || selectorOptions.empty()) {
    return;
  }
  selectorSelectedIndex += delta;
  if (selectorSelectedIndex < 0) {
    selectorSelectedIndex = static_cast<int>(selectorOptions.size()) - 1;
  }
  if (selectorSelectedIndex >= static_cast<int>(selectorOptions.size())) {
    selectorSelectedIndex = 0;
  }
  const int contentTop = 0;
  const int visibleRows = PopUp::bounds(renderer, static_cast<int>(selectorOptions.size()), contentTop).rows;
  if (selectorSelectedIndex < selectorScrollOffset) {
    selectorScrollOffset = selectorSelectedIndex;
  } else if (selectorSelectedIndex >= selectorScrollOffset + visibleRows) {
    selectorScrollOffset = selectorSelectedIndex - visibleRows + 1;
  }
  updateRequired = true;
}

void CategorySettingsActivity::selectorPage(const int delta) {
  const int contentTop = 0;
  const int pageRows = PopUp::bounds(renderer, static_cast<int>(selectorOptions.size()), contentTop).rows;
  moveSelector(delta * pageRows);
}

void CategorySettingsActivity::closeSelector(const bool save) {
  if (!selectorOpen) {
    return;
  }
  if (save && selectorMode == kSelectorModeSleepImage) {
    if (selectorSelectedIndex >= 0 && selectorSelectedIndex < static_cast<int>(selectorValues.size()) &&
        selectorValues[selectorSelectedIndex] == kSleepImageRefreshValue) {
      rebuildSleepImageIndex();
      openSleepImageSelector();
      return;
    }
    applySleepImageSelection();
  } else if (save && selectorSourceIndex >= 0 && selectorSourceIndex < static_cast<int>(menuItems.size())) {
    applySelectedOption(menuItems[selectorSourceIndex], selectorSelectedIndex);
  }
  selectorOpen = false;
  selectorMode = kSelectorModeSetting;
  selectorSourceIndex = -1;
  selectorSelectedIndex = 0;
  selectorScrollOffset = 0;
  selectorOptions.clear();
  selectorValues.clear();
  updateRequired = true;
}

/**
 * @brief Main loop handling input and state updates
 */
void CategorySettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (selectorOpen) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      closeSelector(false);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      closeSelector(true);
      return;
    }
    if (mappedInput.wasPressed(MenuNav::itemPrev())) {
      moveSelector(-1);
      return;
    }
    if (mappedInput.wasPressed(MenuNav::itemNext())) {
      moveSelector(1);
      return;
    }
    if (mappedInput.wasPressed(MenuNav::tabPrev())) {
      selectorPage(-1);
      return;
    }
    if (mappedInput.wasPressed(MenuNav::tabNext())) {
      selectorPage(1);
      return;
    }
    return;
  }

  if (embedded && groupOpen) {
    groupInput();
    return;
  }

  // Tab vs item nav buttons depend on the main-menu nav setting (front: L/R tabs, U/D items; side: swapped).
  const bool upPressed = mappedInput.wasPressed(itemPrevButton());
  const bool downPressed = mappedInput.wasPressed(itemNextButton());
  const bool leftPressed = mappedInput.wasPressed(tabPrevButton());
  const bool rightPressed = mappedInput.wasPressed(tabNextButton());
  const bool confirmPressed = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);

  if (leftPressed) {
    int newTabIndex = (tabSelectorIndex - 1 + TAB_COUNT) % TAB_COUNT;
    tabSelectorIndex = newTabIndex;

    if (newTabIndex != 2) {
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
      navigateToSelectedMenu();
      return;
    }
    updateRequired = true;
    return;
  }

  if (backPressed) {
    onGoBack();
    return;
  }

  bool needRedraw = false;

  if (upPressed) {
    const int totalItems = static_cast<int>(menuItems.size());
    if (totalItems > 0) {
      selectedIndex = selectedIndex < 0 ? totalItems - 1 : (selectedIndex - 1 + totalItems) % totalItems;
      const int maxScroll = std::max(0, totalItems - itemsPerPage);
      if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
      if (selectedIndex >= scrollOffset + itemsPerPage) scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
      needRedraw = true;
    }
  } else if (downPressed) {
    const int totalItems = static_cast<int>(menuItems.size());
    if (totalItems > 0) {
      selectedIndex = selectedIndex < 0 ? 0 : (selectedIndex + 1) % totalItems;
      int maxScroll = std::max(0, totalItems - itemsPerPage);
      if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
      if (selectedIndex > scrollOffset + itemsPerPage - 1) {
        scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      }
      scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
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
      } else if (selected.type == SettingType::ENUM || selected.type == SettingType::VALUE) {
        openSelectorForSelected();
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

void CategorySettingsActivity::renderSelectorOverlay() {
  if (!selectorOpen || selectorOptions.empty()) {
    return;
  }
  const int contentTop = 0;
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(selectorOptions.size()), contentTop);
  const int maxScroll = std::max(0, static_cast<int>(selectorOptions.size()) - box.rows);
  selectorScrollOffset = std::max(0, std::min(selectorScrollOffset, maxScroll));

  const char* title = "Select";
  if (selectorSourceIndex >= 0 && selectorSourceIndex < static_cast<int>(menuItems.size()) &&
      menuItems[selectorSourceIndex].name) {
    title = menuItems[selectorSourceIndex].name;
  }
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, title);
  PopUp::list(renderer, box, selectorOptions, selectorSelectedIndex, selectorScrollOffset);
  PopUp::border(renderer, box);
}

/**
 * @brief Render the category settings screen
 */
void CategorySettingsActivity::render() {
  if (!embedded) {
    renderer.clearScreen();
  }

  const auto pageWidth = renderer.getScreenWidth();
  const int itemFont = systemFontId();

  if (embedded && groupOpen) {
    renderer.clearScreen();
    renderGroupPage();
    if (selectorOpen) renderSelectorOverlay();
    return;
  }

  int dividerY = navigation::Menu::height + 20 + UiLayout::LIST_ITEM_HEIGHT + 10 + 30;
  if (!embedded) {
    renderTabBar(renderer);

    const int headerY = mainContentTop();
    const int headerHeight = mainHeaderHeight();
    const int headerTextY = headerY + (headerHeight - renderer.text.getLineHeight(MONTSERRAT_12_FONT_ID)) / 2;

    renderer.text.render(MONTSERRAT_12_FONT_ID, 20, headerTextY, categoryName, true, EpdFontFamily::BOLD);

  // Version shown as a small rounded tag: black rounded background with white text.
  const int verFont = MONTSERRAT_8_FONT_ID;
  const int verPadX = 8;
  const int versionW = renderer.text.getWidth(verFont, INX_VERSION);
  const int verLineH = renderer.text.getLineHeight(verFont);
  const int verTagH = verLineH + 6;
  const int verTagW = versionW + verPadX * 2;
  const int verTagX = pageWidth - verTagW - 20;
  const int verTagY = headerY + (headerHeight - verTagH) / 2;
  renderer.rectangle.fill(verTagX, verTagY, verTagW, verTagH, true, true);  // filled, rounded (black)
  const int versionY = verTagY + (verTagH - verLineH) / 2;
  renderer.text.render(verFont, verTagX + verPadX, versionY, INX_VERSION, false, EpdFontFamily::REGULAR);  // white text

    dividerY = headerY + headerHeight;
    renderer.line.render(0, dividerY, pageWidth, dividerY, true);
  }

  const char* backLbl = selectorOpen ? "Cancel" : (backButtonLabel ? backButtonLabel : "\xC2\xAB Back");
  const char* confirmLbl = selectorOpen ? "Select" : "Open";
  const char* prevLbl = selectorOpen ? "Page -" : "";
  const char* nextLbl = selectorOpen ? "Page +" : "";
  const auto labels = mappedInput.mapLabels(backLbl, confirmLbl, prevLbl, nextLbl);

  const int startY = dividerY;
  constexpr int itemHeight = UiTheme::DRAWER_LIST_ITEM_HEIGHT;

  int visibleCount = 0;
  const auto hasNextRenderedRow = [&](const int currentOffset) {
    for (int nextOffset = currentOffset + 1;
         nextOffset < itemsPerPage && nextOffset + scrollOffset < static_cast<int>(menuItems.size()); ++nextOffset) {
      const auto& nextEntry = menuItems[static_cast<size_t>(nextOffset + scrollOffset)];
      if (nextEntry.type != SettingType::SEPARATOR || (nextEntry.name != nullptr && nextEntry.name[0] != '\0')) {
        return true;
      }
    }
    return false;
  };
  for (int i = 0; i < itemsPerPage && (i + scrollOffset) < (int)menuItems.size(); i++) {
    int index = i + scrollOffset;
    const auto& entry = menuItems[index];

    if (entry.type == SettingType::SEPARATOR && (entry.name == nullptr || entry.name[0] == '\0')) {
      continue;
    }

    int itemY = startY + (visibleCount * itemHeight);
    bool isSelected = (index == selectedIndex);
    const bool hasNextRow = hasNextRenderedRow(i);

    if (entry.type == SettingType::SEPARATOR) {
      if (isSelected) {
        renderer.rectangle.fill(0, itemY, pageWidth, itemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
      }

      int textX = 20;
      int textY = itemY + (itemHeight - renderer.text.getLineHeight(itemFont)) / 2;
      renderer.text.render(itemFont, textX, textY, entry.name, !isSelected);

      const char* indicator = entry.getValueText();
      if (indicator && indicator[0] != '\0') {
        int indicatorW = renderer.text.getWidth(itemFont, indicator);
        const int indicatorY = itemY + (itemHeight - renderer.text.getLineHeight(itemFont)) / 2;
        renderer.text.render(itemFont, pageWidth - indicatorW - 30, indicatorY, indicator,
                             !isSelected);
      }

      if (hasNextRow) {
        renderer.line.render(0, itemY + itemHeight - 1, pageWidth, itemY + itemHeight - 1, true,
                             LineRender::Style::Dotted);
      }
      visibleCount++;
      continue;
    }

    if (isSelected) {
      renderer.rectangle.fill(0, itemY, pageWidth, itemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    int textX = entry.group == GroupType::NONE ? 20 : 28;
    int textY = itemY + (itemHeight - renderer.text.getLineHeight(itemFont)) / 2;

    renderer.text.render(itemFont, textX, textY, entry.name, !isSelected);

    const bool useCheckbox = (entry.type == SettingType::TOGGLE && entry.valuePtr);
    if (useCheckbox) {
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, pageWidth - 24, itemY, itemHeight, isSelected,
                                                 SETTINGS.*(entry.valuePtr) != 0);
    } else if (entry.type == SettingType::ENUM && entry.name && strcmp(entry.name, "Font Family") == 0) {
      const char* val = entry.getValueText();
      if (val && val[0] != '\0') {
        ReaderFontSettingsDraw::drawFontFamilyRowValue(renderer, READER_SETTINGS.fontFamily, pageWidth - 24, itemY, itemHeight,
                                                       isSelected, val);
      }
    } else if (entry.type == SettingType::ENUM && entry.name && strcmp(entry.name, "Font Size") == 0) {
      const int valueAreaLeft = std::max(textX + 88, pageWidth * 38 / 100);
      ReaderFontSettingsDraw::drawFontSizeSliderRowValue(renderer, READER_SETTINGS.fontFamily, READER_SETTINGS.fontSize,
                                                         valueAreaLeft, pageWidth - 24, itemY, itemHeight, isSelected);
    } else {
      const char* val = entry.getValueText();
      if (val && val[0] != '\0') {
        int valW = renderer.text.getWidth(itemFont, val);
        const int valY = itemY + (itemHeight - renderer.text.getLineHeight(itemFont)) / 2;
        renderer.text.render(itemFont, pageWidth - valW - 30, valY, val, !isSelected);
      }
    }

    if (hasNextRow) {
      renderer.line.render(0, itemY + itemHeight - 1, pageWidth, itemY + itemHeight - 1, true,
                           LineRender::Style::Dotted);
    }
    visibleCount++;
  }

  if ((int)menuItems.size() > itemsPerPage) {
    int listHeight = itemsPerPage * itemHeight;
    int thumbH = (itemsPerPage * listHeight) / menuItems.size();
    int thumbY = startY + (scrollOffset * listHeight) / menuItems.size();
    renderer.rectangle.fill(pageWidth - 4, thumbY, 2, thumbH, true);
  }

  if (!embedded && INX_THEME.mainTabsAtBottom()) {
    // Bottom-tabs mode moves the tab bar to the screen bottom, where the classic button-hints row normally
    // goes, so redraw that same row just above the tab bar instead — only for this settings screen, since
    // other bottom-tabs screens rely on the tab bar alone.
    const int hintsAreaTop = mainContentBottom(renderer) - kBottomButtonHintsHeight;
    const int hintsY = hintsAreaTop + (kBottomButtonHintsHeight - 40) / 2;
    renderer.ui.buttonHints(itemFont, labels.btn1, labels.btn2, labels.btn3, labels.btn4,
                           hintsY);
  }

  if (selectorOpen) {
    renderSelectorOverlay();
  }

  if (!embedded) {
    renderButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  if (!embedded) {
    renderer.displayBuffer();
  }
}

void CategorySettingsActivity::renderEmbedded() {
  if (!embedded || subActivity) return;
  render();
  updateRequired = false;
}

bool CategorySettingsActivity::takeRenderRequest() {
  if (!embedded || subActivity || !updateRequired) return false;
  updateRequired = false;
  return true;
}
