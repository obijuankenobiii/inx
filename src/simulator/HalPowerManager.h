#pragma once

#ifdef SIMULATOR

class HalPowerManager {
 public:
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;
  void begin() {}
  void setPowerSaving(bool) {}
};

inline HalPowerManager powerManager;

#endif
