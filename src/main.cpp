#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <builtinFonts/all.h>

#include <cstring>
#include <new>
#include <string>

#include "activity/network/CalibreConnectActivity.h"
#include "activity/network/HotspotActivity.h"
#include "activity/network/LocalNetworkActivity.h"
#include "activity/page/LibraryActivity.h"
#include "activity/page/RecentActivity.h"
#include "activity/page/SettingsActivity.h"
#include "activity/page/StatisticActivity.h"
#include "activity/page/SyncActivity.h"
#include "activity/reader/ReaderActivity.h"
#include "activity/system/BootActivity.h"
#include "activity/system/SleepActivity.h"
#include "activity/util/FullScreenMessageActivity.h"
#include "state/SystemSetting.h"
#include "system/Battery.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

HalDisplay display;
HalGPIO gpio;
MappedInputManager input(gpio);
GfxRenderer render(display);

static uint8_t activityArena[65536];
Activity* currentActivity = nullptr;

unsigned long t1 = 0;
unsigned long t2 = 0;

void verifyPowerButtonDuration();
void waitForPowerRelease();
void enterDeepSleep();
void onGoToReader(const std::string& path);
void onSelectBook(const std::string& path);
void onGoToRecent();
void onGoToStatistics();
void onGoToFileTransfer();
void onGoToSettings();
void onGoToLibrary(const std::string& path = "/");
void setupDisplayAndFonts();
void onNetworkModeSelected(NetworkMode mode);
void openReaderFromCallback(const std::string& path);

/**
 * @brief Switches the current activity to a new activity type using placement new.
 *
 * Destroys the existing activity, clears the memory arena, and constructs
 * the new activity in the same memory location.
 *
 * @tparam T The activity type to switch to
 * @tparam Args The argument types for the activity constructor
 * @param args The arguments to forward to the activity constructor
 */
template <typename T, typename... Args>
void switchTo(Args&&... args) {
  if (sizeof(T) > sizeof(activityArena)) {
    Serial.printf("FATAL: %d byte Activity exceeds %d byte Arena!\n", sizeof(T), sizeof(activityArena));
    return;
  }

  if (currentActivity) {
    currentActivity->onExit();
    currentActivity->~Activity();
    currentActivity = nullptr;
  }

  memset(activityArena, 0, sizeof(activityArena));
  currentActivity = new (activityArena) T(std::forward<Args>(args)...);

  if (currentActivity) {
    currentActivity->onEnter();
  }
}

/**
 * @brief Navigates to the reader activity for a specific book.
 *
 * @param path File path to the book to read
 */
void onGoToReader(const std::string& path) {
  switchTo<ReaderActivity>(render, input, path, [](const std::string&) { onGoToRecent(); });
}

/**
 * @brief Opens the reader activity and returns to the library when closed.
 *
 * @param path File path to the book to read
 */
void openReaderFromCallback(const std::string& path) {
  switchTo<ReaderActivity>(render, input, path, [path](const std::string&) {
    std::string folderPath = path.substr(0, path.find_last_of('/'));
    if (folderPath.empty()) folderPath = "/";
    onGoToLibrary(folderPath);
  });
}

/**
 * @brief Callback wrapper for selecting a book to read.
 *
 * @param path File path to the selected book
 */
void onSelectBook(const std::string& path) { onGoToReader(path); }

/**
 * @brief Navigates to the statistics activity.
 */
void onGoToStatistics() { switchTo<StatisticActivity>(render, input, onGoToRecent, onGoToFileTransfer); }

/**
 * @brief Navigates to the recent books activity.
 */
void onGoToRecent() {
  switchTo<RecentActivity>(render, input, []() { onGoToLibrary("/"); }, onGoToStatistics, onSelectBook, onGoToRecent);
}

/**
 * @brief Handles network mode selection and navigates to appropriate activity.
 *
 * @param mode The selected network mode
 */
void onNetworkModeSelected(NetworkMode mode) {
  switch (mode) {
    case NetworkMode::JOIN_NETWORK:
      switchTo<LocalNetworkActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::CONNECT_CALIBRE:
      switchTo<CalibreConnectActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::CREATE_HOTSPOT:
      switchTo<HotspotActivity>(render, input, onGoToFileTransfer);
      break;
  }
}

/**
 * @brief Navigates to the file transfer/sync activity.
 */
void onGoToFileTransfer() {
  switchTo<SyncActivity>(render, input, onNetworkModeSelected, onGoToRecent, onGoToStatistics, onGoToSettings);
}

/**
 * @brief Navigates to the settings activity.
 */
void onGoToSettings() {
  switchTo<SettingsActivity>(render, input, onGoToRecent, []() { onGoToLibrary("/"); }, onGoToFileTransfer);
}

/**
 * @brief Navigates to the library activity, optionally at a specific path.
 *
 * @param path Directory path to open in the library (defaults to root)
 */
void onGoToLibrary(const std::string& path) {
  switchTo<LibraryActivity>(render, input, onGoToRecent, openReaderFromCallback, onGoToRecent, onGoToSettings, path);
}

/**
 * @brief Verifies power button press duration to determine if device should wake or sleep.
 *
 * Checks if the power button press duration meets the configured threshold
 * to prevent accidental wake/sleep events.
 */
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::SLEEP) return;
  const auto start = millis();
  bool abort = false;
  gpio.update();
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);
    gpio.update();
  }
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < SETTINGS.getPowerButtonDuration()) {
      delay(10);
      gpio.update();
    }
    abort = gpio.getHeldTime() < SETTINGS.getPowerButtonDuration();
  } else {
    abort = true;
  }
  if (abort) gpio.startDeepSleep();
}

/**
 * @brief Waits for the power button to be released.
 *
 * Blocks execution until the power button is no longer being pressed.
 */
void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

/**
 * @brief Puts the device into deep sleep mode.
 *
 * Switches to sleep activity, puts display to sleep, and starts deep sleep.
 */
void enterDeepSleep() {
  switchTo<SleepActivity>(render, input);
  display.deepSleep();
  gpio.startDeepSleep();
}

/**
 * @brief Initializes the display and font system.
 */
void setupDisplayAndFonts() {
  display.begin();
  FontManager::initialize(render);
}

/**
 * @brief Arduino setup function - initializes hardware and starts the boot activity.
 *
 * Sets up GPIO, display, fonts, SD card, and determines wake behavior
 * before launching the boot activity.
 */
void setup() {
  t1 = millis();
  gpio.begin();
  setupDisplayAndFonts();

  if (gpio.isUsbConnected()) {
    Serial.begin(115200);
    unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000) delay(10);
  }

  if (!SdMan.begin()) {
    switchTo<FullScreenMessageActivity>(render, input, "SD card error", EpdFontFamily::BOLD);
    return;
  }

  switch (gpio.getWakeupReason()) {
    case HalGPIO::WakeupReason::PowerButton:
      verifyPowerButtonDuration();
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      gpio.startDeepSleep();
      break;
    default:
      break;
  }

  switchTo<BootActivity>(render, input);
  waitForPowerRelease();
}

/**
 * @brief Arduino main loop - processes activity updates and handles sleep timers.
 *
 * Updates input state, manages auto-sleep timer, processes power button
 * for sleep, and executes the current activity's loop.
 */
void loop() {
  const unsigned long loopStartTime = millis();
  static unsigned long lastActivityTime = millis();

  gpio.update();

  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || (currentActivity && currentActivity->preventAutoSleep())) {
    lastActivityTime = millis();
  }

  if (millis() - lastActivityTime >= SETTINGS.getSleepTimeoutMs()) {
    enterDeepSleep();
    return;
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    enterDeepSleep();
    return;
  }

  if (currentActivity) {
    currentActivity->loop();
  }

  if (currentActivity && currentActivity->skipLoopDelay()) {
    yield();
  } else {
    delay(10);
  }
}