#include "system/StoredClock.h"

#include <SDCardManager.h>
#include <Serialization.h>

namespace {
constexpr char CLOCK_FILE[] = "/.system/clock.bin";
constexpr uint8_t CLOCK_FILE_VERSION = 1;

bool valid(const HalGPIO::DateTime& dt) {
  if (dt.year < 2024 || dt.year > 2099) return false;
  if (dt.month < 1 || dt.month > 12) return false;
  if (dt.day < 1 || dt.day > 31) return false;
  if (dt.hour > 23 || dt.minute > 59 || dt.second > 59) return false;
  if (dt.weekday < 1 || dt.weekday > 7) return false;
  return true;
}
}  // namespace

namespace StoredClock {

bool save(const HalGPIO::DateTime& dateTime) {
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
  return true;
}

bool load(HalGPIO::DateTime& outDateTime) {
  FsFile file;
  if (!SdMan.openFileForRead("CLK", CLOCK_FILE, file)) {
    return false;
  }

  uint8_t version = 0;
  HalGPIO::DateTime dt;
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

}  // namespace StoredClock
