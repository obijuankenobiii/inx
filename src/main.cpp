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
 * @brief Switches the current activity using standard heap allocation.
 * * This uses 'new' and 'delete' which allows the ReaderActivity to utilize 
 * the full 360KB of available heap rather than being stuck in a small static buffer.
 */
template <typename T, typename... Args>
void switchTo(Args&&... args) {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }

  currentActivity = new T(std::forward<Args>(args)...);

  if (currentActivity) {
    currentActivity->onEnter();
  } else {
    Serial.println("FATAL: Memory allocation failed for Activity switch!");
  }
}

/**
 * @brief Navigates to the reader activity for a specific book.
 */
void onGoToReader(const std::string& path) {
  switchTo<ReaderActivity>(render, input, path, [](const std::string&) { onGoToRecent(); });
}

/**
 * @brief Opens the reader activity and returns to the library when closed.
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
 */
void onSelectBook(const std::string& path) { onGoToReader(path); }

/**
 * @brief Navigates to the statistics activity.
 */
void onGoToStatistics() { 
  switchTo<StatisticActivity>(render, input, onGoToRecent, onGoToFileTransfer); 
}

/**
 * @brief Navigates to the recent books activity.
 */
void onGoToRecent() {
  switchTo<RecentActivity>(render, input, []() { onGoToLibrary("/"); }, onGoToStatistics, onSelectBook, onGoToRecent);
}

/**
 * @brief Handles network mode selection and navigates to appropriate activity.
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
 * @brief Navigates to the library activity.
 */
void onGoToLibrary(const std::string& path) {
  switchTo<LibraryActivity>(render, input, onGoToRecent, openReaderFromCallback, onGoToRecent, onGoToSettings, path);
}

/**
 * @brief Set up application.
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

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

void enterDeepSleep() {
  switchTo<SleepActivity>(render, input);
  display.deepSleep();
  gpio.startDeepSleep();
}

void setupDisplayAndFonts() {
  display.begin();
  FontManager::initialize(render);
}

/**
 * @brief Set up application.
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
 * @brief All activity loop.
 */
void loop() {
  gpio.update();
  static unsigned long lastActivityTime = millis();

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
    delay(30);
  }
}