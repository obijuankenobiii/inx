#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include "../settings/CategorySettingsActivity.h"
#include "../settings/LibraryIndexer.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"


namespace {
/**
 * @brief RAII wrapper for thread-safe mutex operations.
 * 
 * Automatically acquires a mutex on construction and releases it on destruction,
 * ensuring proper resource management even in exception scenarios.
 */
class MutexGuard {
 private:
  SemaphoreHandle_t& mutex;
  bool acquired;

 public:
  explicit MutexGuard(SemaphoreHandle_t& m) : mutex(m), acquired(false) {
    if (mutex) {
      acquired = (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE);
    }
  }

  ~MutexGuard() {
    if (acquired && mutex) {
      xSemaphoreGive(mutex);
    }
  }

  bool isAcquired() const { return acquired; }
};

}  // namespace

const char* SettingsActivity::categoryNames[categoryCount] = {"Display", "Reader", "Controls", "System"};
constexpr int LIST_ITEM_HEIGHT = 60;

namespace {
constexpr int displaySettingsCount = 5;
const SettingInfo displaySettings[displaySettingsCount] = {
    SettingInfo::Enum("Sleep Screen", &SystemSetting::sleepScreen, {"Dark", "Light", "Custom", "Recent Book", "Transparent Cover", "None"}),
    SettingInfo::Enum("Sleep Screen Cover Mode", &SystemSetting::sleepScreenCoverMode, {"Fit", "Crop"}),
    SettingInfo::Enum("Sleep Screen Cover Filter", &SystemSetting::sleepScreenCoverFilter,
                      {"None", "Contrast", "Inverted"}),

    SettingInfo::Enum("Hide Battery %", &SystemSetting::hideBatteryPercentage, {"Never", "In Reader", "Always"}),

    SettingInfo::Enum("Recent Library Mode", &SystemSetting::recentLibraryMode, {"Grid", "Default"})};

constexpr int readerSettingsCount = 29;
const SettingInfo readerSettings[readerSettingsCount] = {
    SettingInfo::Separator("═══ Font ═══", GroupType::FONT),
    SettingInfo::Enum("Font Family", &SystemSetting::fontFamily, {"Bookerly", "Atkinson Hyperlegible", "Literata"}, GroupType::FONT),
    SettingInfo::Enum("Font Size", &SystemSetting::fontSize, {"Extra Small", "Small", "Medium", "Large", "X Large"}, GroupType::FONT),
    
    SettingInfo::Separator("═══ Layout ═══", GroupType::LAYOUT),
    SettingInfo::Enum("Line Spacing", &SystemSetting::lineSpacing, {"Tight", "Normal", "Wide"}, GroupType::LAYOUT),
    SettingInfo::Value("Screen Margin", &SystemSetting::screenMargin, {5, 80, 5}, GroupType::LAYOUT),
    SettingInfo::Enum("Paragraph Alignment", &SystemSetting::paragraphAlignment,
                      {"Justify", "Left", "Center", "Right"}, GroupType::LAYOUT),
    SettingInfo::Toggle("Extra Paragraph Spacing", &SystemSetting::extraParagraphSpacing, GroupType::LAYOUT),
    SettingInfo::Enum("Reading Orientation", &SystemSetting::orientation,
                      {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"}, GroupType::LAYOUT),
    SettingInfo::Toggle("Hyphenation", &SystemSetting::hyphenationEnabled, GroupType::LAYOUT),
    
    SettingInfo::Separator("═══ Navigation ═══", GroupType::READER_CONTROLS),
    SettingInfo::Enum("Next & Previous Mapping", &SystemSetting::readerDirectionMapping,
                      {"Left/Right", "Right/Left", "Up/Down", "Down/Up", "None"}, GroupType::READER_CONTROLS),
    SettingInfo::Enum("Book Settings Toggle", &SystemSetting::readerMenuButton,
                      {"Up", "Down", "Left", "Right", "Confirm"}, GroupType::READER_CONTROLS),
    SettingInfo::Toggle("Long-press Chapter Skip", &SystemSetting::longPressChapterSkip, GroupType::READER_CONTROLS),
    SettingInfo::Enum("Short Power Button", &SystemSetting::readerShortPwrBtn,
                      {"Page Turn", "Page Refresh"}, GroupType::READER_CONTROLS),
    
    SettingInfo::Separator("═══ System ═══", GroupType::SYSTEM),
    SettingInfo::Toggle("Text Anti-Aliasing", &SystemSetting::textAntiAliasing, GroupType::SYSTEM),
    SettingInfo::Enum("Refresh Frequency", &SystemSetting::refreshFrequency,
                      {"1 page", "5 pages", "10 pages", "15 pages", "30 pages"}, GroupType::SYSTEM),
    
    SettingInfo::Separator("═══ Status Bar ═══", GroupType::STATUS_BAR),
    SettingInfo::Enum("Status Bar Mode", &SystemSetting::statusBar,
                      {"None", "No Progress", "Full w/ Percentage", "Full w/ Progress Bar", "Progress Bar",
                       "Battery %", "Percentage", "Page Bars"}, GroupType::STATUS_BAR),
    SettingInfo::Enum("Left Section", &SystemSetting::statusBarLeft, 
                      {"None", "Page Numbers", "Percentage", "Chapter Title", "Battery Icon",
                       "Battery %", "Battery Icon+%", "Progress Bar", "Progress Bar+%", "Page Bars",
                       "Book Title", "Author Name"}, GroupType::STATUS_BAR),
    SettingInfo::Enum("Middle Section", &SystemSetting::statusBarMiddle,
                      {"None", "Page Numbers", "Percentage", "Chapter Title", "Battery Icon",
                       "Battery %", "Battery Icon+%", "Progress Bar", "Progress Bar+%", "Page Bars",
                       "Book Title", "Author Name"}, GroupType::STATUS_BAR),
    SettingInfo::Enum("Right Section", &SystemSetting::statusBarRight,
                      {"None", "Page Numbers", "Percentage", "Chapter Title", "Battery Icon",
                       "Battery %", "Battery Icon+%", "Progress Bar", "Progress Bar+%", "Page Bars",
                       "Book Title", "Author Name"}, GroupType::STATUS_BAR)};

constexpr int controlsSettingsCount = 2;
const SettingInfo controlsSettings[controlsSettingsCount] = {
    SettingInfo::Enum(
        "Front Button Layout", &SystemSetting::frontButtonLayout,
        {"Bck, Cnfrm, Lft, Rght", "Lft, Rght, Bck, Cnfrm", "Lft, Bck, Cnfrm, Rght", "Bck, Cnfrm, Rght, Lft"}),
    SettingInfo::Enum("Short Power Button Click", &SystemSetting::shortPwrBtn, {"Ignore", "Sleep", "Page Refresh"})
};

constexpr int systemSettingsCount = 7;
const SettingInfo systemSettings[systemSettingsCount] = {
    SettingInfo::Enum("Time to Sleep", &SystemSetting::sleepTimeout, {"1 min", "5 min", "10 min", "15 min", "30 min"}),
    SettingInfo::Toggle("Use Index for Library", &SystemSetting::useLibraryIndex),
    SettingInfo::Enum("Boot Mode", &SystemSetting::bootSetting, {"Recent Books", "Home Page"}),
    SettingInfo::Action("KOReader Sync"),
    SettingInfo::Action("OPDS Browser"),
    SettingInfo::Action("Clear Cache"),
    SettingInfo::Action("Check for updates")};
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
    if (updateRequired && !subActivity && !isIndexing) {
      MutexGuard guard(renderingMutex);
      if (guard.isAcquired()) {
        updateRequired = false;
        render();
      }
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
  renderingMutex = xSemaphoreCreateMutex();

  tabSelectorIndex = 2;
  selectedCategoryIndex = 0;

  isIndexing = false;
  indexingProgress = 0;
  indexingTotal = 0;
  memset(currentIndexingPath, 0, sizeof(currentIndexingPath));

  render();
  
  xTaskCreate(&SettingsActivity::taskTrampoline, "SettingsActivityTask", 4096, this, 1, &displayTaskHandle);
}

/**
 * @brief Cleans up resources when exiting the settings activity.
 * 
 * Deletes the display task and rendering mutex to prevent resource leaks.
 */
void SettingsActivity::onExit() {
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
 * @brief Main update loop for handling user input and navigation.
 * 
 * Processes button presses for category navigation, entering sub-categories,
 * library indexing, and power button refresh functionality.
 */
void SettingsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    updateRequired = true;
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

  if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
    if (renderingMutex != nullptr && mappedInput.getHeldTime() >= 300) {
      if (xSemaphoreTake(renderingMutex, portMAX_DELAY) == pdTRUE) {
        vTaskDelay(pdMS_TO_TICKS(300));
        SETTINGS.saveToFile();
        onRecentOpen();
      }
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    tabSelectorIndex = 1;
    navigateToSelectedMenu();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    tabSelectorIndex = 3;
    navigateToSelectedMenu();
    return;
  }

  if (tabSelectorIndex != 2) {
    return;
  }

  const int totalItems = categoryCount + 1;
  bool needUpdate = false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedCategoryIndex = (selectedCategoryIndex - 1 + totalItems) % totalItems;
    needUpdate = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedCategoryIndex = (selectedCategoryIndex + 1) % totalItems;
    needUpdate = true;
  }

  if (needUpdate) {
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedCategoryIndex < categoryCount) {
      enterCategory(selectedCategoryIndex);
      return;
    }
    startLibraryIndexing();
  }
}

/**
 * @brief Navigates to a specific settings category.
 * 
 * @param categoryIndex Index of the category to enter (0-3)
 */
void SettingsActivity::enterCategory(int categoryIndex) {
  if ((categoryIndex < 0) || (categoryIndex >= categoryCount)) {
    return;
  }

  exitActivity();

  const SettingInfo* settingsList = nullptr;
  int settingsCount = 0;

  switch (categoryIndex) {
    case 0:
      settingsList = displaySettings;
      settingsCount = displaySettingsCount;
      break;
    case 1:
      settingsList = readerSettings;
      settingsCount = readerSettingsCount;
      break;
    case 2:
      settingsList = controlsSettings;
      settingsCount = controlsSettingsCount;
      break;
    case 3:
      settingsList = systemSettings;
      settingsCount = systemSettingsCount;
      break;
  }

  enterNewActivity(new CategorySettingsActivity(renderer, mappedInput, categoryNames[categoryIndex], settingsList,
                                                settingsCount, [this] {
                                                  exitActivity();
                                                  updateRequired = true;
                                                }));
}

/**
 * @brief Renders the main settings screen.
 * 
 * Draws the tab bar, header, subtitle, settings list, and button hints.
 */
void SettingsActivity::render() const {
  renderer.clearScreen();
  renderTabBar(renderer);

  const int startY = TAB_BAR_HEIGHT;
  const int headerHeight = TAB_BAR_HEIGHT;
  const int headerY = startY;
  const char* headerText = "Settings";
  int headerTextX = 20;
  int headerTextY = headerY + (headerHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;

  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, headerTextX, headerTextY - 10, headerText, true, EpdFontFamily::BOLD);

  const char* subtitleText = "Manage your preference.";
  int subtitleY = headerY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, renderer.getScreenWidth(), dividerY);

  renderSettingsList();

  const auto labels = mappedInput.mapLabels("« Recent", "Select", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

/**
 * @brief Renders the scrollable list of settings categories.
 * 
 * Draws each category item with selection highlighting and the library indexing option.
 */
void SettingsActivity::renderSettingsList() const {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  int startY = TAB_BAR_HEIGHT * 2 + 8;
  int drawY = startY;
  int visibleAreaHeight = screenHeight - drawY - 80;

  for (int i = 0; i < categoryCount; i++) {
    if (i * LIST_ITEM_HEIGHT < visibleAreaHeight) {
      int itemY = drawY + (i * LIST_ITEM_HEIGHT);
      bool isSelected = (i == selectedCategoryIndex);

      if (isSelected) {
        renderer.fillRect(0, itemY, screenWidth, LIST_ITEM_HEIGHT);
      }

      const char* arrowRight = "›";
      const char* categoryName = categoryNames[i];
      int textX = 20;
      int textY = itemY + (LIST_ITEM_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;

      renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, renderer.getScreenWidth() - 30, textY, arrowRight, isSelected ? 0 : 1);
      renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, textX, textY, categoryName, isSelected ? 0 : 1);

      renderer.drawLine(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1);
    }
  }

  int indexButtonY = drawY + (categoryCount * LIST_ITEM_HEIGHT);
  bool isIndexSelected = (selectedCategoryIndex == categoryCount);

  if (isIndexSelected) {
    renderer.fillRect(0, indexButtonY, screenWidth, LIST_ITEM_HEIGHT);
  }

  renderer.drawRect(20, indexButtonY + 20, 16, 16, !isIndexSelected);
  renderer.fillRect(24, indexButtonY + 24, 8, 8, !isIndexSelected);

  int textY = indexButtonY + (LIST_ITEM_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 50, textY, "Index Your Library", isIndexSelected ? 0 : 1);

  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, screenWidth - 30, textY, "›", isIndexSelected ? 0 : 1);
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
      "LibraryIndexTask", 8192, this, 1, nullptr);
}

/**
 * @brief Displays the library indexing progress dialog.
 * 
 * Shows a popup with a progress bar and file count during the indexing process.
 */
void SettingsActivity::showIndexingProgress() {
  if (xSemaphoreTake(renderingMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }

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
  xSemaphoreGive(renderingMutex);
}