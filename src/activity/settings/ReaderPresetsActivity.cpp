/**
 * @file ReaderPresetsActivity.cpp
 * @brief Definitions for ReaderPresetsActivity.
 */

#include "ReaderPresetsActivity.h"

#include <Arduino.h>

#include <algorithm>
#include <cstring>

#include "../util/KeyboardEntryActivity.h"
#include "GfxRenderer.h"
#include "ReaderPresetEditorActivity.h"
#include "state/ReaderPreset.h"
#include "state/SystemSetting.h"
#include "system/MenuNav.h"
#include "system/UiTheme.h"

namespace {
constexpr int kRowValueRightInset = 30;

const char* overlayOptionFor(const int presetIndex, const int optionIndex) {
  if (presetIndex == 0) {
    static constexpr const char* kDefaultOptions[] = {"Edit", "Cancel"};
    return (optionIndex >= 0 && optionIndex < 2) ? kDefaultOptions[optionIndex] : "";
  }
  static constexpr const char* kPresetOptions[] = {"Edit", "Rename", "Delete", "Cancel"};
  return (optionIndex >= 0 && optionIndex < 4) ? kPresetOptions[optionIndex] : "";
}

int overlayOptionCountFor(const int presetIndex) { return presetIndex == 0 ? 2 : 4; }

const char* readerQualityLabel(const uint8_t quality) {
  switch (quality) {
    case SystemSetting::READER_IMAGE_MEDIUM:
      return "Medium";
    case SystemSetting::READER_IMAGE_HIGH:
      return "High";
    default:
      return "Low";
  }
}

const char* xtcPowerLabel() {
  return SETTINGS.xtcShortPwrBtn == SystemSetting::XTC_POWER_PAGE_REFRESH ? "Page Refresh" : "Next";
}

const char* xtcAutoTurnLabel() {
  static char buf[12];
  if (SETTINGS.xtcPageAutoTurnSeconds == 0) {
    return "Off";
  }
  snprintf(buf, sizeof(buf), "%u sec", SETTINGS.xtcPageAutoTurnSeconds);
  return buf;
}

const char* xtcRefreshLabel() {
  static char buf[12];
  snprintf(buf, sizeof(buf), "%u page%s", SETTINGS.xtcRefreshFrequency, SETTINGS.xtcRefreshFrequency == 1 ? "" : "s");
  return buf;
}
}  // namespace

ReaderPresetsActivity::ReaderPresetsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const std::function<void()>& onGoBack,
                                             std::function<void()> tabNavigateRecent,
                                             std::function<void()> tabNavigateLibrary,
                                             std::function<void()> tabNavigateSync,
                                             std::function<void()> tabNavigateStatistics)
    : ActivityWithSubactivity("ReaderPresets", renderer, mappedInput),
      Menu(),
      onGoBack_(onGoBack),
      onTabRecent_(std::move(tabNavigateRecent)),
      onTabLibrary_(std::move(tabNavigateLibrary)),
      onTabSync_(std::move(tabNavigateSync)),
      onTabStatistics_(std::move(tabNavigateStatistics)) {
  tabSelectorIndex = 2;  // Settings tab
}

void ReaderPresetsActivity::onEnter() {
  READER_PRESETS.load();
  const int screenH = renderer.getScreenHeight();
  const int listTop = mainHeaderDividerY();
  const int contentBottom = INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) : screenH - 60;
  itemsPerPage_ = std::max(1, (contentBottom - listTop) / kListItemHeight);
  selectedRow_ = 0;
  scrollOffset_ = 0;
  enteredHalfRefresh_ = false;
  render();
}

void ReaderPresetsActivity::onExit() { exitActivity(); }

int ReaderPresetsActivity::presetRowsStart() const { return 2 + (xtcExpanded_ ? 4 : 0); }

int ReaderPresetsActivity::rowCount() const { return presetRowsStart() + READER_PRESETS.count(); }

int ReaderPresetsActivity::presetIndexForRow(int row) const {
  const int start = presetRowsStart();
  return row < start ? -1 : row - start;
}

bool ReaderPresetsActivity::isXtcSettingRow(const int row) const { return xtcExpanded_ && row >= 2 && row <= 5; }

void ReaderPresetsActivity::changeXtcSetting(const int row, const int delta) {
  if (row == 2) {
    const int step = delta >= 0 ? 1 : (SystemSetting::READER_IMAGE_QUALITY_COUNT - 1);
    SETTINGS.xtcImageQuality =
        static_cast<uint8_t>((SETTINGS.xtcImageQuality + step) % SystemSetting::READER_IMAGE_QUALITY_COUNT);
  } else if (row == 3) {
    int value = static_cast<int>(SETTINGS.xtcPageAutoTurnSeconds) + delta * 10;
    if (value < 0) value = 60;
    if (value > 60) value = 0;
    SETTINGS.xtcPageAutoTurnSeconds = static_cast<uint8_t>(value);
  } else if (row == 4) {
    static constexpr uint8_t values[] = {1, 5, 10, 15, 30};
    int idx = 3;
    for (int i = 0; i < static_cast<int>(sizeof(values) / sizeof(values[0])); ++i) {
      if (values[i] == SETTINGS.xtcRefreshFrequency) {
        idx = i;
        break;
      }
    }
    const int count = static_cast<int>(sizeof(values) / sizeof(values[0]));
    idx = (idx + (delta >= 0 ? 1 : count - 1)) % count;
    SETTINGS.xtcRefreshFrequency = values[idx];
  } else if (row == 5) {
    SETTINGS.xtcShortPwrBtn = SETTINGS.xtcShortPwrBtn == SystemSetting::XTC_POWER_NEXT
                                  ? SystemSetting::XTC_POWER_PAGE_REFRESH
                                  : SystemSetting::XTC_POWER_NEXT;
  }
  SETTINGS.saveToFile();
}

void ReaderPresetsActivity::navigateToSelectedMenu() {
  if (tabSelectorIndex == 0 && onTabRecent_) {
    onTabRecent_();
  } else if (tabSelectorIndex == 1 && onTabLibrary_) {
    onTabLibrary_();
  } else if (tabSelectorIndex == 3 && onTabSync_) {
    onTabSync_();
  } else if (tabSelectorIndex == 4 && onTabStatistics_) {
    onTabStatistics_();
  }
}

void ReaderPresetsActivity::render() {
  const int screenW = renderer.getScreenWidth();
  renderer.clearScreen(0xFF);

  renderTabBar(renderer);

  const int headerY = mainContentTop();
  const int headerHeight = TAB_BAR_HEIGHT;
  const int titleY = headerY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, titleY, "Reader Presets", true, EpdFontFamily::BOLD);

  const char* back = "\xC2\xAB System";
  const int backW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, back);
  const int backY = headerY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenW - 20 - backW, backY, back, true);
  const int headerDividerY = mainHeaderDividerY();
  const int listTop = headerDividerY;

  const int rows = rowCount();
  for (int i = 0; i < itemsPerPage_ && (i + scrollOffset_) < rows; i++) {
    const int rowIndex = i + scrollOffset_;
    const int itemY = listTop + i * kListItemHeight;
    const bool isSelected = (rowIndex == selectedRow_);
    const int textY = itemY + (kListItemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

    if (rowIndex == 0) {
      if (isSelected) {
        renderer.rectangle.fill(0, itemY, screenW, kListItemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
      } else {
        renderer.rectangle.fill(0, itemY, screenW, kListItemHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
      }
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, "+ Add new preset", !isSelected,
                           EpdFontFamily::REGULAR);

      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
      continue;
    }

    if (rowIndex == 1) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, "XTC", isSelected ? 0 : 1, EpdFontFamily::BOLD);
      const char* tag = xtcExpanded_ ? "-" : "+";
      const int tagW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tag);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenW - kRowValueRightInset - tagW, textY, tag,
                           isSelected ? 0 : 1);
      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
      continue;
    }

    if (isXtcSettingRow(rowIndex)) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      const char* label = "  Quality";
      const char* value = readerQualityLabel(SETTINGS.xtcImageQuality);
      if (rowIndex == 3) {
        label = "  Auto Page Turn";
        value = xtcAutoTurnLabel();
      } else if (rowIndex == 4) {
        label = "  Page Until Refresh";
        value = xtcRefreshLabel();
      } else if (rowIndex == 5) {
        label = "  Power Button";
        value = xtcPowerLabel();
      }
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, label, isSelected ? 0 : 1);
      const int valueW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, value);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenW - kRowValueRightInset - valueW, textY, value,
                           isSelected ? 0 : 1);
      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
      continue;
    }

    renderer.rectangle.fill(
        0, itemY, screenW, kListItemHeight,
        isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
    const int presetIndex = presetIndexForRow(rowIndex);
    const std::string name = READER_PRESETS.nameOf(presetIndex);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, name.c_str(), isSelected ? 0 : 1);
    if (presetIndex == 0) {
      const char* tag = "Default";
      const int tagW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, tag);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, screenW - kRowValueRightInset - tagW, textY, tag,
                           isSelected ? 0 : 1);
    }
    renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                         LineRender::Style::Dotted);
  }
  renderer.line.render(0, headerDividerY, screenW, headerDividerY, true);

  renderButtonHints(renderer, "\xC2\xAB System", "Open", "", "");

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  enteredHalfRefresh_ = true;
}

void ReaderPresetsActivity::renderOverlay() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int optionCount = overlayOptionCountFor(overlayPresetIndex_);

  const int boxW = std::min(screenW - 60, 320);
  constexpr int rowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT - 4;
  constexpr int overlayHeaderH = UiTheme::DRAWER_HEADER_HEIGHT - 4;
  const int boxH = overlayHeaderH + optionCount * rowH;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = (screenH - boxH) / 2;

  renderer.rectangle.fill(boxX, boxY, boxW, boxH, false);

  const std::string title = READER_PRESETS.nameOf(overlayPresetIndex_);
  const int titleY = boxY + (overlayHeaderH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, boxX + 16, titleY, title.c_str(), true, EpdFontFamily::BOLD);

  for (int i = 0; i < optionCount; i++) {
    const int rowY = boxY + overlayHeaderH + i * rowH;
    const bool sel = (i == overlaySel_);
    renderer.rectangle.fill(
        boxX + 1, rowY, boxW - 2, rowH,
        sel ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
    const int textY = rowY + (rowH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, boxX + 20, textY, overlayOptionFor(overlayPresetIndex_, i),
                         sel ? 0 : 1);
    if (i + 1 < optionCount) {
      renderer.line.render(boxX, rowY + rowH, boxX + boxW, rowY + rowH, !sel, LineRender::Style::Dotted);
    }
  }

  renderer.line.render(boxX, boxY + overlayHeaderH, boxX + boxW, boxY + overlayHeaderH, true);

  renderer.rectangle.render(boxX, boxY, boxW, boxH, true);
  renderer.rectangle.render(boxX + 1, boxY + 1, boxW - 2, boxH - 2, true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void ReaderPresetsActivity::openEditor(int presetIndex) {
  enterNewActivity(
      new ReaderPresetEditorActivity(renderer, mappedInput, presetIndex, [this]() { subFinished_ = true; }));
}

void ReaderPresetsActivity::openRenameKeyboard(int presetIndex) {
  const std::string current = READER_PRESETS.nameOf(presetIndex);
  enterNewActivity(new KeyboardEntryActivity(
      renderer, mappedInput, "Rename preset", current, 10, 40, false,
      [this, presetIndex](const std::string& entered) {
        pendingRenameIndex_ = presetIndex;
        pendingRenameName_ = entered;
        subFinished_ = true;
      },
      [this]() { subFinished_ = true; }));
}

void ReaderPresetsActivity::activateSelectedRow() {
  if (selectedRow_ == 0) {
    openEditor(-1);  // new preset
    return;
  }
  if (selectedRow_ == 1) {
    xtcExpanded_ = !xtcExpanded_;
    const int rows = rowCount();
    if (selectedRow_ >= rows) selectedRow_ = std::max(0, rows - 1);
    render();
    return;
  }
  if (isXtcSettingRow(selectedRow_)) {
    changeXtcSetting(selectedRow_, 1);
    render();
    return;
  }
  overlayPresetIndex_ = presetIndexForRow(selectedRow_);
  overlaySel_ = 0;
  overlayOpen_ = true;
  renderOverlay();
}

void ReaderPresetsActivity::handleOverlayInput() {
  const int n = overlayOptionCountFor(overlayPresetIndex_);

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    overlayOpen_ = false;
    render();
    return;
  }
  if (mappedInput.wasPressed(MenuNav::itemPrev())) {
    overlaySel_ = (overlaySel_ - 1 + n) % n;
    renderOverlay();
    return;
  }
  if (mappedInput.wasPressed(MenuNav::itemNext())) {
    overlaySel_ = (overlaySel_ + 1) % n;
    renderOverlay();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    const char* choice = overlayOptionFor(overlayPresetIndex_, overlaySel_);
    const int presetIndex = overlayPresetIndex_;
    overlayOpen_ = false;
    if (strcmp(choice, "Edit") == 0) {
      openEditor(presetIndex);
    } else if (strcmp(choice, "Rename") == 0) {
      openRenameKeyboard(presetIndex);
    } else if (strcmp(choice, "Delete") == 0) {
      READER_PRESETS.remove(presetIndex);
      const int rows = rowCount();
      if (selectedRow_ >= rows) selectedRow_ = std::max(0, rows - 1);
      render();
    } else {  // Cancel
      render();
    }
  }
}

void ReaderPresetsActivity::handleListInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    auto back = onGoBack_;
    if (back) back();  // parent dismisses this activity; touch no members afterward
    return;
  }

  if (mappedInput.wasPressed(MenuNav::itemPrev())) {
    if (selectedRow_ > 0) {
      selectedRow_--;
      if (selectedRow_ < scrollOffset_) scrollOffset_ = selectedRow_;
      render();
    }
    return;
  }
  if (mappedInput.wasPressed(MenuNav::itemNext())) {
    if (selectedRow_ < rowCount() - 1) {
      selectedRow_++;
      if (selectedRow_ >= scrollOffset_ + itemsPerPage_) scrollOffset_ = selectedRow_ - itemsPerPage_ + 1;
      render();
    }
    return;
  }

  if (isXtcSettingRow(selectedRow_) && mappedInput.wasPressed(MenuNav::tabPrev())) {
    changeXtcSetting(selectedRow_, -1);
    render();
    return;
  }
  if (isXtcSettingRow(selectedRow_) && mappedInput.wasPressed(MenuNav::tabNext())) {
    changeXtcSetting(selectedRow_, 1);
    render();
    return;
  }

  if (mappedInput.wasPressed(MenuNav::tabPrev())) {
    tabSelectorIndex = (tabSelectorIndex - 1 + TAB_COUNT) % TAB_COUNT;
    if (tabSelectorIndex == 2) {
      render();
    } else {
      navigateToSelectedMenu();
    }
    return;
  }
  if (mappedInput.wasPressed(MenuNav::tabNext())) {
    tabSelectorIndex = (tabSelectorIndex + 1) % TAB_COUNT;
    if (tabSelectorIndex == 2) {
      render();
    } else {
      navigateToSelectedMenu();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateSelectedRow();
    return;
  }
}

void ReaderPresetsActivity::finishSubActivity() {
  exitActivity();
  if (pendingRenameIndex_ >= 0) {
    READER_PRESETS.rename(pendingRenameIndex_, pendingRenameName_);
    pendingRenameIndex_ = -1;
    pendingRenameName_.clear();
  }
  const int rows = rowCount();
  if (selectedRow_ >= rows) selectedRow_ = std::max(0, rows - 1);
  render();
}

void ReaderPresetsActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    if (subFinished_) {
      subFinished_ = false;
      finishSubActivity();
    }
    return;
  }

  if (overlayOpen_) {
    handleOverlayInput();
  } else {
    handleListInput();
  }
}
