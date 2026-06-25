/**
 * @file CategorySettingsActivity.cpp
 * @brief Definitions for CategorySettingsActivity.
 */

#include "CategorySettingsActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "CalibreSettingsActivity.h"
#include "ClockStylePickerActivity.h"
#include "ClearCacheActivity.h"
#include "KOReaderSettingsActivity.h"
#include "OtaUpdateActivity.h"
#include "ReaderFontSettingsDraw.h"
#include "SleepImagePickerActivity.h"
#include "ThumbnailGeneratorActivity.h"
#include "TimeSyncActivity.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "util/StringUtils.h"

#include <EpdFontFamily.h>

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
          if (setting.name != nullptr && strcmp(setting.name, "Timezone") == 0) {
            entry.getValueText = []() -> const char* {
              static char buffer[16];
              SETTINGS.formatTimeZone(buffer, sizeof(buffer));
              return buffer;
            };
          } else {
            entry.getValueText = [this, setting]() -> const char* {
              static char buffer[32];
              snprintf(buffer, sizeof(buffer), "%d", SETTINGS.*(setting.valuePtr));
              return buffer;
            };
          }
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
            if (strcmp(setting.name, "Generate thumbnails") == 0) {
              exitActivity();
              enterNewActivity(new ThumbnailGeneratorActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
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
            if (strcmp(setting.name, "Delete Cache") == 0) {
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
              return;
            }
            if (strcmp(setting.name, "Choose clock") == 0 || strcmp(setting.name, "Face") == 0) {
              exitActivity();
              enterNewActivity(new ClockStylePickerActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
            }
            if (strcmp(setting.name, "Sync time via WiFi") == 0 || strcmp(setting.name, "Sync") == 0) {
              exitActivity();
              enterNewActivity(new TimeSyncActivity(renderer, mappedInput, [this] {
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

namespace {
std::string formatTimezoneOption(const uint8_t value) {
  const int minutes = (static_cast<int>(value) - 48) * 15;
  const char sign = minutes < 0 ? '-' : '+';
  const int absMinutes = minutes < 0 ? -minutes : minutes;
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "UTC%c%02d:%02d", sign, absMinutes / 60, absMinutes % 60);
  return std::string(buffer);
}
}  // namespace

int CategorySettingsActivity::selectedOptionIndex(const MenuEntry& entry) const {
  if (!entry.valuePtr) {
    return 0;
  }
  const int current = SETTINGS.*(entry.valuePtr);
  if (entry.type == SettingType::VALUE) {
    const int step = std::max(1, static_cast<int>(entry.valueRange.step));
    return std::max(0, (current - static_cast<int>(entry.valueRange.min)) / step);
  }
  return std::max(0, current);
}

void CategorySettingsActivity::applySelectedOption(MenuEntry& entry, const int optionIndex) {
  if (!entry.valuePtr) {
    return;
  }
  if (entry.type == SettingType::VALUE) {
    const int step = std::max(1, static_cast<int>(entry.valueRange.step));
    const int value = static_cast<int>(entry.valueRange.min) + optionIndex * step;
    SETTINGS.*(entry.valuePtr) = static_cast<uint8_t>(std::max(static_cast<int>(entry.valueRange.min),
                                                               std::min(static_cast<int>(entry.valueRange.max), value)));
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
    selectorOptions = entry.enumValues;
  } else {
    const int step = std::max(1, static_cast<int>(entry.valueRange.step));
    for (int value = entry.valueRange.min; value <= entry.valueRange.max; value += step) {
      if (entry.name && std::strcmp(entry.name, "Timezone") == 0) {
        selectorOptions.push_back(formatTimezoneOption(static_cast<uint8_t>(value)));
      } else {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%d", value);
        selectorOptions.emplace_back(buffer);
      }
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
  constexpr int visibleRows = 5;
  if (selectorSelectedIndex < selectorScrollOffset) {
    selectorScrollOffset = selectorSelectedIndex;
  } else if (selectorSelectedIndex >= selectorScrollOffset + visibleRows) {
    selectorScrollOffset = selectorSelectedIndex - visibleRows + 1;
  }
  updateRequired = true;
}

void CategorySettingsActivity::selectorPage(const int delta) {
  constexpr int pageRows = 5;
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
    updateRequired = true;
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

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  constexpr int titleFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
  constexpr int itemFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
  constexpr int rowHeight = 42;
  constexpr int headerHeight = 44;
  constexpr int visibleRows = 5;

  const int rows = std::min(visibleRows, static_cast<int>(selectorOptions.size()));
  const int panelW = std::min(pageWidth - 24, 360);
  const int panelH = headerHeight + rows * rowHeight + 18;
  const int panelX = (pageWidth - panelW) / 2;
  const int panelY = std::max(TAB_BAR_HEIGHT + 8, (pageHeight - panelH) / 2);

  renderer.rectangle.fill(panelX, panelY, panelW, panelH, false);
  renderer.rectangle.render(panelX, panelY, panelW, panelH, true);

  const char* title = "Select";
  if (selectorSourceIndex >= 0 && selectorSourceIndex < static_cast<int>(menuItems.size()) &&
      menuItems[selectorSourceIndex].name) {
    title = menuItems[selectorSourceIndex].name;
  }
  const std::string shownTitle = renderer.text.truncate(titleFont, title, panelW - 32, EpdFontFamily::BOLD);
  renderer.text.render(titleFont, panelX + 16, panelY + 12, shownTitle.c_str(), true, EpdFontFamily::BOLD);
  renderer.line.render(panelX, panelY + headerHeight - 4, panelX + panelW, panelY + headerHeight - 4, true);

  const int maxScroll = std::max(0, static_cast<int>(selectorOptions.size()) - rows);
  selectorScrollOffset = std::max(0, std::min(selectorScrollOffset, maxScroll));
  for (int i = 0; i < rows; ++i) {
    const int optionIndex = selectorScrollOffset + i;
    if (optionIndex >= static_cast<int>(selectorOptions.size())) {
      break;
    }
    const int rowY = panelY + headerHeight + i * rowHeight;
    const bool selected = optionIndex == selectorSelectedIndex;
    if (selected) {
      renderer.rectangle.fill(panelX + 4, rowY + 2, panelW - 8, rowHeight - 4, true);
    }

    const std::string option = renderer.text.truncate(itemFont, selectorOptions[optionIndex].c_str(), panelW - 44);
    const int textY = rowY + (rowHeight - renderer.text.getLineHeight(itemFont)) / 2;
    renderer.text.render(itemFont, panelX + 18, textY, option.c_str(), !selected,
                         selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  if (static_cast<int>(selectorOptions.size()) > rows) {
    const int trackX = panelX + panelW - 10;
    const int trackY = panelY + headerHeight + 4;
    const int trackH = rows * rowHeight - 8;
    const int thumbH = std::max(8, trackH * rows / static_cast<int>(selectorOptions.size()));
    const int thumbY = trackY + selectorScrollOffset * std::max(1, trackH - thumbH) / maxScroll;
    renderer.rectangle.fill(trackX, trackY, 2, trackH, true);
    renderer.rectangle.fill(trackX - 2, thumbY, 6, thumbH, true);
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

  // Version shown as a small rounded tag: black rounded background with white text.
  const int verFont = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
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
        const int indicatorY = itemY + (itemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
        renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageWidth - indicatorW - 30, indicatorY, indicator,
                             !isSelected);
      }

      renderer.line.render(0, itemY + itemHeight - 1, pageWidth, itemY + itemHeight - 1, true);
      visibleCount++;
      continue;
    }

    if (isSelected) {
      renderer.rectangle.fill(0, itemY, pageWidth, itemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    int textX = entry.group == GroupType::NONE ? 20 : 28;
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
        const int valY = itemY + (itemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
        renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageWidth - valW - 30, valY, val, !isSelected);
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

  if (selectorOpen) {
    renderSelectorOverlay();
  }

  const char* backLbl = selectorOpen ? "Cancel" : (backButtonLabel ? backButtonLabel : "\xC2\xAB Back");
  const char* confirmLbl = selectorOpen ? "Select" : "Open";
  const char* prevLbl = selectorOpen ? "Page -" : "";
  const char* nextLbl = selectorOpen ? "Page +" : "";
  const auto labels = mappedInput.mapLabels(backLbl, confirmLbl, prevLbl, nextLbl);
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
