#pragma once

/**
 * @file QuickActionsMenuUi.h
 * @brief Popup listing the user-selected "Quick Actions" (see QuickActionsSettingsActivity), opened
 *        by a button mapped to SystemSetting::BTN_ACTION_QUICK_ACTIONS. Confirm runs the highlighted
 *        action via ReaderButtonBindings::dispatch() and closes; Back closes without running anything.
 */

#include <cstdint>
#include <vector>

class EpubActivity;

class QuickActionsMenuUi {
 public:
  bool isActive() const { return mode_; }

  void enter(EpubActivity& act);
  void handleInput(EpubActivity& act);

 private:
  void clampScroll();
  void render(EpubActivity& act);

  bool mode_ = false;
  std::vector<uint8_t> actions_;
  int selected_ = 0;
  int scroll_ = 0;
};
