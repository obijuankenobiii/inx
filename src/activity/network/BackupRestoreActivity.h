#pragma once

/**
 * @file BackupRestoreActivity.h
 * @brief Device backup and restore window launched from Sync.
 */

#include <functional>

#include "activity/ActivityWithSubactivity.h"

class BackupRestoreActivity final : public ActivityWithSubactivity {
 public:
  explicit BackupRestoreActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 const std::function<void()>& goBack)
      : ActivityWithSubactivity("BackupRestore", renderer, mappedInput), goBack_(goBack) {}

  void onEnter() override;
  void loop() override;

 private:
  enum class State { Menu, Working, Done };
  enum class Action { None, Backup, Restore };

  State state_ = State::Menu;
  Action action_ = Action::None;
  int selectedIndex_ = 0;
  int copiedCount_ = 0;
  int skippedCount_ = 0;
  int failedCount_ = 0;

  const std::function<void()> goBack_;

  void render();
  void renderMenu(int bodyTop);
  void renderWorking();
  void renderDone();
  void startAction(Action action);
  bool createBackup();
  bool restoreBackup();
  void reloadStoresAfterRestore();
};
