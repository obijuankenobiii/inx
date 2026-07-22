#pragma once

/**
 * @file ReaderPresetsActivity.h
 * @brief Reader-settings panel: a list of named presets plus an "Add new preset" action.
 *
 * Replaces the old flat reader-settings list. Selecting "Add new" or a preset opens the
 * ReaderPresetEditorActivity (live preview + categorized settings). Confirm on an existing preset
 * opens a small action overlay (Edit / Rename / Delete; Default can only be edited).
 */

#include <functional>
#include <string>

#include "../Menu.h"
#include "activity/ActivityWithSubactivity.h"
#include "system/UiTheme.h"

class ReaderPresetsActivity final : public ActivityWithSubactivity, public Menu {
 public:
  ReaderPresetsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onGoBack,
                        std::function<void()> tabNavigateRecent = nullptr,
                        std::function<void()> tabNavigateLibrary = nullptr,
                        std::function<void()> tabNavigateSync = nullptr,
                        std::function<void()> tabNavigateStatistics = nullptr);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  void navigateToSelectedMenu() override;

  void render();
  void renderOverlay();
  int rowCount() const;  ///< Add-new + XTC section + preset count
  int presetRowsStart() const;
  int presetIndexForRow(int row) const;  ///< store index for a preset row, or -1 for the Add-new row
  bool isXtcSettingRow(int row) const;
  void changeXtcSetting(int row, int delta);
  void activateSelectedRow();
  void openEditor(int presetIndex);
  void openRenameKeyboard(int presetIndex);
  void handleOverlayInput();
  void handleListInput();
  void finishSubActivity();

  const std::function<void()> onGoBack_;
  const std::function<void()> onTabRecent_;
  const std::function<void()> onTabLibrary_;
  const std::function<void()> onTabSync_;
  const std::function<void()> onTabStatistics_;

  static constexpr int kListItemHeight = UiTheme::DRAWER_LIST_ITEM_HEIGHT;

  int selectedRow_ = 0;
  int scrollOffset_ = 0;
  int itemsPerPage_ = 1;
  bool xtcExpanded_ = false;

  bool overlayOpen_ = false;
  int overlayPresetIndex_ = -1;
  int overlaySel_ = 0;

  // Deferred sub-activity teardown (editor / rename keyboard) to avoid reentrant deletion.
  bool subFinished_ = false;
  int pendingRenameIndex_ = -1;
  std::string pendingRenameName_;
  bool enteredHalfRefresh_ = false;
};
