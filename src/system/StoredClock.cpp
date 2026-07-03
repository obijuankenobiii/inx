#include "system/StoredClock.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <ctime>

#include "state/SystemSetting.h"

namespace {
constexpr char CLOCK_FILE[] = "/.system/clock.bin";
constexpr uint8_t CLOCK_FILE_VERSION = 1;

bool runtimeBaseAvailable = false;
StoredClock::DateTime runtimeBase;
uint32_t runtimeBaseMillis = 0;

bool valid(const StoredClock::DateTime& dt) {
  if (dt.year < 2024 || dt.year > 2099) return false;
  if (dt.month < 1 || dt.month > 12) return false;
  if (dt.day < 1 || dt.day > 31) return false;
  if (dt.hour > 23 || dt.minute > 59 || dt.second > 59) return false;
  if (dt.weekday < 1 || dt.weekday > 7) return false;
  return true;
}

bool isLeapYear(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static constexpr uint8_t DAYS[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return month >= 1 && month <= 12 ? DAYS[month] : 31;
}

uint8_t weekdayFromTm(const tm& t) {
  return static_cast<uint8_t>(t.tm_wday == 0 ? 7 : t.tm_wday);
}

void advanceDateTime(StoredClock::DateTime& dt, uint32_t seconds) {
  uint32_t carry = static_cast<uint32_t>(dt.second) + seconds;
  dt.second = static_cast<uint8_t>(carry % 60);
  carry /= 60;

  carry += dt.minute;
  dt.minute = static_cast<uint8_t>(carry % 60);
  carry /= 60;

  carry += dt.hour;
  dt.hour = static_cast<uint8_t>(carry % 24);
  uint32_t days = carry / 24;

  while (days-- > 0) {
    dt.day++;
    if (dt.day > daysInMonth(dt.year, dt.month)) {
      dt.day = 1;
      dt.month++;
      if (dt.month > 12) {
        dt.month = 1;
        dt.year++;
      }
    }
    dt.weekday = static_cast<uint8_t>(dt.weekday >= 7 ? 1 : dt.weekday + 1);
  }
}

bool readStoredClock(StoredClock::DateTime& outDateTime) {
  FsFile file;
  if (!SdMan.openFileForRead("CLK", CLOCK_FILE, file)) {
    return false;
  }

  uint8_t version = 0;
  StoredClock::DateTime dt;
  serialization::readPod(file, version);
  if (version != CLOCK_FILE_VERSION) {
    file.close();
    return false;
  }

  serialization::readPod(file, dt.year);
  serialization::readPod(file, dt.month);
  serialization::readPod(file, dt.day);
  serialization::readPod(file, dt.hour);
  serialization::readPod(file, dt.minute);
  serialization::readPod(file, dt.second);
  serialization::readPod(file, dt.weekday);
  file.close();

  if (!valid(dt)) {
    return false;
  }
  outDateTime = dt;
  return true;
}

bool loadSystemClock(StoredClock::DateTime& outDateTime) {
  const time_t utcNow = time(nullptr);
  if (utcNow < 1704067200) {
    return false;
  }

  const time_t localEpoch = utcNow + SETTINGS.getTimeZoneOffsetMinutes() * 60;
  tm local {};
  if (gmtime_r(&localEpoch, &local) == nullptr) {
    return false;
  }

  StoredClock::DateTime dt;
  dt.year = static_cast<uint16_t>(local.tm_year + 1900);
  dt.month = static_cast<uint8_t>(local.tm_mon + 1);
  dt.day = static_cast<uint8_t>(local.tm_mday);
  dt.hour = static_cast<uint8_t>(local.tm_hour);
  dt.minute = static_cast<uint8_t>(local.tm_min);
  dt.second = static_cast<uint8_t>(local.tm_sec);
  dt.weekday = weekdayFromTm(local);

  if (!valid(dt)) {
    return false;
  }
  outDateTime = dt;
  return true;
}
}  // namespace

namespace StoredClock {

bool save(const DateTime& dateTime) {
  if (!valid(dateTime)) {
    return false;
  }

  SdMan.mkdir("/.system");
  FsFile file;
  if (!SdMan.openFileForWrite("CLK", CLOCK_FILE, file)) {
    return false;
  }

  serialization::writePod(file, CLOCK_FILE_VERSION);
  serialization::writePod(file, dateTime.year);
  serialization::writePod(file, dateTime.month);
  serialization::writePod(file, dateTime.day);
  serialization::writePod(file, dateTime.hour);
  serialization::writePod(file, dateTime.minute);
  serialization::writePod(file, dateTime.second);
  serialization::writePod(file, dateTime.weekday);
  file.close();
  runtimeBase = dateTime;
  runtimeBaseMillis = millis();
  runtimeBaseAvailable = true;
  return true;
}

bool load(DateTime& outDateTime) {
  DateTime dt;
  if (loadSystemClock(dt)) {
    outDateTime = dt;
    return true;
  }

  uint32_t baseMillis = 0;
  if (runtimeBaseAvailable) {
    dt = runtimeBase;
    baseMillis = runtimeBaseMillis;
  } else if (!readStoredClock(dt)) {
    return false;
  }

  advanceDateTime(dt, static_cast<uint32_t>((millis() - baseMillis) / 1000UL));
  if (!valid(dt)) {
    return false;
  }
  outDateTime = dt;
  return true;
}

bool persistCurrent() {
  DateTime dt;
  if (!load(dt)) {
    return false;
  }
  return save(dt);
}

}  // namespace StoredClock
