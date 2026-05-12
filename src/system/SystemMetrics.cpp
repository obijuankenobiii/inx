#include "SystemMetrics.h"

#include <esp_timer.h>

namespace {
uint64_t currentLoopStartUs = 0;
uint64_t currentLoopActiveEndUs = 0;
uint64_t activeAccumUs = 0;
uint64_t totalAccumUs = 0;
uint8_t cpuUsagePercent = 0;
}  // namespace

void SystemMetrics::markLoopStart() {
  const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());

  if (currentLoopStartUs != 0) {
    const uint64_t totalUs = nowUs - currentLoopStartUs;
    uint64_t activeUs = 0;

    if (currentLoopActiveEndUs > currentLoopStartUs) {
      activeUs = currentLoopActiveEndUs - currentLoopStartUs;
      if (activeUs > totalUs) {
        activeUs = totalUs;
      }
    }

    activeAccumUs += activeUs;
    totalAccumUs += totalUs;

    if (totalAccumUs >= 1000000ULL) {
      cpuUsagePercent = totalAccumUs > 0 ? static_cast<uint8_t>((activeAccumUs * 100ULL) / totalAccumUs) : 0;
      activeAccumUs = 0;
      totalAccumUs = 0;
    }
  }

  currentLoopStartUs = nowUs;
  currentLoopActiveEndUs = nowUs;
}

void SystemMetrics::markActiveWorkEnd() {
  currentLoopActiveEndUs = static_cast<uint64_t>(esp_timer_get_time());
}

uint8_t SystemMetrics::getCpuUsagePercent() {
  return cpuUsagePercent;
}
