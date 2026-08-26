#pragma once

#include <Arduino.h>

class HalPowerManager {
  int normalFreq = 0;
  bool isLowPower = false;

 public:
  static constexpr int LOW_POWER_FREQ = 10;
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;

  void begin();
  void setPowerSaving(bool enabled);
};

extern HalPowerManager powerManager;
