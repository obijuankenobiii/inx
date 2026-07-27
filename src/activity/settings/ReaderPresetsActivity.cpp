/**
 * @file ReaderPresetsActivity.cpp
 * @brief Definitions for ReaderPresetsActivity.
 */

#include "ReaderPresetsActivity.h"

#include <Arduino.h>
#include <EpdFontFamily.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

#include "../util/KeyboardEntryActivity.h"
#include "GfxRenderer.h"
#include "ReaderFontSettingsDraw.h"
#include "ReaderPresetEditorActivity.h"
#include "state/ReaderPreset.h"
#include "state/ReaderSetting.h"
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
  return READER_SETTINGS.xtcShortPwrBtn == SystemSetting::XTC_POWER_PAGE_REFRESH ? "Page Refresh" : "Next";
}

const char* xtcAutoTurnLabel() {
  static char buf[12];
  if (READER_SETTINGS.xtcPageAutoTurnSeconds == 0) {
    return "Off";
  }
  snprintf(buf, sizeof(buf), "%u sec", READER_SETTINGS.xtcPageAutoTurnSeconds);
  return buf;
}

const char* xtcRefreshLabel() {
  static char buf[12];
  snprintf(buf, sizeof(buf), "%u page%s", READER_SETTINGS.xtcRefreshFrequency, READER_SETTINGS.xtcRefreshFrequency == 1 ? "" : "s");
  return buf;
}

const char* systemRefreshLabel() {
  static char buf[12];
  const int pages = READER_SETTINGS.getRefreshFrequency();
  snprintf(buf, sizeof(buf), "%u page%s", pages, pages == 1 ? "" : "s");
  return buf;
}

const char* systemAutoTurnLabel() {
  static char buf[12];
  if (READER_SETTINGS.pageAutoTurnSeconds == 0) {
    return "Off";
  }
  snprintf(buf, sizeof(buf), "%u sec", READER_SETTINGS.pageAutoTurnSeconds);
  return buf;
}

// Per-button (Up/Down/Left/Right, short/long) action mapping - the second half of the "System"
// section, alongside Text Anti-Aliasing/Refresh Frequency/Page Auto Turn above. Up/Down are always
// the raw side buttons regardless of device (X4: physically vertical; X3: physically horizontal, but
// the same signals) - only the printed label changes. Left/Right are the separate front row, present
// on both devices.
constexpr int kButtonActionRowCount = 8;

uint8_t ReaderSetting::* const kButtonActionFields[kButtonActionRowCount] = {
    &ReaderSetting::btnUpShortAction,    &ReaderSetting::btnUpLongAction,   &ReaderSetting::btnDownShortAction,
    &ReaderSetting::btnDownLongAction,   &ReaderSetting::btnLeftShortAction, &ReaderSetting::btnLeftLongAction,
    &ReaderSetting::btnRightShortAction, &ReaderSetting::btnRightLongAction};

const char* buttonActionRowLabel(const int idx, const bool x3) {
  switch (idx) {
    case 0:
      return x3 ? "  Side Left (short)" : "  Side Up (short)";
    case 1:
      return x3 ? "  Side Left (long)" : "  Side Up (long)";
    case 2:
      return x3 ? "  Side Right (short)" : "  Side Down (short)";
    case 3:
      return x3 ? "  Side Right (long)" : "  Side Down (long)";
    case 4:
      return "  Front Left (short)";
    case 5:
      return "  Front Left (long)";
    case 6:
      return "  Front Right (short)";
    case 7:
      return "  Front Right (long)";
    default:
      return "";
  }
}

const char* readerButtonActionLabel(const uint8_t action) {
  static const char* const kLabels[] = {"None",
                                        "Page Next",
                                        "Page Previous",
                                        "Open Settings",
                                        "Annotate",
                                        "Dictionary",
                                        "Page Refresh",
                                        "Chapter Skip Next",
                                        "Chapter Skip Previous",
                                        "Bookmark",
                                        "Table of Contents",
                                        "Change Orientation"};
  if (action >= SystemSetting::READER_BUTTON_ACTION_COUNT) {
    return "None";
  }
  return kLabels[action];
}

// Power Button is a single, short-press-only reader setting (physical Power button while reading has
// no reader-configurable long-press - that's reserved at the hardware/system level) - a 9th row in
// "Buttons" alongside the 8 Up/Down/Left/Right short+long rows, not paired with a long-press slot. It
// shares the same READER_BUTTON_ACTION set (and readerButtonActionLabel()) as those 8 rows.
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
  Serial.printf("[%lu] [MEM] Free heap at ReaderPresetsActivity::onEnter(): %u bytes\n", millis(),
               static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
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

// System section: 5 fixed rows - Text Anti-Aliasing, Refresh Frequency, Page Auto Turn, Image Quality,
// Smart Refresh (Images). Pulled out of the per-book/per-preset SettingsDrawer (the "═══ System ═══"
// and "═══ Image ═══" groups) into single global SystemSetting fields instead of per-book overrides.
// Status Bar (Left/Middle/Right) is also a global field now (see statusBarLeft/Middle/Right on
// SystemSetting) but stays UI-editable only from that same SettingsDrawer (opened while reading), not
// duplicated here - so it's not listed as a row in this section. "Buttons" is its own top-level,
// collapsible section (short/long press action for each of Up/Down/Left/Right - see ReaderButtonBindings
// for the dispatch these configure), a sibling of System/XTC, sitting between them: System, Buttons,
// XTC, Presets.
constexpr int kSystemFixedRowCount = 5;

bool ReaderPresetsActivity::isSystemSettingRow(const int row) const {
  return systemExpanded_ && row > systemHeaderRow() && row <= systemHeaderRow() + kSystemFixedRowCount;
}

int ReaderPresetsActivity::buttonsHeaderRow() const {
  return systemHeaderRow() + 1 + (systemExpanded_ ? kSystemFixedRowCount : 0);
}

bool ReaderPresetsActivity::isButtonsHeaderRow(const int row) const { return row == buttonsHeaderRow(); }

bool ReaderPresetsActivity::isButtonActionRow(const int row) const {
  return buttonsExpanded_ && row > buttonsHeaderRow() && row <= buttonsHeaderRow() + kButtonActionRowCount;
}

bool ReaderPresetsActivity::isPowerButtonRow(const int row) const {
  return buttonsExpanded_ && row == buttonsHeaderRow() + kButtonActionRowCount + 1;
}

// Only Text Anti-Aliasing (systemLocalRow == 1) and Smart Refresh (systemLocalRow == 5) are plain
// toggles; every other System/XTC row with more than 2 options opens the generic popup selector instead
// (see openSelectorForRow()) rather than cycling with Left/Right.
void ReaderPresetsActivity::changeSystemSetting(const int row, const int delta) {
  (void)delta;
  const int systemLocalRow = row - systemHeaderRow();
  if (systemLocalRow == 1) {
    READER_SETTINGS.textAntiAliasing = !READER_SETTINGS.textAntiAliasing;
  } else if (systemLocalRow == 5) {
    READER_SETTINGS.readerSmartRefreshOnImages = !READER_SETTINGS.readerSmartRefreshOnImages;
  }
  READER_SETTINGS.saveToFile();
}

int ReaderPresetsActivity::xtcHeaderRow() const {
  return buttonsHeaderRow() + 1 + (buttonsExpanded_ ? kButtonActionRowCount + 1 : 0);
}

int ReaderPresetsActivity::addPresetRow() const { return xtcHeaderRow() + 1 + (xtcExpanded_ ? 4 : 0); }

int ReaderPresetsActivity::presetRowsStart() const { return addPresetRow() + 1; }

int ReaderPresetsActivity::rowCount() const { return presetRowsStart() + READER_PRESETS.count(); }

int ReaderPresetsActivity::presetIndexForRow(int row) const {
  const int start = presetRowsStart();
  return row < start ? -1 : row - start;
}

bool ReaderPresetsActivity::isXtcSettingRow(const int row) const {
  return xtcExpanded_ && row > xtcHeaderRow() && row <= xtcHeaderRow() + 4;
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
  const int headerHeight = mainHeaderHeight();
  const int titleY = headerY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, titleY, "Reader Presets", true, EpdFontFamily::BOLD);

  const char* back = "\xC2\xAB Back";
  const int backW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, back);
  const int backY = headerY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenW - 20 - backW, backY, back, true);
  const int headerDividerY = mainHeaderDividerY();
  const int listTop = headerDividerY;

  const int rows = rowCount();
  const int xtcHeader = xtcHeaderRow();
  for (int i = 0; i < itemsPerPage_ && (i + scrollOffset_) < rows; i++) {
    const int rowIndex = i + scrollOffset_;
    const int itemY = listTop + i * kListItemHeight;
    const bool isSelected = (rowIndex == selectedRow_);
    const int textY = itemY + (kListItemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

    if (rowIndex == systemHeaderRow()) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, "System", isSelected ? 0 : 1);
      const char* tag = systemExpanded_ ? "-" : "+";
      const int tagW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tag);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenW - kRowValueRightInset - tagW, textY, tag,
                           isSelected ? 0 : 1);
      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
      continue;
    }

    if (isSystemSettingRow(rowIndex)) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      const char* label = "  Text Anti-Aliasing";
      const char* value = nullptr;
      bool isToggle = true;
      bool toggleChecked = READER_SETTINGS.textAntiAliasing != 0;
      const int systemLocalRow = rowIndex - systemHeaderRow();
      if (systemLocalRow == 2) {
        label = "  Refresh Frequency";
        value = systemRefreshLabel();
        isToggle = false;
      } else if (systemLocalRow == 3) {
        label = "  Page Auto Turn";
        value = systemAutoTurnLabel();
        isToggle = false;
      } else if (systemLocalRow == 4) {
        label = "  Image Quality";
        value = readerQualityLabel(READER_SETTINGS.readerImageGrayscale);
        isToggle = false;
      } else if (systemLocalRow == 5) {
        label = "  Smart Refresh (Images)";
        toggleChecked = READER_SETTINGS.readerSmartRefreshOnImages != 0;
      }
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, label, isSelected ? 0 : 1);
      if (isToggle) {
        ReaderFontSettingsDraw::drawToggleCheckbox(renderer, screenW - kRowValueRightInset, itemY, kListItemHeight,
                                                   isSelected, toggleChecked);
      } else {
        const int valueW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, value);
        renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenW - kRowValueRightInset - valueW, textY, value,
                             isSelected ? 0 : 1);
      }
      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
      continue;
    }

    if (isButtonsHeaderRow(rowIndex)) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, "Buttons", isSelected ? 0 : 1);
      const char* tag = buttonsExpanded_ ? "-" : "+";
      const int tagW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, tag);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenW - kRowValueRightInset - tagW, textY, tag,
                           isSelected ? 0 : 1);
      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
      continue;
    }

    if (isButtonActionRow(rowIndex)) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      const int idx = rowIndex - buttonsHeaderRow() - 1;  // 0-7
      const char* label = buttonActionRowLabel(idx, renderer.deviceIsX3());
      const char* value = readerButtonActionLabel(READER_SETTINGS.*(kButtonActionFields[idx]));
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, label, isSelected ? 0 : 1);
      const int valueW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, value);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenW - kRowValueRightInset - valueW, textY, value,
                           isSelected ? 0 : 1);
      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
      continue;
    }

    if (isPowerButtonRow(rowIndex)) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      const char* label = "  Power Button (short)";
      const char* value = readerButtonActionLabel(READER_SETTINGS.btnPowerShortAction);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, label, isSelected ? 0 : 1);
      const int valueW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, value);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenW - kRowValueRightInset - valueW, textY, value,
                           isSelected ? 0 : 1);
      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
      continue;
    }

    if (rowIndex == xtcHeader) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, "XTC", isSelected ? 0 : 1);
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
      const char* value = readerQualityLabel(READER_SETTINGS.xtcImageQuality);
      const int xtcLocalRow = rowIndex - xtcHeader;
      if (xtcLocalRow == 2) {
        label = "  Auto Page Turn";
        value = xtcAutoTurnLabel();
      } else if (xtcLocalRow == 3) {
        label = "  Page Until Refresh";
        value = xtcRefreshLabel();
      } else if (xtcLocalRow == 4) {
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

    if (rowIndex == addPresetRow()) {
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

  renderButtonHints(renderer, "\xC2\xAB Back", "Open", "", "");

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  enteredHalfRefresh_ = true;
}

void ReaderPresetsActivity::renderOverlay() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int optionCount = overlayOptionCountFor(overlayPresetIndex_);

  const int boxW = std::min(screenW - 60, 320);
  constexpr int rowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT - 4;
  const int overlayHeaderH = INX_THEME.drawerHeaderHeight() - 4;
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
    if (sel) {
      renderer.rectangle.fill(boxX + 1, rowY, boxW - 2, rowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
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

void ReaderPresetsActivity::openGenericSelector(std::string title, std::vector<std::string> options,
                                                const int currentIndex, std::function<void(int)> onCommit) {
  selectorTitle_ = std::move(title);
  selectorOptions_ = std::move(options);
  selectorOnCommit_ = std::move(onCommit);
  actionSelectorOpen_ = true;
  actionSelectorSel_ =
      selectorOptions_.empty() ? 0 : std::max(0, std::min(currentIndex, static_cast<int>(selectorOptions_.size()) - 1));
  constexpr int visibleRows = 6;
  actionSelectorScroll_ = std::max(0, actionSelectorSel_ - visibleRows / 2);
}

void ReaderPresetsActivity::openSelectorForRow(const int row) {
  if (isButtonActionRow(row)) {
    const int idx = row - buttonsHeaderRow() - 1;  // 0-7
    uint8_t ReaderSetting::* const field = kButtonActionFields[idx];
    std::vector<std::string> options;
    for (int i = 0; i < static_cast<int>(SystemSetting::READER_BUTTON_ACTION_COUNT); ++i) {
      options.emplace_back(readerButtonActionLabel(static_cast<uint8_t>(i)));
    }
    openGenericSelector(buttonActionRowLabel(idx, renderer.deviceIsX3()), std::move(options),
                        static_cast<int>(READER_SETTINGS.*field), [field](const int chosen) {
                          READER_SETTINGS.*field = static_cast<uint8_t>(chosen);
                          READER_SETTINGS.saveToFile();
                        });
    return;
  }

  if (isPowerButtonRow(row)) {
    std::vector<std::string> options;
    for (int i = 0; i < static_cast<int>(SystemSetting::READER_BUTTON_ACTION_COUNT); ++i) {
      options.emplace_back(readerButtonActionLabel(static_cast<uint8_t>(i)));
    }
    openGenericSelector("Power Button (short)", std::move(options), READER_SETTINGS.btnPowerShortAction,
                        [](const int chosen) {
                          READER_SETTINGS.btnPowerShortAction = static_cast<uint8_t>(chosen);
                          READER_SETTINGS.saveToFile();
                        });
    return;
  }

  const int systemLocalRow = isSystemSettingRow(row) ? row - systemHeaderRow() : -1;
  if (systemLocalRow == 2) {
    // refreshFrequency stores the SystemSetting::REFRESH_FREQUENCY enum index (0-4), not the page count
    // itself - see SystemSetting::getRefreshFrequency() for the index->page-count mapping this must match.
    std::vector<std::string> options = {"1 page", "5 pages", "10 pages", "15 pages", "30 pages"};
    const int idx = READER_SETTINGS.refreshFrequency < options.size() ? READER_SETTINGS.refreshFrequency : 3;
    openGenericSelector("Refresh Frequency", std::move(options), idx, [](const int chosen) {
      READER_SETTINGS.refreshFrequency = static_cast<uint8_t>(chosen);
      READER_SETTINGS.saveToFile();
    });
    return;
  }
  if (systemLocalRow == 3) {
    std::vector<std::string> options;
    for (int sec = 0; sec <= 180; sec += 10) {
      options.push_back(sec == 0 ? "Off" : (std::to_string(sec) + " sec"));
    }
    const int idx = READER_SETTINGS.pageAutoTurnSeconds / 10;
    openGenericSelector("Page Auto Turn", std::move(options), idx, [](const int chosen) {
      READER_SETTINGS.pageAutoTurnSeconds = static_cast<uint8_t>(chosen * 10);
      READER_SETTINGS.saveToFile();
    });
    return;
  }
  if (systemLocalRow == 4) {
    openGenericSelector("Image Quality", {"Low", "Medium", "High"}, READER_SETTINGS.readerImageGrayscale,
                        [](const int chosen) {
                          READER_SETTINGS.readerImageGrayscale = static_cast<uint8_t>(chosen);
                          READER_SETTINGS.saveToFile();
                        });
    return;
  }
  const int xtcLocalRow = isXtcSettingRow(row) ? row - xtcHeaderRow() : -1;
  if (xtcLocalRow == 1) {
    openGenericSelector("Quality", {"Low", "Medium", "High"}, READER_SETTINGS.xtcImageQuality, [](const int chosen) {
      READER_SETTINGS.xtcImageQuality = static_cast<uint8_t>(chosen);
      READER_SETTINGS.saveToFile();
    });
    return;
  }
  if (xtcLocalRow == 2) {
    std::vector<std::string> options;
    for (int sec = 0; sec <= 60; sec += 10) {
      options.push_back(sec == 0 ? "Off" : (std::to_string(sec) + " sec"));
    }
    const int idx = READER_SETTINGS.xtcPageAutoTurnSeconds / 10;
    openGenericSelector("Auto Page Turn", std::move(options), idx, [](const int chosen) {
      READER_SETTINGS.xtcPageAutoTurnSeconds = static_cast<uint8_t>(chosen * 10);
      READER_SETTINGS.saveToFile();
    });
    return;
  }
  if (xtcLocalRow == 3) {
    static constexpr uint8_t values[] = {1, 5, 10, 15, 30};
    std::vector<std::string> options = {"1 page", "5 pages", "10 pages", "15 pages", "30 pages"};
    int idx = 4;
    for (int i = 0; i < 5; ++i) {
      if (values[i] == READER_SETTINGS.xtcRefreshFrequency) {
        idx = i;
        break;
      }
    }
    openGenericSelector("Page Until Refresh", std::move(options), idx, [](const int chosen) {
      static constexpr uint8_t v[] = {1, 5, 10, 15, 30};
      READER_SETTINGS.xtcRefreshFrequency = v[chosen];
      READER_SETTINGS.saveToFile();
    });
    return;
  }
  if (xtcLocalRow == 4) {
    const int idx = READER_SETTINGS.xtcShortPwrBtn == SystemSetting::XTC_POWER_PAGE_REFRESH ? 1 : 0;
    openGenericSelector("Power Button", {"Next", "Page Refresh"}, idx, [](const int chosen) {
      READER_SETTINGS.xtcShortPwrBtn = chosen == 1 ? SystemSetting::XTC_POWER_PAGE_REFRESH : SystemSetting::XTC_POWER_NEXT;
      READER_SETTINGS.saveToFile();
    });
    return;
  }
}

void ReaderPresetsActivity::handleActionSelectorInput() {
  constexpr int visibleRows = 6;
  const int optionCount = static_cast<int>(selectorOptions_.size());
  if (optionCount == 0) {
    actionSelectorOpen_ = false;
    render();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    actionSelectorOpen_ = false;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectorOnCommit_) {
      selectorOnCommit_(actionSelectorSel_);
    }
    actionSelectorOpen_ = false;
    render();
    return;
  }
  if (mappedInput.wasPressed(MenuNav::itemPrev())) {
    actionSelectorSel_ = (actionSelectorSel_ - 1 + optionCount) % optionCount;
    if (actionSelectorSel_ < actionSelectorScroll_) actionSelectorScroll_ = actionSelectorSel_;
    if (actionSelectorSel_ >= actionSelectorScroll_ + visibleRows) {
      actionSelectorScroll_ = actionSelectorSel_ - visibleRows + 1;
    }
    renderActionSelectorOverlay();
    return;
  }
  if (mappedInput.wasPressed(MenuNav::itemNext())) {
    actionSelectorSel_ = (actionSelectorSel_ + 1) % optionCount;
    if (actionSelectorSel_ < actionSelectorScroll_) actionSelectorScroll_ = actionSelectorSel_;
    if (actionSelectorSel_ >= actionSelectorScroll_ + visibleRows) {
      actionSelectorScroll_ = actionSelectorSel_ - visibleRows + 1;
    }
    renderActionSelectorOverlay();
    return;
  }
}

void ReaderPresetsActivity::renderActionSelectorOverlay() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int optionCount = static_cast<int>(selectorOptions_.size());
  constexpr int visibleRows = 6;
  const int rows = std::min(visibleRows, optionCount);
  if (rows <= 0) {
    return;
  }

  const int boxW = std::min(screenW - 60, 320);
  constexpr int rowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT - 4;
  const int overlayHeaderH = INX_THEME.drawerHeaderHeight() - 4;
  const int boxH = overlayHeaderH + rows * rowH;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = (screenH - boxH) / 2;

  renderer.rectangle.fill(boxX, boxY, boxW, boxH, false);

  const std::string shownTitle =
      renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, selectorTitle_.c_str(), boxW - 32, EpdFontFamily::BOLD);
  const int titleY = boxY + (overlayHeaderH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, boxX + 16, titleY, shownTitle.c_str(), true,
                       EpdFontFamily::BOLD);

  const int maxScroll = std::max(0, optionCount - rows);
  actionSelectorScroll_ = std::max(0, std::min(actionSelectorScroll_, maxScroll));

  for (int i = 0; i < rows; ++i) {
    const int optionIdx = actionSelectorScroll_ + i;
    if (optionIdx >= optionCount) {
      break;
    }
    const int rowY = boxY + overlayHeaderH + i * rowH;
    const bool sel = (optionIdx == actionSelectorSel_);
    if (sel) {
      renderer.rectangle.fill(boxX + 1, rowY, boxW - 2, rowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    const int textY = rowY + (rowH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, boxX + 20, textY, selectorOptions_[optionIdx].c_str(),
                         sel ? 0 : 1);
    if (i + 1 < rows) {
      renderer.line.render(boxX, rowY + rowH, boxX + boxW, rowY + rowH, !sel, LineRender::Style::Dotted);
    }
  }

  if (optionCount > rows) {
    const int trackX = boxX + boxW - 10;
    const int trackY = boxY + overlayHeaderH;
    const int trackH = rows * rowH;
    const int thumbH = std::max(8, trackH * rows / optionCount);
    const int thumbY = trackY + actionSelectorScroll_ * std::max(1, trackH - thumbH) / maxScroll;
    renderer.rectangle.fill(trackX, trackY, 2, trackH, true);
    renderer.rectangle.fill(trackX - 2, thumbY, 6, thumbH, true);
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
  if (selectedRow_ == systemHeaderRow()) {
    systemExpanded_ = !systemExpanded_;
    clampSelectionToRowCount();
    render();
    return;
  }
  if (isSystemSettingRow(selectedRow_)) {
    const int systemLocalRow = selectedRow_ - systemHeaderRow();
    if (systemLocalRow == 1 || systemLocalRow == 5) {
      changeSystemSetting(selectedRow_, 0);  // Text Anti-Aliasing / Smart Refresh: plain toggle, no popup
      render();
      return;
    }
    openSelectorForRow(selectedRow_);
    renderActionSelectorOverlay();
    return;
  }
  if (isButtonsHeaderRow(selectedRow_)) {
    buttonsExpanded_ = !buttonsExpanded_;
    clampSelectionToRowCount();
    render();
    return;
  }
  if (isButtonActionRow(selectedRow_)) {
    openSelectorForRow(selectedRow_);
    renderActionSelectorOverlay();
    return;
  }
  if (isPowerButtonRow(selectedRow_)) {
    openSelectorForRow(selectedRow_);
    renderActionSelectorOverlay();
    return;
  }
  if (selectedRow_ == xtcHeaderRow()) {
    xtcExpanded_ = !xtcExpanded_;
    clampSelectionToRowCount();
    render();
    return;
  }
  if (isXtcSettingRow(selectedRow_)) {
    openSelectorForRow(selectedRow_);
    renderActionSelectorOverlay();
    return;
  }
  if (selectedRow_ == addPresetRow()) {
    openEditor(-1);  // new preset
    return;
  }
  overlayPresetIndex_ = presetIndexForRow(selectedRow_);
  overlaySel_ = 0;
  overlayOpen_ = true;
  renderOverlay();
}

void ReaderPresetsActivity::clampSelectionToRowCount() {
  const int rows = rowCount();
  if (selectedRow_ >= rows) {
    selectedRow_ = std::max(0, rows - 1);
  }
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

  if (actionSelectorOpen_) {
    handleActionSelectorInput();
  } else if (overlayOpen_) {
    handleOverlayInput();
  } else {
    handleListInput();
  }
}
