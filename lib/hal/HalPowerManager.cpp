#include "HalPowerManager.h"

#include <WiFi.h>

HalPowerManager powerManager;

void HalPowerManager::begin() {
  normalFreq = static_cast<int>(getCpuFrequencyMhz());
  if (normalFreq <= 0) {
    normalFreq = 160;
  }
}

void HalPowerManager::setPowerSaving(bool enabled) {
  if (normalFreq <= 0) {
    return;
  }

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    enabled = false;
  }

  if (enabled && !isLowPower) {
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      return;
    }
    isLowPower = true;
  } else if (!enabled && isLowPower) {
    if (!setCpuFrequencyMhz(normalFreq)) {
      return;
    }
    isLowPower = false;
  }
}
