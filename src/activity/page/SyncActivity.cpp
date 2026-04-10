#include "../page/SyncActivity.h"

#include <GfxRenderer.h>

#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
/**
 * Thread-safe mutex operations wrapper
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

constexpr int MENU_ITEM_COUNT = 3;
const char* MENU_ITEMS[MENU_ITEM_COUNT] = {"Join a Network", "Connect to Calibre", "Create Hotspot"};
const char* MENU_DESCRIPTIONS[MENU_ITEM_COUNT] = {
    "Connect to an existing WiFi network",
    "Use Calibre wireless device transfers",
    "Create a WiFi network others can join",
};
constexpr int LIST_ITEM_HEIGHT = 80;
}  // namespace

/**
 * Static task trampoline that forwards to the displayTaskLoop member function.
 */
void SyncActivity::taskTrampoline(void* param) {
  auto* self = static_cast<SyncActivity*>(param);
  self->displayTaskLoop();
}

/**
 * Lifecycle hook called when entering the activity.
 * Initializes rendering resources and starts the display task.
 */
void SyncActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  if (!renderingMutex) return;

  selectedIndex = 0;
  
  // Initial render immediately
  render();

  if (displayTaskHandle == nullptr) {
    xTaskCreate(&SyncActivity::taskTrampoline, "SyncTask", 8192, this, 1, &displayTaskHandle);
  }
}

/**
 * Main loop for handling user input and updating the display state.
 * Processes button presses for menu navigation and tab switching.
 */
void SyncActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    updateRequired = true;
    return;
  }
  const bool confirmPressed = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (renderingMutex != nullptr && mappedInput.getHeldTime() >= 300) {
      if (xSemaphoreTake(renderingMutex, portMAX_DELAY) == pdTRUE) {
        vTaskDelay(pdMS_TO_TICKS(300));
        onRecentOpen();
      }
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    tabSelectorIndex = 2;
    navigateToSelectedMenu();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    tabSelectorIndex = 4;
    navigateToSelectedMenu();
    return;
  }

  if (tabSelectorIndex != 3) {
    return;
  }

  if (confirmPressed) {
    NetworkMode mode = NetworkMode::JOIN_NETWORK;
    if (selectedIndex == 1) {
      mode = NetworkMode::CONNECT_CALIBRE;
    } else if (selectedIndex == 2) {
      mode = NetworkMode::CREATE_HOTSPOT;
    }

    if (onModeSelected) {
      onModeSelected(mode);
    }
    return;
  }

  bool needUpdate = false;

  if (upPressed) {
    selectedIndex = (selectedIndex + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
    needUpdate = true;
  }

  if (downPressed) {
    selectedIndex = (selectedIndex + 1) % MENU_ITEM_COUNT;
    needUpdate = true;
  }

  if (needUpdate) {
    updateRequired = true;
  }
}

/**
 * Background task loop that periodically checks if a display update is required.
 * Renders the display when updateRequired is true.
 */
void SyncActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
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
 * Renders the complete sync activity view including menu items and tab bar.
 */
void SyncActivity::render() const {
  renderer.clearScreen();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  renderTabBar(renderer);

  const int startY = TAB_BAR_HEIGHT;
  const int headerHeight = TAB_BAR_HEIGHT;
  const int headerY = startY;

  const char* headerText = "File Transfer";
  int headerTextY = headerY + (headerHeight - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, headerTextY - 10, headerText, true, EpdFontFamily::BOLD);

  const char* subtitleText = "How would you like to connect?";
  int subtitleY = headerY + 40;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText, true);

  const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
  renderer.drawLine(0, dividerY, screenWidth, dividerY);

  const int listStartY = dividerY;
  const int visibleAreaHeight = screenHeight - listStartY - 80;

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    const int itemY = listStartY + i * LIST_ITEM_HEIGHT;

    if (itemY < listStartY + visibleAreaHeight && itemY + LIST_ITEM_HEIGHT > listStartY) {
      const bool isSelected = (i == selectedIndex);

      if (isSelected) {
        renderer.fillRect(0, itemY, screenWidth, LIST_ITEM_HEIGHT);
      }

      const int textX = 20;
      const int titleY = itemY + 10;
      const int descriptionY = itemY + 45;

      renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, textX, titleY, MENU_ITEMS[i], !isSelected);
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, descriptionY, MENU_DESCRIPTIONS[i], !isSelected);

      if (i < MENU_ITEM_COUNT - 1) {
        renderer.drawLine(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1);
      }
    }
  }

  const auto labels = mappedInput.mapLabels("« Recent", "Select", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

/**
 * Lifecycle hook called when exiting the activity.
 * Cleans up rendering resources and stops the display task.
 */
void SyncActivity::onExit() {
  Activity::onExit();

  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
}