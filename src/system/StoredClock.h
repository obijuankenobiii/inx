#pragma once

#include <HalGPIO.h>

namespace StoredClock {

bool save(const HalGPIO::DateTime& dateTime);
bool load(HalGPIO::DateTime& outDateTime);

}  // namespace StoredClock
