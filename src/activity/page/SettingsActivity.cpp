#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include "../settings/CategorySettingsActivity.h"
#include "../settings/LibraryIndexer.h"
#include "../settings/OtaUpdateActivity.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

const int LIST_ITEM_HEIGHT = 60;

namespace {
constexpr int systemPageSettingsCount = 22;
const SettingInfo systemPageSettings[systemPageSettingsCount] = {
    SettingInfo::Separator("Display & sleep", GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Sleep Screen", &SystemSetting::sleepScreen,
                      {"Dark", "Light", "Custom", "Recent Book", "Transparent Cover", "None"}, GroupType::DEVICE_DISPLAY),
    SettingInfo::Action("Choose sleep image", GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Sleep Screen Cover Mode", &SystemSetting::sleepScreenCoverMode, {"Fit", "Crop"},
                      GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Sleep Screen Cover Filter", &SystemSetting::sleepScreenCoverFilter,
                      {"None", "Contrast", "Inverted"}, GroupType::DEVICE_DISPLAY),
    SettingInfo::Toggle("Sleep Screen Cover Grayscale", &SystemSetting::sleepScreenCoverGrayscale,
                      GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Hide Battery %", &SystemSetting::hideBatteryPercentage, {"Never", "In Reader", "Always"},
                      GroupType::DEVICE_DISPLAY),
    SettingInfo::Enum("Recent Library Mode", &SystemSetting::recentLibraryMode, {"Grid", "List Stats", "Flow"},
                      GroupType::DEVICE_DISPLAY),

    SettingInfo::Separator("Buttons", GroupType::DEVICE_BUTTONS),
    SettingInfo::Enum(
        "Front Button Layout", &SystemSetting::frontButtonLayout,
        {"Bck, Cnfrm, Lft, Rght", "Lft, Rght, Bck, Cnfrm", "Lft, Bck, Cnfrm, Rght", "Bck, Cnfrm, Rght, Lft"},
        GroupType::DEVICE_BUTTONS),
    SettingInfo::Enum("Short Power Button Click", &SystemSetting::shortPwrBtn, {"Ignore", "Sleep", "Page Refresh"},
                      GroupType::DEVICE_BUTTONS),

    SettingInfo::Separator("Device & library", GroupType::DEVICE_ADVANCED),
    SettingInfo::Enum("Time to Sleep", &SystemSetting::sleepTimeout, {"1 min", "5 min", "10 min", "15 min", "30 min"},
                      GroupType::DEVICE_ADVANCED),
    SettingInfo::Toggle("Use Index for Library", &SystemSetting::useLibraryIndex, GroupType::DEVICE_ADVANCED),
    SettingInfo::Enum("Boot Mode", &SystemSetting::bootSetting, {"Recent Books", "Home Page"}, GroupType::DEVICE_ADVANCED),

    SettingInfo::Separator("Actions", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("Index your library", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("KOReader Sync", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("OPDS Browser", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("Clear Cache", GroupType::DEVICE_ACTIONS),
    SettingInfo::Action("Check for updates", GroupType::DEVICE_ACTIONS),

    /* Standalone row (not inside a collapsible group); always visible. */
    SettingInfo::Action("About", GroupType::NONE)};

constexpr int readerSettingsCount = 26;
const SettingInfo readerSettings[readerSettingsCount] = {
    SettingInfo::Separator("Font", GroupType::FONT),
    SettingInfo::Enum("Font Family", &SystemSetting::fontFamily, {"Bookerly", "Atkinson Hyperlegible", "Literata"},
                      GroupType::FONT),
    SettingInfo::Enum("Font Size", &SystemSetting::fontSize, {"Extra Small", "Small", "Medium", "Large", "X Large"},
                      GroupType::FONT),

    SettingInfo::Separator(" Layout ", GroupType::LAYOUT),
    SettingInfo::Enum("Line Spacing", &SystemSetting::lineSpacing, {"Tight", "Normal", "Wide"}, GroupType::LAYOUT),
    SettingInfo::Value("Screen Margin", &SystemSetting::screenMargin, {5, 80, 5}, GroupType::LAYOUT),
    SettingInfo::Enum("Paragraph Alignment", &SystemSetting::paragraphAlignment,
                      {"Justify", "Left", "Center", "Right", "Default (CSS)"},
                      GroupType::LAYOUT),
    SettingInfo::Toggle("Extra Paragraph Spacing", &SystemSetting::extraParagraphSpacing, GroupType::LAYOUT),
    SettingInfo::Enum("Reading Orientation", &SystemSetting::orientation,
                      {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"}, GroupType::LAYOUT),
    SettingInfo::Toggle("Hyphenation", &SystemSetting::hyphenationEnabled, GroupType::LAYOUT),

    SettingInfo::Separator(" Navigation ", GroupType::READER_CONTROLS),
    SettingInfo::Enum("Next & Previous Mapping", &SystemSetting::readerDirectionMapping,
                      {"Left/Right", "Right/Left", "Up/Down", "Down/Up", "None"}, GroupType::READER_CONTROLS),
    SettingInfo::Enum("Book Settings Toggle", &SystemSetting::readerMenuButton,
                      {"Up", "Down", "Left", "Right", "Confirm"}, GroupType::READER_CONTROLS),
    SettingInfo::Toggle("Long-press Chapter Skip", &SystemSetting::longPressChapterSkip, GroupType::READER_CONTROLS),
    SettingInfo::Enum("Short Power Button", &SystemSetting::readerShortPwrBtn, {"Page Turn", "Page Refresh"},
                      GroupType::READER_CONTROLS),
    SettingInfo::Value("Page Auto Turn", &SystemSetting::pageAutoTurnSeconds, {0, 180, 10}, GroupType::READER_CONTROLS),

    SettingInfo::Separator(" System ", GroupType::SYSTEM),
    SettingInfo::Toggle("Text Anti-Aliasing", &SystemSetting::textAntiAliasing, GroupType::SYSTEM),
    SettingInfo::Enum("Refresh Frequency", &SystemSetting::refreshFrequency,
                      {"1 page", "5 pages", "10 pages", "15 pages", "30 pages"}, GroupType::SYSTEM),
    SettingInfo::Toggle("EPUB Image Grayscale", &SystemSetting::readerImageGrayscale, GroupType::SYSTEM),
    SettingInfo::Toggle("EPUB Smart Refresh (Images)", &SystemSetting::readerSmartRefreshOnImages, GroupType::SYSTEM),

    SettingInfo::Separator(" Status Bar ", GroupType::STATUS_BAR),
    SettingInfo::Enum("Status Bar Mode", &SystemSetting::statusBar,
                      {"None", "No Progress", "Full w/ Percentage", "Full w/ Progress Bar", "Progress Bar", "Battery %",
                       "Percentage", "Page Bars"},
                      GroupType::STATUS_BAR),
    SettingInfo::Enum("Left Section", &SystemSetting::statusBarLeft,
                      {"None", "Page Numbers", "Percentage", "Chapter Title", "Battery Icon", "Battery %",
                       "Battery Icon+%", "Progress Bar", "Progress Bar+%", "Page Bars", "Book Title", "Author Name"},
                      GroupType::STATUS_BAR),
    SettingInfo::Enum("Middle Section", &SystemSetting::statusBarMiddle,
                      {"None", "Page Numbers", "Percentage", "Chapter Title", "Battery Icon", "Battery %",
                       "Battery Icon+%", "Progress Bar", "Progress Bar+%", "Page Bars", "Book Title", "Author Name"},
                      GroupType::STATUS_BAR),
    SettingInfo::Enum("Right Section", &SystemSetting::statusBarRight,
                      {"None", "Page Numbers", "Percentage", "Chapter Title", "Battery Icon", "Battery %",
                       "Battery Icon+%", "Progress Bar", "Progress Bar+%", "Page Bars", "Book Title", "Author Name"},
                      GroupType::STATUS_BAR)};

}  // namespace

/**
 * @brief Static trampoline function for FreeRTOS task creation.
 *
 * @param param Pointer to the SettingsActivity instance
 */
void SettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<SettingsActivity*>(param);
  self->displayTaskLoop();
}

/**
 * @brief Main loop for the display rendering task.
 *
 * Periodically checks if a UI update is required and performs rendering
 * when conditions are met (no sub-activity active and not indexing).
 */
void SettingsActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity && !isIndexing && !showingAbout) {
      updateRequired = false;
      openCurrentPanel();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/**
 * @brief Initializes the settings activity when it becomes active.
 *
 * Creates the rendering mutex, sets initial navigation state, and starts
 * the display rendering task.
 */
void SettingsActivity::onEnter() {
  Activity::onEnter();

  tabSelectorIndex = 2;
  currentPanel = SettingsPanel::System;

  isIndexing = false;
  showingAbout = false;
  indexingProgress = 0;
  indexingTotal = 0;
  memset(currentIndexingPath, 0, sizeof(currentIndexingPath));

  openCurrentPanel();

  xTaskCreate(&SettingsActivity::taskTrampoline, "SettingsActivityTask", 4096, this, 1, &displayTaskHandle);
}

/**
 * @brief Cleans up resources when exiting the settings activity.
 *
 * Deletes the display task and rendering mutex to prevent resource leaks.
 */
void SettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  if (aboutPage) {
    delete aboutPage;
    aboutPage = nullptr;
  }

  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
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
    showIndexingProgress();
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    swapPanelAndReopen();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    handleTabNavigation(true, false);
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
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
  const char* title = (currentPanel == SettingsPanel::System) ? "System" : "Reader";
  const SettingInfo* list = (currentPanel == SettingsPanel::System) ? systemPageSettings : readerSettings;
  const int count = (currentPanel == SettingsPanel::System) ? systemPageSettingsCount : readerSettingsCount;

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
          aboutPage = new AboutPage(
              renderer, mappedInput,
              [this]() {
                showingAbout = false;
                openCurrentPanel();
              },
              [this]() {
                showingAbout = false;
                enterNewActivity(new OtaUpdateActivity(renderer, mappedInput, [this]() {
                  exitActivity();
                  openCurrentPanel();
                }));
              });
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
  renderer.drawRect(popupX + 20, progressBarY + 20, 260, 15);

  if (indexingTotal > 0) {
    int percentage = (indexingProgress * 100) / indexingTotal;
    int fillWidth = (260 * percentage) / 100;
    if (fillWidth > 0) renderer.fillRect(popupX + 20, progressBarY + 20, fillWidth, 15);
  }

  char countMsg[64];
  if (indexingTotal > 0) {
    snprintf(countMsg, sizeof(countMsg), "%d of %d files", indexingProgress, indexingTotal);
  } else {
    snprintf(countMsg, sizeof(countMsg), "Found %d files...", indexingProgress);
  }
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, popupX + 20, progressBarY + 50, countMsg);

  renderer.displayBuffer();
}
