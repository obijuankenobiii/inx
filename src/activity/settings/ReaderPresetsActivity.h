#pragma once

/**
 * @file ReaderPresetsActivity.h
 * @brief Reader-settings panel with Pro-style top-level sections. System and Buttons are
 *        flattened into a short root list and open as dedicated detail pages; the Presets tab owns
 *        the named presets and "Add new preset" action.
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
#include "activity/page/navigation/Menu.h"
#include "system/UiLayout.h"
#include "system/UiTheme.h"

class ReaderPresetsActivity final : public ActivityWithSubactivity, public Menu {
 public:
  ReaderPresetsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onGoBack,
                        std::function<void()> tabNavigateRecent = nullptr,
                        std::function<void()> tabNavigateLibrary = nullptr,
                        std::function<void()> tabNavigateSync = nullptr,
                        std::function<void()> tabNavigateStatistics = nullptr,
                        bool embeddedMode = false,
                        bool presetsOnlyMode = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void renderEmbedded();
  bool takeRenderRequest();
  bool isDetailOpen() const { return isSubPageOpen() || isOverlayOpen(); }
  bool isSubPageOpen() const {
    return embedded_ && (detailSection_ != DetailSection::None || subActivity != nullptr);
  }
  bool isOverlayOpen() const { return embedded_ && (overlayOpen_ || actionSelectorOpen_); }
  bool isFirstItemSelected() const {
    return embedded_ && detailSection_ == DetailSection::None && !overlayOpen_ && !actionSelectorOpen_ &&
           !subActivity && selectedRow_ == 0;
  }
  void clearItemSelection() {
    if (embedded_ && detailSection_ == DetailSection::None && !overlayOpen_ && !actionSelectorOpen_ && !subActivity) {
      selectedRow_ = -1;
    }
  }

 private:
  enum class DetailSection { None, System, Buttons };

  void navigateToSelectedMenu() override;

  void render();
  void renderOverlay();
  int rowCount() const;  ///< System section + Buttons section + Add-new + preset count
  int systemHeaderRow() const { return 0; }  ///< System is the very first row.
  bool isSystemSettingRow(int row) const;  ///< Flat system rows in embedded mode; legacy rows when expanded.
  bool isFontManagerRow(int row) const;    ///< Native Pro-style font download manager entry.
  int buttonsHeaderRow() const;  ///< "Buttons" top-level header, alongside System
  bool isButtonsHeaderRow(int row) const;
  bool isButtonActionRow(int row) const;  ///< True for the 8 many-option rows that open the action selector
  bool isPowerButtonRow(int row) const;  ///< True for the Power Button row - short-press only, no long-press pair
  void changeSystemSetting(int row, int delta);
  int addPresetRow() const;  ///< "+ Add new preset" row, immediately before the preset list
  int presetRowsStart() const;
  int presetIndexForRow(int row) const;  ///< store index for a preset row, or -1 for the Add-new row
  void activateSelectedRow();
  void openEditor(int presetIndex);
  void openRenameKeyboard(int presetIndex);
  void openQuickActionsScreen();  ///< Buttons > Quick Actions - checklist used by the in-reader popup
  void handleOverlayInput();
  void handleListInput();
  void handleDetailInput();
  void finishSubActivity();
  void clampSelectionToRowCount();
  void openDetail(DetailSection section);
  void closeDetail();
  void renderDetail();
  int detailRowCount() const;
  int detailGlobalRow(int row) const;

  // Generic popup selector - every multi-option System row (everything except the plain
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
  bool embedded_ = false;
  bool presetsOnly_ = false;
  bool updateRequired_ = false;

  static constexpr int kListItemHeight = UiTheme::DRAWER_LIST_ITEM_HEIGHT;
  // In bottom-tabs mode, the tab bar sits at the screen bottom where the classic button-hints row normally
  // goes, so that row is redrawn just above the tab bar instead (see render()). This reserves that space.
  static constexpr int kBottomButtonHintsHeight = 50;

  int selectedRow_ = -1;
  int scrollOffset_ = 0;
  int itemsPerPage_ = 1;
  bool systemExpanded_ = false;
  bool buttonsExpanded_ = false;  ///< Legacy expandable state; embedded mode opens a Buttons detail page.
  int detailSelectedRow_ = -1;
  int detailScrollOffset_ = 0;
  DetailSection detailSection_ = DetailSection::None;

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
