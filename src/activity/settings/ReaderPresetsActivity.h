#pragma once

/**
 * @file ReaderPresetsActivity.h
 * @brief Reader-settings panel: a collapsible "System" section (Text Anti-Aliasing, Refresh
 *        Frequency, Page Auto Turn - pulled out of the per-preset "═══ System ═══" group that used
 *        to live nested inside the preset editor's embedded SettingsDrawer, and made a single global
 *        SystemSetting instead of a per-book override), a collapsible "XTC" section, then a list of
 *        named presets plus an "Add new preset" action.
 *
 * Selecting "Add new" or a preset opens the ReaderPresetEditorActivity (live preview + categorized
 * settings). Confirm on an existing preset opens a small action overlay (Edit / Rename / Delete;
 * Default can only be edited).
 */

#include <functional>
#include <string>
#include <vector>

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
  int rowCount() const;  ///< System section + XTC section + Add-new + preset count
  int systemHeaderRow() const { return 0; }  ///< System is the very first row - order is System, XTC, then presets
  bool isSystemSettingRow(int row) const;  ///< True for the 3 fixed rows only (not the nested Buttons group)
  int buttonsHeaderRow() const;  ///< "Buttons" top-level header, a sibling of System/XTC, between them
  bool isButtonsHeaderRow(int row) const;
  bool isButtonActionRow(int row) const;  ///< True for the 8 many-option rows that open the action selector
  bool isPowerButtonRow(int row) const;  ///< True for the Power Button row - short-press only, no long-press pair
  void changeSystemSetting(int row, int delta);
  int xtcHeaderRow() const;
  int addPresetRow() const;  ///< "+ Add new preset" row, immediately before the preset list
  int presetRowsStart() const;
  int presetIndexForRow(int row) const;  ///< store index for a preset row, or -1 for the Add-new row
  bool isXtcSettingRow(int row) const;
  void activateSelectedRow();
  void openEditor(int presetIndex);
  void openRenameKeyboard(int presetIndex);
  void handleOverlayInput();
  void handleListInput();
  void finishSubActivity();
  void clampSelectionToRowCount();

  // Generic popup selector - every multi-option System/XTC row (everything except the plain
  // Text-Anti-Aliasing toggle) opens this via Confirm instead of cycling with Left/Right, same shape
  // as the preset Edit/Rename/Delete overlay. onCommit is called with the chosen option index.
  void openGenericSelector(std::string title, std::vector<std::string> options, int currentIndex,
                           std::function<void(int)> onCommit);
  void handleActionSelectorInput();
  void renderActionSelectorOverlay();
  void openSelectorForRow(int row);  ///< Builds the right options/onCommit for whichever row this is

  const std::function<void()> onGoBack_;
  const std::function<void()> onTabRecent_;
  const std::function<void()> onTabLibrary_;
  const std::function<void()> onTabSync_;
  const std::function<void()> onTabStatistics_;

  static constexpr int kListItemHeight = UiTheme::DRAWER_LIST_ITEM_HEIGHT;

  int selectedRow_ = 0;
  int scrollOffset_ = 0;
  int itemsPerPage_ = 1;
  bool systemExpanded_ = false;
  bool buttonsExpanded_ = false;  ///< "Buttons" sub-group, nested inside System
  bool xtcExpanded_ = false;

  bool overlayOpen_ = false;
  int overlayPresetIndex_ = -1;
  int overlaySel_ = 0;

  bool actionSelectorOpen_ = false;
  std::string selectorTitle_;
  std::vector<std::string> selectorOptions_;
  std::function<void(int)> selectorOnCommit_;
  int actionSelectorSel_ = 0;
  int actionSelectorScroll_ = 0;

  // Deferred sub-activity teardown (editor / rename keyboard) to avoid reentrant deletion.
  bool subFinished_ = false;
  int pendingRenameIndex_ = -1;
  std::string pendingRenameName_;
  bool enteredHalfRefresh_ = false;
};
