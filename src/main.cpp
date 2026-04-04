#include <cstring>
#include <string>

#include <Arduino.h>
#include <SPI.h>

#include <HalDisplay.h>
#include <HalGPIO.h>

#include <GfxRenderer.h>
#include <system/font/all.h>

#include <SDCardManager.h>
#include "system/Battery.h"
#include "system/MappedInputManager.h"
#include "system/Fonts.h"
#include "system/FontManager.h"

#include "state/SystemSetting.h"

#include "activity/page/LibraryActivity.h"
#include "activity/page/RecentActivity.h"
#include "activity/page/SettingsActivity.h"
#include "activity/page/StatisticActivity.h"
#include "activity/page/SyncActivity.h"
#include "activity/reader/ReaderActivity.h"
#include "activity/system/BootActivity.h"
#include "activity/system/SleepActivity.h"
#include "activity/util/FullScreenMessageActivity.h"
#include "activity/network/HotspotActivity.h"
#include "activity/network/LocalNetworkActivity.h"
#include "activity/network/CalibreConnectActivity.h"

HalDisplay display;
HalGPIO gpio;
MappedInputManager input(gpio);
GfxRenderer render(display);

Activity* currentActivity = nullptr;

unsigned long t1 = 0;
unsigned long t2 = 0;

void exitActivity();
void enterNewActivity(Activity* activity);
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
 * @brief Exits the current activity and cleans up resources
 */
void exitActivity() {
    if (currentActivity) {
        currentActivity->onExit();
        delete currentActivity;
        currentActivity = nullptr;
        delay(10);
    }
}

/**
 * @brief Enters a new activity and sets it as the current activity
 * @param activity Pointer to the new activity to enter
 */
void enterNewActivity(Activity* activity) {
    if (!activity) {
        return;
    }
    
    if (currentActivity == activity) {
        return;
    }
    
    exitActivity();
    currentActivity = activity;
    currentActivity->onEnter();
}

/**
 * @brief Verifies power button press duration
 */
void verifyPowerButtonDuration() {
    if (SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::SLEEP) {
        return;
    }

    const auto start = millis();
    bool abort = false;
    const uint16_t calibration = start;
    const uint16_t calibratedPressDuration =
        (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

    gpio.update();
    while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
        delay(10);
        gpio.update();
    }

    t2 = millis();
    if (gpio.isPressed(HalGPIO::BTN_POWER)) {
        do {
            delay(10);
            gpio.update();
        } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < calibratedPressDuration);
        abort = gpio.getHeldTime() < calibratedPressDuration;
    } else {
        abort = true;
    }

    if (abort) {
        gpio.startDeepSleep();
    }
}

/**
 * @brief Waits for the power button to be released
 */
void waitForPowerRelease() {
    gpio.update();
    while (gpio.isPressed(HalGPIO::BTN_POWER)) {
        delay(50);
        gpio.update();
    }
}

/**
 * @brief Enters deep sleep mode
 */
void enterDeepSleep() {
    exitActivity();
    enterNewActivity(new SleepActivity(render, input));
    display.deepSleep();
    gpio.startDeepSleep();
}

/**
 * @brief Navigates to the reader activity
 */
void onGoToReader(const std::string& path) {
    exitActivity();    
    enterNewActivity(new ReaderActivity(
        render, 
        input, 
        path,
        [](const std::string&) { onGoToRecent(); }
    ));
}

/**
 * @brief Callback for opening a reader from library
 */
void openReaderFromCallback(const std::string& path) {
    exitActivity();
    enterNewActivity(new ReaderActivity(
        render, 
        input, 
        path,
        [path](const std::string&) {
            std::string folderPath = path.substr(0, path.find_last_of('/'));
            if (folderPath.empty()) folderPath = "/";
            onGoToLibrary(folderPath);
        }
    ));
}

/**
 * @brief Callback for book selection from library
 */
void onSelectBook(const std::string& path) {
    onGoToReader(path);
}

/**
 * @brief Navigates to the statistics activity
 */
void onGoToStatistics() {
    exitActivity();
    enterNewActivity(new StatisticActivity(render, input, onGoToRecent, onGoToFileTransfer));
}

/**
 * @brief Navigates to the recent books activity
 */
void onGoToRecent() {
    exitActivity();
    enterNewActivity(new RecentActivity(
        render, 
        input, 
        []() { onGoToLibrary("/"); },
        onGoToStatistics, 
        onSelectBook,
        onGoToRecent
    ));
}

/**
 * @brief Handles network mode selection
 */
void onNetworkModeSelected(NetworkMode mode) {
    exitActivity();
    switch (mode) {
        case NetworkMode::JOIN_NETWORK:
            enterNewActivity(new LocalNetworkActivity(render, input, onGoToFileTransfer));
            break;
        case NetworkMode::CONNECT_CALIBRE:
            enterNewActivity(new CalibreConnectActivity(render, input, onGoToFileTransfer));
            break;
        case NetworkMode::CREATE_HOTSPOT:
            enterNewActivity(new HotspotActivity(render, input, onGoToFileTransfer));
            break;

    }
}

/**
 * @brief Navigates to the file transfer/sync activity
 */
void onGoToFileTransfer() {
    exitActivity();
    enterNewActivity(new SyncActivity(
        render, 
        input, 
        onNetworkModeSelected, 
        onGoToRecent, 
        onGoToStatistics, 
        onGoToSettings
    ));
}

/**
 * @brief Navigates to the settings activity
 */
void onGoToSettings() {
    exitActivity();
    enterNewActivity(new SettingsActivity(
        render, 
        input, 
        onGoToRecent, 
        []() { onGoToLibrary("/"); },
        onGoToFileTransfer
    ));
}

/**
 * @brief Navigates to the library activity
 */
void onGoToLibrary(const std::string& path) {
    exitActivity();
    enterNewActivity(new LibraryActivity(
        render, 
        input, 
        onGoToRecent, 
        openReaderFromCallback,
        onGoToRecent, 
        onGoToSettings, 
        path
    ));
}

/**
 * @brief Initializes the display and loads all fonts
 */
void setupDisplayAndFonts() {
    display.begin();
    FontManager::initialize(render);
}

/**
 * @brief Arduino setup function
 */
void setup() {
    t1 = millis();
    gpio.begin();
    setupDisplayAndFonts();

    if (gpio.isUsbConnected()) {
        Serial.begin(115200);
        unsigned long start = millis();
        while (!Serial && (millis() - start) < 3000) {
            delay(10);
        }
    }

    if (!SdMan.begin()) {
        exitActivity();
        enterNewActivity(new FullScreenMessageActivity(
            render, 
            input, 
            "SD card error", 
            EpdFontFamily::BOLD
        ));
        return;
    }

    switch (gpio.getWakeupReason()) {
        case HalGPIO::WakeupReason::PowerButton:
            verifyPowerButtonDuration();
            break;
        case HalGPIO::WakeupReason::AfterUSBPower:
            gpio.startDeepSleep();
            break;
        case HalGPIO::WakeupReason::AfterFlash:
        case HalGPIO::WakeupReason::Other:
        default:
            break;
    }

    exitActivity();
    enterNewActivity(new BootActivity(render, input));
    waitForPowerRelease();
}

/**
 * @brief Arduino main loop
 */
void loop() {
    static unsigned long maxLoopDuration = 0;
    const unsigned long loopStartTime = millis();
    static unsigned long lastActivityTime = millis();

    gpio.update();

    if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || 
        (currentActivity && currentActivity->preventAutoSleep())) {
        lastActivityTime = millis();
    }

    const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
    if (millis() - lastActivityTime >= sleepTimeoutMs) {
        enterDeepSleep();
        return;
    }

    if (gpio.isPressed(HalGPIO::BTN_POWER) && 
        gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
        enterDeepSleep();
        return;
    }

    if (currentActivity) {
        currentActivity->loop();
    }

    const unsigned long loopDuration = millis() - loopStartTime;
    if (loopDuration > maxLoopDuration) {
        maxLoopDuration = loopDuration;
    }

    if (currentActivity && currentActivity->skipLoopDelay()) {
        yield();
    } else {
        delay(10);
    }
}