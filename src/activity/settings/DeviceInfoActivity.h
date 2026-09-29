#pragma once

/**
 * @file DeviceInfoActivity.h
 * @brief Device information page for X3/X4 hardware.
 */

#include <functional>
#include <string>
#include <vector>

#include "activity/Activity.h"

class DeviceInfoActivity final : public Activity {
 public:
  explicit DeviceInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::function<void()>& onClose)
      : Activity("DeviceInfo", renderer, mappedInput), onClose(onClose) {}

  void onEnter() override;
  void loop() override;

 private:
  struct InfoRow {
    std::string label;
    std::string value;
  };

  const std::function<void()> onClose;
  std::vector<InfoRow> rows;
  bool updateRequired = false;

  void buildRows();
  void render();
};
