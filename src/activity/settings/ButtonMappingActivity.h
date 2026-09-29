#pragma once

/**
 * @file ButtonMappingActivity.h
 * @brief Reader button action mapping subpage.
 */

#include <cstdint>
#include <functional>
#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "system/UiLayout.h"

/** Reusable Reader -> Button page. */
class ButtonMappingActivity final : public ActivityWithSubactivity {
 public:
  ButtonMappingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onDone)
      : ActivityWithSubactivity("ButtonMapping", renderer, mappedInput), onDone_(std::move(onDone)) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kQuickActionRow = 0;
  static constexpr int kFrontButtonStartRow = 1;
  static constexpr int kSideButtonStartRow = 5;
  static constexpr int kPowerButtonRow = 9;
  static constexpr int kItemCount = 10;
  static constexpr int kButtonCountPerGroup = 4;
  static constexpr int kSectionHeaderHeight = 24;
  static constexpr int kRowHeight = UiLayout::LIST_ITEM_HEIGHT - 2;
  static constexpr int kQuickActionToGroupGap = 10;
  static constexpr int kGroupGap = 10;

  void render();
  void handleListInput();
  void handleSelectorInput();
  void openSelector(int row);
  void closeSelector();
  void commitSelector();
  void renderSelector();
  uint8_t* actionSlot(int row);
  const uint8_t* actionSlot(int row) const;
  int itemY(int row) const;
  int frontHeaderY() const;
  int sideHeaderY() const;
  int maxScroll() const;
  void keepSelectionVisible();

  std::function<void()> onDone_;
  int selectedRow_ = 0;
  int scrollOffset_ = 0;
  bool selectorOpen_ = false;
  bool subFinished_ = false;
  int selectorRow_ = -1;
  int selectorSelected_ = 0;
  int selectorScroll_ = 0;
  std::vector<uint8_t> selectorActions_;
};
