#pragma once

#include <cstdint>

class SystemMetrics {
 public:
  static void markLoopStart();
  static void markActiveWorkEnd();
  static uint8_t getCpuUsagePercent();
};
