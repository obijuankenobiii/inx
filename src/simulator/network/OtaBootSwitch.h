#pragma once

#ifdef SIMULATOR

#include <cstdint>

struct esp_partition_t;

namespace ota_boot {
uint32_t computeSeqCrc(uint32_t value);
bool switchTo(const esp_partition_t* partition);
}  // namespace ota_boot

#else
#error "src/simulator/network/OtaBootSwitch.h is only for simulator builds"
#endif
