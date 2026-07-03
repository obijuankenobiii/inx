#pragma once

#include <HalGPIO.h>
#include <cstdint>

namespace StoredClock {

#ifdef SIMULATOR
struct DateTime {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint8_t weekday = 0;
};
#else
using DateTime = HalGPIO::DateTime;
#endif

bool save(const DateTime& dateTime);
bool load(DateTime& outDateTime);
bool persistCurrent();
void scheduleSleepAdvance(uint32_t seconds);
bool applyScheduledSleepAdvance();

}  // namespace StoredClock
