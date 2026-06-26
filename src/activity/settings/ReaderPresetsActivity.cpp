/**
 * @file ReaderPresetsActivity.cpp
 * @brief Definitions for ReaderPresetsActivity.
 */

#include "ReaderPresetsActivity.h"

#include <Arduino.h>

#include <algorithm>
#include <vector>

#include "GfxRenderer.h"
#include "ReaderPresetEditorActivity.h"
#include "../util/KeyboardEntryActivity.h"
#include "state/ReaderPreset.h"
#include "state/SystemSetting.h"
#include "system/MenuNav.h"

namespace {
std::vector<std::string> overlayOptionsFor(int presetIndex) {
  if (presetIndex == 0) {
    return {"Edit", "Cancel"};
  }
  return {"Edit", "Rename", "Delete", "Cancel"};
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
  itemsPerPage_ = std::max(1, (screenH - kListTop - 60) / kListItemHeight);
  selectedRow_ = 0;
  scrollOffset_ = 0;
  enteredHalfRefresh_ = false;
  render();
}

void ReaderPresetsActivity::onExit() {
  exitActivity();
}

int ReaderPresetsActivity::rowCount() const { return 1 + READER_PRESETS.count(); }

int ReaderPresetsActivity::presetIndexForRow(int row) const { return row == 0 ? -1 : row - 1; }

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

  // Header band matches the System settings screen: title vertically centered in a TAB_BAR_HEIGHT-tall
  // band below the tab bar, with the divider at TAB_BAR_HEIGHT * 2.
  const int headerY = TAB_BAR_HEIGHT;
  const int headerHeight = TAB_BAR_HEIGHT;
  const int titleY = headerY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, titleY, "Reader Presets", true, EpdFontFamily::BOLD);

  const char* back = "\xC2\xAB System";
  const int backW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, back);
  const int backY = headerY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenW - 20 - backW, backY, back, true);
  renderer.line.render(0, kHeaderDividerY, screenW, kHeaderDividerY, true);

  const int rows = rowCount();
  for (int i = 0; i < itemsPerPage_ && (i + scrollOffset_) < rows; i++) {
    const int rowIndex = i + scrollOffset_;
    const int itemY = kListTop + i * kListItemHeight - 6;
    const bool isSelected = (rowIndex == selectedRow_);
    const int textY = itemY + (kListItemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

    if (rowIndex == 0) {
      if (isSelected) {
        renderer.rectangle.fill(0, itemY, screenW, kListItemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
      } else {
        renderer.rectangle.fill(0, itemY, screenW, kListItemHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
      }
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, "+ Add new preset", !isSelected, EpdFontFamily::REGULAR);

      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true);
      continue;
    }

    renderer.rectangle.fill(0, itemY, screenW, kListItemHeight,
                            isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink)
                                       : static_cast<int>(GfxRenderer::FillTone::Paper));
    const int presetIndex = presetIndexForRow(rowIndex);
    const std::string name = READER_PRESETS.nameOf(presetIndex);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, name.c_str(), isSelected ? 0 : 1);
    if (presetIndex == 0) {
      const char* tag = "Default";
      const int tagW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, tag);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, screenW - 24 - tagW, textY, tag, isSelected ? 0 : 1);
    }
    renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true);
  }

  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "\xC2\xAB System", "Open", "", "");

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  enteredHalfRefresh_ = true;
}

void ReaderPresetsActivity::renderOverlay() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const std::vector<std::string> options = overlayOptionsFor(overlayPresetIndex_);

  const int boxW = std::min(screenW - 60, 320);
  const int rowH = 50;
  const int boxH = 50 + static_cast<int>(options.size()) * rowH;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = (screenH - boxH) / 2;

  renderer.rectangle.fill(boxX, boxY, boxW, boxH, false);
  renderer.rectangle.render(boxX, boxY, boxW, boxH, true);

  const std::string title = READER_PRESETS.nameOf(overlayPresetIndex_);
  const int overlayHeaderH = 40;
  const int titleY = boxY + (overlayHeaderH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2 - 1;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, boxX + 16, titleY, title.c_str(), true,
                       EpdFontFamily::BOLD);
  renderer.line.render(boxX, boxY + overlayHeaderH, boxX + boxW, boxY + overlayHeaderH, true);

  for (size_t i = 0; i < options.size(); i++) {
    const int rowY = boxY + 42 + static_cast<int>(i) * rowH;
    const bool sel = (static_cast<int>(i) == overlaySel_);
    renderer.rectangle.fill(boxX + 1, rowY, boxW - 2, rowH,
                            sel ? static_cast<int>(GfxRenderer::FillTone::Ink)
                                : static_cast<int>(GfxRenderer::FillTone::Paper));
    const int textY = rowY + (rowH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, boxX + 20, textY, options[i].c_str(), sel ? 0 : 1);
  }

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
  overlayPresetIndex_ = presetIndexForRow(selectedRow_);
  overlaySel_ = 0;
  overlayOpen_ = true;
  renderOverlay();
}

void ReaderPresetsActivity::handleOverlayInput() {
  const std::vector<std::string> options = overlayOptionsFor(overlayPresetIndex_);
  const int n = static_cast<int>(options.size());

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
    const std::string choice = options[overlaySel_];
    const int presetIndex = overlayPresetIndex_;
    overlayOpen_ = false;
    if (choice == "Edit") {
      openEditor(presetIndex);
    } else if (choice == "Rename") {
      openRenameKeyboard(presetIndex);
    } else if (choice == "Delete") {
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
    return;
  }

  if (overlayOpen_) {
    handleOverlayInput();
  } else {
    handleListInput();
  }
}
