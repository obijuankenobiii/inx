#pragma once

#include <functional>
#include <string>

#include "activity/ActivityWithSubactivity.h"

class TimeSyncActivity final : public ActivityWithSubactivity {
 public:
  TimeSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : ActivityWithSubactivity("TimeSync", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;

 private:
  enum class State { CONNECTING, SYNCING, DONE, FAILED };

  State state = State::CONNECTING;
  std::string message = "Connect to WiFi";
  std::function<void()> onBack;

  void render();
  void beginWifiOrSync();
  void onWifiComplete(bool connected);
  void performSync();
  void wifiOff();
};
