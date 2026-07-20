/**
 * @file TimeSyncActivity.cpp
 * @brief WiFi/NTP time synchronization for the X3 RTC clock.
 */

#include "TimeSyncActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <cstdio>
#include <ctime>

#include "activity/network/WifiSelectionActivity.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/WifiPower.h"

namespace {
constexpr int NTP_TIMEOUT_MS = 8000;

uint8_t weekdayFromTm(const tm& t) { return static_cast<uint8_t>(t.tm_wday == 0 ? 7 : t.tm_wday); }
}  // namespace

void TimeSyncActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  beginWifiOrSync();
}

void TimeSyncActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    onBack();
  }
}

void TimeSyncActivity::render() {
  renderer.clearScreen();
  const int h = renderer.getScreenHeight();
  const int titleY = h / 2 - 42;
  const int bodyY = h / 2 - 6;

  const char* title = "Sync time";
  if (state == State::DONE) {
    title = "Time synced";
  } else if (state == State::FAILED) {
    title = "Time sync failed";
  }

  renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, titleY, title, true, EpdFontFamily::BOLD);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, bodyY, message.c_str(), true);

  char tz[16];
  SETTINGS.formatTimeZone(tz, sizeof(tz));
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, bodyY + 32, tz, true);

  if (state == State::DONE || state == State::FAILED) {
    const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Done", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}

void TimeSyncActivity::beginWifiOrSync() {
  state = State::CONNECTING;
  message = "Select a WiFi network";
  render();

  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    performSync();
    return;
  }

  enterNewActivity(
      new WifiSelectionActivity(renderer, mappedInput, [this](const bool connected) { onWifiComplete(connected); }));
}

void TimeSyncActivity::onWifiComplete(const bool connected) {
  exitActivity();

  if (!connected) {
    state = State::FAILED;
    message = "WiFi was not connected";
    render();
    return;
  }

  performSync();
}

void TimeSyncActivity::performSync() {
  state = State::SYNCING;
  message = "Syncing from pool.ntp.org";
  render();

  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.google.com");
  esp_sntp_init();

  const uint32_t start = millis();
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && millis() - start < NTP_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  const time_t utcNow = time(nullptr);
  if (utcNow < 1704067200) {
    state = State::FAILED;
    message = "NTP did not return time";
    wifiOff();
    render();
    return;
  }

  const time_t localEpoch = utcNow + SETTINGS.getTimeZoneOffsetMinutes() * 60;
  tm local{};
  if (gmtime_r(&localEpoch, &local) == nullptr) {
    state = State::FAILED;
    message = "Could not apply timezone";
    wifiOff();
    render();
    return;
  }

  struct SyncedDateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
  } dt;
  dt.year = static_cast<uint16_t>(local.tm_year + 1900);
  dt.month = static_cast<uint8_t>(local.tm_mon + 1);
  dt.day = static_cast<uint8_t>(local.tm_mday);
  dt.hour = static_cast<uint8_t>(local.tm_hour);
  dt.minute = static_cast<uint8_t>(local.tm_min);
  dt.second = static_cast<uint8_t>(local.tm_sec);
  dt.weekday = weekdayFromTm(local);

  char buffer[40];
#ifndef SIMULATOR
  HalGPIO::DateTime rtcDateTime;
  rtcDateTime.year = dt.year;
  rtcDateTime.month = dt.month;
  rtcDateTime.day = dt.day;
  rtcDateTime.hour = dt.hour;
  rtcDateTime.minute = dt.minute;
  rtcDateTime.second = dt.second;
  rtcDateTime.weekday = dt.weekday;
  if (gpio.deviceIsX3() && gpio.writeDateTime(rtcDateTime)) {
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u saved to RTC", dt.hour, dt.minute);
  } else
#endif
  {
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u synced", dt.hour, dt.minute);
  }
  message = buffer;
  state = State::DONE;
  wifiOff();
  render();
}

void TimeSyncActivity::wifiOff() {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  WifiPower::off();
}
