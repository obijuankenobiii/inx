/**
 * @file ButtonMappingActivity.cpp
 * @brief Reader button action mapping subpage.
 */

#include "ButtonMappingActivity.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <string>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/PopUp.h"
#include "activity/settings/QuickActionsSettingsActivity.h"
#include "images/Download.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"

namespace {

const char* buttonLabel(const int row, const bool x3) {
  switch (row) {
    case 0:
      return "Left";
    case 1:
      return "Left (Long)";
    case 2:
      return "Right";
    case 3:
      return "Right (Long)";
    case 4:
      return x3 ? "Left" : "Up";
    case 5:
      return x3 ? "Left (Long)" : "Up (Long)";
    case 6:
      return x3 ? "Right" : "Down";
    case 7:
      return x3 ? "Right (Long)" : "Down (Long)";
    case 8:
      return "Power Button";
    default:
      return "";
  }
}

const char* itemLabel(const int row, const bool x3) {
  if (row == 0) return "Set up actions menu";
  if (row == 9) return buttonLabel(8, x3);
  if (row >= 1 && row < 5) {
    return buttonLabel(row - 1, x3);
  }
  return buttonLabel(row - 5 + 4, x3);
}

std::vector<uint8_t> mappingActions() {
  std::vector<uint8_t> actions;
  actions.reserve(SystemSetting::READER_BUTTON_ACTION_COUNT);
  for (int action = 0; action < static_cast<int>(SystemSetting::READER_BUTTON_ACTION_COUNT); ++action) {
    actions.push_back(static_cast<uint8_t>(action));
  }
  return actions;
}

std::vector<std::string> actionLabels(const std::vector<uint8_t>& actions) {
  std::vector<std::string> labels;
  labels.reserve(actions.size());
  for (const uint8_t action : actions) {
    labels.emplace_back(SystemSetting::readerButtonActionLabel(action));
  }
  return labels;
}

}  // namespace

void ButtonMappingActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  selectedRow_ = kQuickActionRow;
  scrollOffset_ = 0;
  selectorOpen_ = false;
  subFinished_ = false;
  render();
}

int ButtonMappingActivity::frontHeaderY() const {
  return UiLayout::PAGE_HEADER_HEIGHT + kRowHeight + kQuickActionToGroupGap;
}

int ButtonMappingActivity::sideHeaderY() const {
  return frontHeaderY() + kSectionHeaderHeight + kButtonCountPerGroup * kRowHeight + kGroupGap;
}

int ButtonMappingActivity::itemY(const int row) const {
  const int frontRowsTop = frontHeaderY() + kSectionHeaderHeight;
  const int sideRowsTop = sideHeaderY() + kSectionHeaderHeight;
  if (row == kQuickActionRow) return UiLayout::PAGE_HEADER_HEIGHT;
  if (row >= kFrontButtonStartRow && row < kSideButtonStartRow) {
    return frontRowsTop + (row - kFrontButtonStartRow) * kRowHeight;
  }
  if (row >= kSideButtonStartRow && row < kPowerButtonRow) {
    return sideRowsTop + (row - kSideButtonStartRow) * kRowHeight;
  }
  return sideRowsTop + kButtonCountPerGroup * kRowHeight;
}

int ButtonMappingActivity::maxScroll() const {
  const int contentBottom = itemY(kPowerButtonRow) + kRowHeight;
  return std::max(0, contentBottom - (renderer.getScreenHeight() - 10));
}

void ButtonMappingActivity::keepSelectionVisible() {
  const int top = UiLayout::PAGE_HEADER_HEIGHT;
  const int bottom = renderer.getScreenHeight() - 10;
  const int selectedTop = itemY(selectedRow_);
  if (selectedTop - scrollOffset_ < top) {
    scrollOffset_ = selectedTop - top;
  } else if (selectedTop + kRowHeight - scrollOffset_ > bottom) {
    scrollOffset_ = selectedTop + kRowHeight - bottom;
  }
  scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll()));
}

uint8_t* ButtonMappingActivity::actionSlot(const int row) {
  switch (row) {
    case kFrontButtonStartRow:
      return &READER_SETTINGS.btnLeftShortAction;
    case kFrontButtonStartRow + 1:
      return &READER_SETTINGS.btnLeftLongAction;
    case kFrontButtonStartRow + 2:
      return &READER_SETTINGS.btnRightShortAction;
    case kFrontButtonStartRow + 3:
      return &READER_SETTINGS.btnRightLongAction;
    case kSideButtonStartRow:
      return &READER_SETTINGS.btnUpShortAction;
    case kSideButtonStartRow + 1:
      return &READER_SETTINGS.btnUpLongAction;
    case kSideButtonStartRow + 2:
      return &READER_SETTINGS.btnDownShortAction;
    case kSideButtonStartRow + 3:
      return &READER_SETTINGS.btnDownLongAction;
    case kPowerButtonRow:
      return &READER_SETTINGS.btnPowerShortAction;
    default:
      return nullptr;
  }
}

const uint8_t* ButtonMappingActivity::actionSlot(const int row) const {
  return const_cast<ButtonMappingActivity*>(this)->actionSlot(row);
}

void ButtonMappingActivity::openSelector(const int row) {
  uint8_t* slot = actionSlot(row);
  if (!slot) return;

  selectorActions_ = mappingActions();
  selectorRow_ = row;
  selectorSelected_ = 0;
  for (int i = 0; i < static_cast<int>(selectorActions_.size()); ++i) {
    if (selectorActions_[static_cast<size_t>(i)] == *slot) {
      selectorSelected_ = i;
      break;
    }
  }
  const int visibleRows = std::max(1, PopUp::bounds(renderer, static_cast<int>(selectorActions_.size())).rows);
  selectorScroll_ = std::max(0, selectorSelected_ - visibleRows / 2);
  selectorOpen_ = true;
  render();
}

void ButtonMappingActivity::closeSelector() {
  selectorOpen_ = false;
  selectorRow_ = -1;
  selectorSelected_ = 0;
  selectorScroll_ = 0;
  selectorActions_.clear();
  render();
}

void ButtonMappingActivity::commitSelector() {
  if (selectorRow_ < 0 || selectorRow_ >= kItemCount || selectorSelected_ < 0 ||
      selectorSelected_ >= static_cast<int>(selectorActions_.size())) {
    closeSelector();
    return;
  }
  if (uint8_t* slot = actionSlot(selectorRow_)) {
    *slot = selectorActions_[static_cast<size_t>(selectorSelected_)];
    READER_SETTINGS.saveToFile();
  }
  closeSelector();
}

void ButtonMappingActivity::handleSelectorInput() {
  const int count = static_cast<int>(selectorActions_.size());
  if (count == 0) {
    closeSelector();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    closeSelector();
    return;
  }

  if (mappedInput.wasPressed(MenuNav::itemPrev())) {
    selectorSelected_ = (selectorSelected_ + count - 1) % count;
  } else if (mappedInput.wasPressed(MenuNav::itemNext())) {
    selectorSelected_ = (selectorSelected_ + 1) % count;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    commitSelector();
    return;
  } else {
    return;
  }

  const int visibleRows = std::max(1, PopUp::bounds(renderer, count).rows);
  if (selectorSelected_ < selectorScroll_) selectorScroll_ = selectorSelected_;
  if (selectorSelected_ >= selectorScroll_ + visibleRows) {
    selectorScroll_ = selectorSelected_ - visibleRows + 1;
  }
  render();
}

void ButtonMappingActivity::handleListInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onDone_) onDone_();
    return;
  }

  if (mappedInput.wasPressed(MenuNav::itemPrev())) {
    selectedRow_ = (selectedRow_ + kItemCount - 1) % kItemCount;
    keepSelectionVisible();
    render();
    return;
  }
  if (mappedInput.wasPressed(MenuNav::itemNext())) {
    selectedRow_ = (selectedRow_ + 1) % kItemCount;
    keepSelectionVisible();
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedRow_ == kQuickActionRow) {
      enterNewActivity(new QuickActionsSettingsActivity(renderer, mappedInput, [this]() { subFinished_ = true; }));
    } else {
      openSelector(selectedRow_);
    }
  }
}

void ButtonMappingActivity::renderSelector() {
  const std::vector<std::string> labels = actionLabels(selectorActions_);
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(labels.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, itemLabel(selectorRow_, renderer.deviceIsX3()));
  PopUp::list(renderer, box, labels, selectorSelected_, selectorScroll_);
  PopUp::border(renderer, box);
}

void ButtonMappingActivity::render() {
  renderer.clearScreen();
  const int bodyTop = SubPage::header(renderer, "Button");
  const int font = systemFontId();
  const int screenW = renderer.getScreenWidth();
  const int valueRight = screenW - 30;
  const int valueMaxW = screenW / 2 - 30;
  keepSelectionVisible();

  const int frontHeader = frontHeaderY() - scrollOffset_;
  const int sideHeader = sideHeaderY() - scrollOffset_;
  if (frontHeader + kSectionHeaderHeight > bodyTop && frontHeader < renderer.getScreenHeight()) {
    renderer.text.render(font, 20, frontHeader + (kSectionHeaderHeight - renderer.text.getLineHeight(font)) / 2,
                         "Front Button", true, EpdFontFamily::BOLD);
  }
  if (sideHeader + kSectionHeaderHeight > bodyTop && sideHeader < renderer.getScreenHeight()) {
    renderer.text.render(font, 20, sideHeader + (kSectionHeaderHeight - renderer.text.getLineHeight(font)) / 2,
                         "Side Button", true, EpdFontFamily::BOLD);
  }

  for (int row = 0; row < kItemCount; ++row) {
    const int y = itemY(row) - scrollOffset_;
    if (y + kRowHeight <= bodyTop || y >= renderer.getScreenHeight()) continue;

    const bool selected = row == selectedRow_;
    const bool black = !selected;
    renderer.rectangle.fill(0, y, screenW, kRowHeight,
                            selected ? static_cast<int>(GfxRenderer::FillTone::Ink)
                                     : static_cast<int>(GfxRenderer::FillTone::Paper));
    const int textY = y + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
    const char* label = itemLabel(row, renderer.deviceIsX3());
    renderer.text.render(font, 20, textY, label, black, EpdFontFamily::REGULAR);

    if (row == kQuickActionRow) {
      constexpr int kIconSize = 40;
      renderer.bitmap.iconScaled(Download, valueRight - kIconSize, y + (kRowHeight - kIconSize) / 2, kIconSize,
                                 kIconSize, kIconSize, kIconSize, BitmapRender::Orientation::Rotate270CW, selected);
    } else if (const uint8_t* slot = actionSlot(row)) {
      const char* value = SystemSetting::readerButtonActionLabel(*slot);
      const std::string shown = renderer.text.truncate(font, value, valueMaxW);
      const int valueW = renderer.text.getWidth(font, shown.c_str());
      renderer.text.render(font, valueRight - valueW, textY, shown.c_str(), black, EpdFontFamily::REGULAR);
    }

    const bool lastInGroup = row == kQuickActionRow || row == kFrontButtonStartRow + kButtonCountPerGroup - 1 ||
                             row == kSideButtonStartRow + kButtonCountPerGroup - 1 || row == kPowerButtonRow;
    if (!lastInGroup) {
      renderer.line.render(0, y + kRowHeight - 1, screenW, y + kRowHeight - 1, black, LineRender::Style::Dotted);
    }
  }

  if (selectorOpen_) renderSelector();
  renderer.displayBuffer();
}

void ButtonMappingActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    if (subFinished_) {
      subFinished_ = false;
      exitActivity();
      render();
    }
    return;
  }
  if (selectorOpen_) {
    handleSelectorInput();
    return;
  }
  handleListInput();
}
