/**
 * @file SettingsActivity.cpp
 * @brief Definitions for SettingsActivity.
 */

#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../settings/CategorySettingsActivity.h"
#include "../settings/LibraryIndexer.h"
#include "../settings/ReaderPresetsActivity.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

const int LIST_ITEM_HEIGHT = 60;

namespace {
const SettingInfo systemPageSettingsX3[] = {
    SettingInfo::Separator("Display ", GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Sleep Screen", &SystemSetting::sleepScreen,
                      {"Dark", "Light", "Custom", "Recent Book", "Transparent Cover", "None", "Date Time"},
                      GroupType::DEVICE_DISPLAY),
    SettingInfo::Action("Choose sleep image", GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Hide Battery %", &SystemSetting::hideBatteryPercentage, {"Never", "In Reader", "Always"},
                      GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Recent Library Mode", &SystemSetting::recentLibraryMode,
                      {"Grid", "Current | Previous", "Flow", "Simple", "List", "Icons", "Cover"},
                      GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Library Mode", &SystemSetting::libraryMode, {"List", "Grid"}, GroupType::DEVICE_DISPLAY),
    SettingInfo::Value("Recent books shown", &SystemSetting::recentVisibleCount, {1, 8, 1}, GroupType::DEVICE_DISPLAY),

    SettingInfo::Separator("Clock", GroupType::CLOCK),
    SettingInfo::Action("Face", GroupType::CLOCK),
    SettingInfo::Enum("Format", &SystemSetting::sleepClockTimeFormat, {"12 hour", "24 hour"}, GroupType::CLOCK),
    SettingInfo::Value("Timezone", &SystemSetting::timeZoneQuarterOffset, {0, 104, 1}, GroupType::CLOCK),
    SettingInfo::Action("Sync", GroupType::CLOCK),

    SettingInfo::Separator("Image", GroupType::IMAGE),
    SettingInfo::Enum("Cover Mode", &SystemSetting::sleepScreenCoverMode, {"Fill", "Crop"}, GroupType::IMAGE),
    SettingInfo::Enum("Cover Filter", &SystemSetting::sleepScreenCoverFilter, {"None", "Contrast", "Inverted"},
                      GroupType::IMAGE),
    SettingInfo::Enum("Sleep Image Quality", &SystemSetting::sleepImageQuality, {"Low", "Medium", "High"},
                      GroupType::IMAGE),
    SettingInfo::Toggle("Rounded thumbnails", &SystemSetting::bitmapRoundedCorners, GroupType::IMAGE),

    SettingInfo::Separator("Buttons", GroupType::DEVICE_BUTTONS),
    SettingInfo::Enum("Front Button", &SystemSetting::frontButtonLayout,
                      {"Back, Ccnfirm, Left, Right", "Left, Right, Back, Confirm", "Left, Back, Confirm, Right",
                       "Back, Confirm, Right, Left"},
                      GroupType::DEVICE_BUTTONS),
    SettingInfo::Enum("Short Power Button Click", &SystemSetting::shortPwrBtn, {"Ignore", "Sleep", "Page Refresh"},
                      GroupType::DEVICE_BUTTONS),
    SettingInfo::Enum("Main Menu Buttons", &SystemSetting::mainMenuNav, {"Front (Left/Right)", "Side (Up/Down)"},
                      GroupType::DEVICE_BUTTONS),

    SettingInfo::Separator("Device ", GroupType::DEVICE_ADVANCED),
    SettingInfo::Enum("Time to Sleep", &SystemSetting::sleepTimeout, {"1 min", "5 min", "10 min", "15 min", "30 min"},
                      GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Use Index for Library", &SystemSetting::useLibraryIndex, GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Library custom sort", &SystemSetting::librarySortEnabled, GroupType::DEVICE_ADVANCED),
    SettingInfo::Enum("Boot Mode", &SystemSetting::bootSetting, {"Recent Books", "Home Page"},
                      GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Refresh on load (Recent)", &SystemSetting::refreshOnLoadRecent, GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Refresh on load (Library)", &SystemSetting::refreshOnLoadLibrary, GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Refresh on load (Settings)", &SystemSetting::refreshOnLoadSettings,
                        GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Refresh on load (Sync)", &SystemSetting::refreshOnLoadSync, GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Refresh on load (Stats)", &SystemSetting::refreshOnLoadStatistics, GroupType::DEVICE_ADVANCED),

    SettingInfo::Separator("Actions", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("Delete Cache", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("Index your library", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("Generate thumbnails", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("KOReader Sync", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("OPDS Browser", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("Check for updates", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("About", GroupType::NONE)};
constexpr int systemPageSettingsX3Count = sizeof(systemPageSettingsX3) / sizeof(systemPageSettingsX3[0]);

const SettingInfo systemPageSettingsX4[] = {
    SettingInfo::Separator("Display ", GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Sleep Screen", &SystemSetting::sleepScreen,
                      {"Dark", "Light", "Custom", "Recent Book", "Transparent Cover", "None"},
                      GroupType::DEVICE_DISPLAY),
    SettingInfo::Action("Choose sleep image", GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Hide Battery %", &SystemSetting::hideBatteryPercentage, {"Never", "In Reader", "Always"},
                      GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Recent Library Mode", &SystemSetting::recentLibraryMode,
                      {"Grid", "Current | Previous", "Flow", "Simple", "List", "Icons", "Cover"},
                      GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Library Mode", &SystemSetting::libraryMode, {"List", "Grid"}, GroupType::DEVICE_DISPLAY),
    SettingInfo::Value("Recent books shown", &SystemSetting::recentVisibleCount, {1, 8, 1}, GroupType::DEVICE_DISPLAY),

    SettingInfo::Separator("Image", GroupType::IMAGE),
    SettingInfo::Enum("Cover Mode", &SystemSetting::sleepScreenCoverMode, {"Fill", "Crop"}, GroupType::IMAGE),
    SettingInfo::Enum("Cover Filter", &SystemSetting::sleepScreenCoverFilter, {"None", "Contrast", "Inverted"},
                      GroupType::IMAGE),
    SettingInfo::Enum("Sleep Image Quality", &SystemSetting::sleepImageQuality, {"Low", "Medium", "High"},
                      GroupType::IMAGE),
    SettingInfo::Toggle("Rounded thumbnails", &SystemSetting::bitmapRoundedCorners, GroupType::IMAGE),

    SettingInfo::Separator("Buttons", GroupType::DEVICE_BUTTONS),
    SettingInfo::Enum("Front Button", &SystemSetting::frontButtonLayout,
                      {"Back, Ccnfirm, Left, Right", "Left, Right, Back, Confirm", "Left, Back, Confirm, Right",
                       "Back, Confirm, Right, Left"},
                      GroupType::DEVICE_BUTTONS),
    SettingInfo::Enum("Short Power Button Click", &SystemSetting::shortPwrBtn, {"Ignore", "Sleep", "Page Refresh"},
                      GroupType::DEVICE_BUTTONS),
    SettingInfo::Enum("Main Menu Buttons", &SystemSetting::mainMenuNav, {"Front (Left/Right)", "Side (Up/Down)"},
                      GroupType::DEVICE_BUTTONS),

    SettingInfo::Separator("Device ", GroupType::DEVICE_ADVANCED),
    SettingInfo::Enum("Time to Sleep", &SystemSetting::sleepTimeout, {"1 min", "5 min", "10 min", "15 min", "30 min"},
                      GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Use Index for Library", &SystemSetting::useLibraryIndex, GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Library custom sort", &SystemSetting::librarySortEnabled, GroupType::DEVICE_ADVANCED),
    SettingInfo::Enum("Boot Mode", &SystemSetting::bootSetting, {"Recent Books", "Home Page"},
                      GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Refresh on load (Recent)", &SystemSetting::refreshOnLoadRecent, GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Refresh on load (Library)", &SystemSetting::refreshOnLoadLibrary, GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Refresh on load (Settings)", &SystemSetting::refreshOnLoadSettings,
                        GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Refresh on load (Sync)", &SystemSetting::refreshOnLoadSync, GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Refresh on load (Stats)", &SystemSetting::refreshOnLoadStatistics, GroupType::DEVICE_ADVANCED),

    SettingInfo::Separator("Actions", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("Delete Cache", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("Index your library", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("Generate thumbnails", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("KOReader Sync", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("OPDS Browser", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("Check for updates", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("About", GroupType::NONE)};
constexpr int systemPageSettingsX4Count = sizeof(systemPageSettingsX4) / sizeof(systemPageSettingsX4[0]);

}  // namespace

/**
 * @brief Initializes the settings activity when it becomes active.
 *
 * Sets initial navigation state and opens the category panel (same pattern as Recent/Library: no display worker task).
 */
void SettingsActivity::onEnter() {
  Activity::onEnter();

  tabSelectorIndex = 2;
  currentPanel = SettingsPanel::System;

  isIndexing = false;
  showingAbout = false;
  indexingProgress = 0;
  indexingTotal = 0;
  lastRenderedIndexingProgress = -1;
  lastRenderedIndexingTotal = -1;
  nextIndexingRenderMs = 0;
  memset(currentIndexingPath, 0, sizeof(currentIndexingPath));

  openCurrentPanel();
}

/**
 * @brief Cleans up resources when exiting the settings activity.
 */
void SettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  if (aboutPage) {
    delete aboutPage;
    aboutPage = nullptr;
  }
}

/**
 * @brief Main update loop for handling user input and navigation.
 *
 * Processes button presses for category navigation, entering sub-categories,
 * library indexing, and power button refresh functionality.
 */
void SettingsActivity::loop() {
  if (showingAbout && aboutPage) {
    aboutPage->handleInput();
    if (aboutPage->isDismissed()) {
      showingAbout = false;
      openCurrentPanel();
    }
    return;
  }

  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (isIndexing) {
    const bool progressChanged =
        indexingProgress != lastRenderedIndexingProgress || indexingTotal != lastRenderedIndexingTotal;
    const unsigned long now = millis();
    if (progressChanged || now >= nextIndexingRenderMs) {
      showIndexingProgress();
      lastRenderedIndexingProgress = indexingProgress;
      lastRenderedIndexingTotal = indexingTotal;
      nextIndexingRenderMs = now + 250;
    }
    vTaskDelay(pdMS_TO_TICKS(40));
    return;
  }

  if (updateRequired && !isIndexing && !showingAbout) {
    updateRequired = false;
    openCurrentPanel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    swapPanelAndReopen();
    return;
  }

  if (mappedInput.wasPressed(tabPrevButton())) {
    handleTabNavigation(true, false);
    return;
  }

  if (mappedInput.wasPressed(tabNextButton())) {
    handleTabNavigation(false, true);
    return;
  }

  if (tabSelectorIndex != 2) {
    return;
  }
}

const char* SettingsActivity::panelBackLabel(const SettingsPanel panel) {
  return panel == SettingsPanel::System ? "\xC2\xAB Reader" : "\xC2\xAB System";
}

void SettingsActivity::swapPanelAndReopen() {
  SETTINGS.saveToFile();
  currentPanel = (currentPanel == SettingsPanel::System) ? SettingsPanel::Reader : SettingsPanel::System;
  exitActivity();
  openCurrentPanel();
}

void SettingsActivity::openCurrentPanel() {
  if (currentPanel == SettingsPanel::Reader) {
    // The Reader panel is now a list of named presets (with a live-preview editor) instead of a flat list.
    enterNewActivity(new ReaderPresetsActivity(
        renderer, mappedInput, [this] { swapPanelAndReopen(); },
        [this] {
          if (onRecentOpen) onRecentOpen();
        },
        [this] {
          if (onLibraryOpen) onLibraryOpen();
        },
        [this] {
          if (onSyncOpen) onSyncOpen();
        },
        [this] {
          if (onStatisticsOpen) onStatisticsOpen();
        }));
    return;
  }

  const char* title = "System settings";
  const SettingInfo* list = renderer.deviceIsX3() ? systemPageSettingsX3 : systemPageSettingsX4;
  const int count = renderer.deviceIsX3() ? systemPageSettingsX3Count : systemPageSettingsX4Count;

  enterNewActivity(new CategorySettingsActivity(
      renderer, mappedInput, title, list, count, [this] { swapPanelAndReopen(); },
      [this] {
        exitActivity();
        startLibraryIndexing();
      },
      [this] {
        exitActivity();
        showingAbout = true;
        if (!aboutPage) {
          aboutPage = new AboutPage(renderer, mappedInput);
        }
        aboutPage->show();
      },
      panelBackLabel(currentPanel),
      [this] {
        if (onRecentOpen) onRecentOpen();
      },
      [this] {
        if (onLibraryOpen) onLibraryOpen();
      },
      [this] {
        if (onSyncOpen) onSyncOpen();
      },
      [this] {
        if (onStatisticsOpen) onStatisticsOpen();
      }));
}

/**
 * @brief Initiates the library indexing process in a background task.
 *
 * Starts a FreeRTOS task that counts all books and indexes them, updating
 * progress in real-time.
 */
void SettingsActivity::startLibraryIndexing() {
  isIndexing = true;
  indexingProgress = 0;
  indexingTotal = 0;
  lastRenderedIndexingProgress = -1;
  lastRenderedIndexingTotal = -1;
  nextIndexingRenderMs = 0;
  memset(currentIndexingPath, 0, sizeof(currentIndexingPath));

  showIndexingProgress();

  xTaskCreate(
      [](void* param) {
        auto* activity = static_cast<SettingsActivity*>(param);

        FsFile root = SdMan.open("/");
        if (root) {
          activity->indexingTotal = LibraryIndexer::countBooks(root);
          root.close();
        }

        vTaskDelay(pdMS_TO_TICKS(10));

        LibraryIndexer::indexAll([activity](int current, int total, const char* path) {
          activity->indexingProgress = current;
          activity->indexingTotal = total;
          if (path) strlcpy(activity->currentIndexingPath, path, sizeof(activity->currentIndexingPath));
          if (current % 10 == 0) vTaskDelay(pdMS_TO_TICKS(1));
        });

        activity->isIndexing = false;
        activity->updateRequired = true;
        vTaskDelete(nullptr);
      },
      "LibraryIndexTask", 4096, this, 1, nullptr);
}

/**
 * @brief Displays the library indexing progress dialog.
 *
 * Shows a popup with a progress bar and file count during the indexing process.
 */
void SettingsActivity::showIndexingProgress() {
  renderer.clearScreen();
  renderTabBar(renderer);

  int screenWidth = renderer.getScreenWidth();
  int screenHeight = renderer.getScreenHeight();

  char titleMsg[64];
  if (indexingTotal == 0) {
    snprintf(titleMsg, sizeof(titleMsg), "Counting files...");
  } else {
    int percentage = (indexingProgress * 100) / indexingTotal;
    snprintf(titleMsg, sizeof(titleMsg), "Indexing: %d%%", percentage);
  }

  ScreenComponents::drawPopup(renderer, titleMsg);

  int popupX = (screenWidth - 300) / 2;
  int progressBarY = (screenHeight - 100) / 2 + 40;
  renderer.rectangle.render(popupX + 20, progressBarY + 20, 260, 15);

  if (indexingTotal > 0) {
    int percentage = (indexingProgress * 100) / indexingTotal;
    int fillWidth = (260 * percentage) / 100;
    if (fillWidth > 0) renderer.rectangle.fill(popupX + 20, progressBarY + 20, fillWidth, 15);
  }

  char countMsg[64];
  if (indexingTotal > 0) {
    snprintf(countMsg, sizeof(countMsg), "%d of %d files", indexingProgress, indexingTotal);
  } else {
    snprintf(countMsg, sizeof(countMsg), "Found %d files...", indexingProgress);
  }
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, popupX + 20, progressBarY + 50, countMsg);

  renderer.displayBuffer();
}
