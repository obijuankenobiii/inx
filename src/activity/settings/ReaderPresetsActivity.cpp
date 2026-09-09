/**
 * @file ReaderPresetsActivity.cpp
 * @brief Definitions for ReaderPresetsActivity.
 */

#include "ReaderPresetsActivity.h"

#include <Arduino.h>
#include <EpdFontFamily.h>

#include <algorithm>
#include <cstring>

#include "../util/KeyboardEntryActivity.h"
#include "GfxRenderer.h"
#include "FontManagerActivity.h"
#include "QuickActionsSettingsActivity.h"
#include "ReaderFontSettingsDraw.h"
#include "ReaderPresetEditorActivity.h"
#include "activity/page/components/global/PopUp.h"
#include "activity/page/components/global/Toggle.h"
#include "images/Close.h"
#include "images/Download.h"
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
      return x3 ? "Side Left (short)" : "Side Up (short)";
    case 1:
      return x3 ? "Side Left (long)" : "Side Up (long)";
    case 2:
      return x3 ? "Side Right (short)" : "Side Down (short)";
    case 3:
      return x3 ? "Side Right (long)" : "Side Down (long)";
    case 4:
      return "Front Left (short)";
    case 5:
      return "Front Left (long)";
    case 6:
      return "Front Right (short)";
    case 7:
      return "Front Right (long)";
    default:
      return "";
  }
}

const char* readerButtonActionLabel(const uint8_t action) { return SystemSetting::readerButtonActionLabel(action); }

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
                                             std::function<void()> tabNavigateStatistics,
                                             bool embeddedMode,
                                             bool presetsOnlyMode)
    : ActivityWithSubactivity("ReaderPresets", renderer, mappedInput),
      Menu(),
      onGoBack_(onGoBack),
      onTabRecent_(std::move(tabNavigateRecent)),
      onTabLibrary_(std::move(tabNavigateLibrary)),
      onTabSync_(std::move(tabNavigateSync)),
      onTabStatistics_(std::move(tabNavigateStatistics)),
      embedded_(embeddedMode),
      presetsOnly_(presetsOnlyMode) {
  tabSelectorIndex = 2;  // Settings tab
}

void ReaderPresetsActivity::onEnter() {
  READER_PRESETS.load();
  const int screenH = renderer.getScreenHeight();
  const int listTop = embedded_ ? navigation::Menu::height + 20 + UiLayout::LIST_ITEM_HEIGHT + 10 + 30
                                : mainHeaderDividerY();
  const int contentBottom = embedded_ ? screenH - navigation::Menu::bottomHeight - 10
                                      : (INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) - kBottomButtonHintsHeight
                                                                       : screenH - 60);
  itemsPerPage_ = std::max(1, (contentBottom - listTop) / kListItemHeight);
  selectedRow_ = embedded_ ? -1 : 0;
  detailSelectedRow_ = -1;
  detailScrollOffset_ = 0;
  detailSection_ = DetailSection::None;
  scrollOffset_ = 0;
  enteredHalfRefresh_ = false;
  render();
}

void ReaderPresetsActivity::onExit() { exitActivity(); }

// System section: 6 fixed rows in the legacy activity - Text Anti-Aliasing, Refresh Frequency, Page Auto Turn,
// Image Quality, Smart Refresh (Images), Quick Actions. The embedded activity keeps the first five rows flat;
// Quick Actions lives inside the Buttons detail page. Pulled out of the per-book/per-preset SettingsDrawer (the
// "═══ System ═══" and "═══ Image ═══" groups) into single global SystemSetting fields instead of
// per-book overrides. Status Bar (Left/Middle/Right) is also a global field now (see
// statusBarLeft/Middle/Right on SystemSetting) but stays UI-editable only from that same SettingsDrawer
// (opened while reading), not duplicated here - so it's not listed as a row in this section. "Buttons"
// is its own top-level, collapsible section (short/long press action for each of Up/Down/Left/Right -
// see ReaderButtonBindings for the dispatch these configure), alongside System. Quick Actions opens
// QuickActionsSettingsActivity from the Buttons
// detail page, a checklist of which READER_BUTTON_ACTION values a button mapped to BTN_ACTION_QUICK_ACTIONS pops up.
constexpr int kSystemFixedRowCount = 6;
// Embedded root rows mirror the Pro layout: the Font Manager entry occupies slot 1,
// while the six actual system settings retain their original local row numbers.
// The embedded Reader root contains the five global reader settings, the font manager,
// and the Button & Gestures sub-page. Quick Actions belongs inside that sub-page.
constexpr int kEmbeddedSystemRowCount = 6;

int embeddedSystemLocalRow(const int row) {
  return row == 0 ? 1 : row;
}

void renderOpenNavigationIcon(const GfxRenderer& renderer, const int screenW, const int itemY,
                              const int rowHeight, const bool invert) {
  constexpr int iconSize = 40;
  const int iconX = screenW - kRowValueRightInset - iconSize;
  const int iconY = itemY + (rowHeight - iconSize) / 2;
  renderer.bitmap.iconScaled(Download, iconX, iconY, iconSize, iconSize, iconSize, iconSize,
                             BitmapRender::Orientation::Rotate270CW, invert);
}

bool ReaderPresetsActivity::isSystemSettingRow(const int row) const {
  if (presetsOnly_) return false;
  if (embedded_ && detailSection_ == DetailSection::None) {
    return row >= 0 && row < kEmbeddedSystemRowCount && !isFontManagerRow(row);
  }
  return systemExpanded_ && row > systemHeaderRow() && row <= systemHeaderRow() + kSystemFixedRowCount;
}

bool ReaderPresetsActivity::isFontManagerRow(const int row) const {
  return embedded_ && !presetsOnly_ && detailSection_ == DetailSection::None && row == 1;
}

int ReaderPresetsActivity::buttonsHeaderRow() const {
  if (embedded_ && !presetsOnly_ && detailSection_ == DetailSection::None) {
    return kEmbeddedSystemRowCount;
  }
  return systemHeaderRow() + 1 + (systemExpanded_ ? kSystemFixedRowCount : 0);
}

bool ReaderPresetsActivity::isButtonsHeaderRow(const int row) const { return !presetsOnly_ && row == buttonsHeaderRow(); }

bool ReaderPresetsActivity::isButtonActionRow(const int row) const {
  return !presetsOnly_ && buttonsExpanded_ && row > buttonsHeaderRow() && row <= buttonsHeaderRow() + kButtonActionRowCount;
}

bool ReaderPresetsActivity::isPowerButtonRow(const int row) const {
  return !presetsOnly_ && buttonsExpanded_ && row == buttonsHeaderRow() + kButtonActionRowCount + 1;
}

// Only Text Anti-Aliasing (systemLocalRow == 1) and Smart Refresh (systemLocalRow == 5) are plain
// toggles; every other System row with more than 2 options opens the generic popup selector instead
// (see openSelectorForRow()) rather than cycling with Left/Right.
void ReaderPresetsActivity::changeSystemSetting(const int row, const int delta) {
  (void)delta;
  const int systemLocalRow = embedded_ ? embeddedSystemLocalRow(row) : row - systemHeaderRow();
  if (systemLocalRow == 1) {
    READER_SETTINGS.textAntiAliasing = !READER_SETTINGS.textAntiAliasing;
  } else if (systemLocalRow == 5) {
    READER_SETTINGS.readerSmartRefreshOnImages = !READER_SETTINGS.readerSmartRefreshOnImages;
  }
  READER_SETTINGS.saveToFile();
}

int ReaderPresetsActivity::addPresetRow() const {
  if (presetsOnly_) return 0;
  return buttonsHeaderRow() + 1 + (buttonsExpanded_ ? kButtonActionRowCount + 1 : 0);
}

int ReaderPresetsActivity::presetRowsStart() const { return presetsOnly_ ? 1 : addPresetRow() + 1; }

int ReaderPresetsActivity::rowCount() const {
  if (embedded_ && !presetsOnly_) {
    return kEmbeddedSystemRowCount + 1;  // Flat System rows, then the Buttons detail page.
  }
  return presetRowsStart() + READER_PRESETS.count();
}

int ReaderPresetsActivity::detailRowCount() const {
  switch (detailSection_) {
    case DetailSection::System:
      return kSystemFixedRowCount;
    case DetailSection::Buttons:
      return kButtonActionRowCount + 2;
    case DetailSection::None:
    default:
      return 0;
  }
}

int ReaderPresetsActivity::detailGlobalRow(const int row) const {
  switch (detailSection_) {
    case DetailSection::System:
      return systemHeaderRow() + row + 1;
    case DetailSection::Buttons:
      return buttonsHeaderRow() + row + 1;
    case DetailSection::None:
    default:
      return -1;
  }
}

void ReaderPresetsActivity::openDetail(const DetailSection section) {
  detailSection_ = section;
  detailSelectedRow_ = -1;
  detailScrollOffset_ = 0;
  systemExpanded_ = section == DetailSection::System;
  buttonsExpanded_ = section == DetailSection::Buttons;
  updateRequired_ = true;
}

void ReaderPresetsActivity::closeDetail() {
  detailSection_ = DetailSection::None;
  detailSelectedRow_ = -1;
  detailScrollOffset_ = 0;
  systemExpanded_ = false;
  buttonsExpanded_ = false;
  updateRequired_ = true;
}

int ReaderPresetsActivity::presetIndexForRow(int row) const {
  const int start = presetRowsStart();
  return row < start ? -1 : row - start;
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
  if (embedded_) {
    updateRequired_ = true;
  }
  const int screenW = renderer.getScreenWidth();
  if (!embedded_) {
    renderer.clearScreen(0xFF);
  }

  if (embedded_ && detailSection_ != DetailSection::None) {
    // Detail pages own the whole settings content area. Clear here as well as in the parent Page
    // render so a direct transition can never leave the previous flattened list underneath it.
    renderer.clearScreen(0xFF);
    renderDetail();
    if (overlayOpen_) renderOverlay();
    if (actionSelectorOpen_) renderActionSelectorOverlay();
    return;
  }

  int listTop = navigation::Menu::height + 20 + UiLayout::LIST_ITEM_HEIGHT + 10 + 30;
  if (!embedded_) {
    renderTabBar(renderer);

    const int headerY = mainContentTop();
    const int headerHeight = mainHeaderHeight();
    const int titleY = headerY + (headerHeight - renderer.text.getLineHeight(MONTSERRAT_12_FONT_ID)) / 2;
    renderer.text.render(MONTSERRAT_12_FONT_ID, 20, titleY, "Reader Presets", true, EpdFontFamily::BOLD);

    const char* back = "\xC2\xAB Back";
    const int backW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, back);
    const int backY = headerY + (headerHeight - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
    renderer.text.render(MONTSERRAT_10_FONT_ID, screenW - 20 - backW, backY, back, true);
    listTop = mainHeaderDividerY();
  }
  const int headerDividerY = listTop;

  const int rows = rowCount();
  for (int i = 0; i < itemsPerPage_ && (i + scrollOffset_) < rows; i++) {
    const int rowIndex = i + scrollOffset_;
    const int itemY = listTop + i * kListItemHeight;
    const bool isSelected = (rowIndex == selectedRow_);
    const int textY = itemY + (kListItemHeight - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;

    if (!embedded_ && !presetsOnly_ && rowIndex == systemHeaderRow()) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      renderer.text.render(MONTSERRAT_10_FONT_ID, 20, textY, "System", isSelected ? 0 : 1);
      const char* tag = systemExpanded_ ? "-" : "+";
      const int tagW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, tag);
      renderer.text.render(MONTSERRAT_10_FONT_ID, screenW - kRowValueRightInset - tagW, textY, tag,
                           isSelected ? 0 : 1);
      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
      continue;
    }

    if (isSystemSettingRow(rowIndex)) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      const char* label = "Text Anti-Aliasing";
      const char* value = nullptr;
      bool isToggle = true;
      bool toggleChecked = READER_SETTINGS.textAntiAliasing != 0;
      const int systemLocalRow = embedded_ ? embeddedSystemLocalRow(rowIndex) : rowIndex - systemHeaderRow();
      if (systemLocalRow == 2) {
        label = "Refresh Frequency";
        value = systemRefreshLabel();
        isToggle = false;
      } else if (systemLocalRow == 3) {
        label = "Page Auto Turn";
        value = systemAutoTurnLabel();
        isToggle = false;
      } else if (systemLocalRow == 4) {
        label = "Image Quality";
        value = readerQualityLabel(READER_SETTINGS.readerImageGrayscale);
        isToggle = false;
      } else if (systemLocalRow == 5) {
        label = "Smart Refresh (Images)";
        toggleChecked = READER_SETTINGS.readerSmartRefreshOnImages != 0;
      } else if (systemLocalRow == 6) {
        label = "Quick Actions";
        value = "Configure >";
        isToggle = false;
      }
      renderer.text.render(MONTSERRAT_10_FONT_ID, 20, textY, label, isSelected ? 0 : 1);
      if (isToggle) {
        Toggle::render(renderer, screenW - kRowValueRightInset, itemY, kListItemHeight, toggleChecked, isSelected);
      } else {
        const int valueW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, value);
        renderer.text.render(MONTSERRAT_10_FONT_ID, screenW - kRowValueRightInset - valueW, textY, value,
                             isSelected ? 0 : 1);
      }
      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
      continue;
    }

    if (isFontManagerRow(rowIndex)) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      renderer.text.render(MONTSERRAT_10_FONT_ID, 20, textY, "Font Manager", isSelected ? 0 : 1);
      renderOpenNavigationIcon(renderer, screenW, itemY, kListItemHeight, isSelected);
      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
      continue;
    }

    if (!presetsOnly_ && isButtonsHeaderRow(rowIndex)) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      renderer.text.render(MONTSERRAT_10_FONT_ID, 20, textY, "Button & Action", isSelected ? 0 : 1);
      if (embedded_) {
        renderOpenNavigationIcon(renderer, screenW, itemY, kListItemHeight, isSelected);
      } else {
        const char* tag = buttonsExpanded_ ? "-" : "+";
        const int tagW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, tag);
        renderer.text.render(MONTSERRAT_10_FONT_ID, screenW - kRowValueRightInset - tagW, textY, tag,
                             isSelected ? 0 : 1);
      }
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
      renderer.text.render(MONTSERRAT_10_FONT_ID, 20, textY, label, isSelected ? 0 : 1);
      const int valueW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, value);
      renderer.text.render(MONTSERRAT_10_FONT_ID, screenW - kRowValueRightInset - valueW, textY, value,
                           isSelected ? 0 : 1);
      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
      continue;
    }

    if (isPowerButtonRow(rowIndex)) {
      renderer.rectangle.fill(
          0, itemY, screenW, kListItemHeight,
          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
      const char* label = "Power Button (short)";
      const char* value = readerButtonActionLabel(READER_SETTINGS.btnPowerShortAction);
      renderer.text.render(MONTSERRAT_10_FONT_ID, 20, textY, label, isSelected ? 0 : 1);
      const int valueW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, value);
      renderer.text.render(MONTSERRAT_10_FONT_ID, screenW - kRowValueRightInset - valueW, textY, value,
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
      renderer.text.render(MONTSERRAT_10_FONT_ID, 20, textY, "+ Add new preset", !isSelected,
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
    renderer.text.render(MONTSERRAT_10_FONT_ID, 20, textY, name.c_str(), isSelected ? 0 : 1);
    if (presetIndex == 0) {
      const char* tag = "Default";
      const int tagW = renderer.text.getWidth(MONTSERRAT_8_FONT_ID, tag);
      renderer.text.render(MONTSERRAT_8_FONT_ID, screenW - kRowValueRightInset - tagW, textY, tag,
                           isSelected ? 0 : 1);
    }
    renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, true,
                         LineRender::Style::Dotted);
  }
  if (!embedded_) renderer.line.render(0, headerDividerY, screenW, headerDividerY, true);

  if (!embedded_ && INX_THEME.mainTabsAtBottom()) {
    // Bottom-tabs mode moves the tab bar to the screen bottom, where the classic button-hints row normally
    // goes, so redraw that same row just above the tab bar instead — matches CategorySettingsActivity.
    const int hintsAreaTop = mainContentBottom(renderer) - kBottomButtonHintsHeight;
    const int hintsY = hintsAreaTop + (kBottomButtonHintsHeight - 40) / 2;
    renderer.ui.buttonHints(MONTSERRAT_10_FONT_ID, "\xC2\xAB System", "Open", "", "", hintsY);
  }

  if (!embedded_) {
    renderButtonHints(renderer, "\xC2\xAB Back", "Open", "", "");
  }

  // The parent Settings page owns the final display refresh. Draw popup content last so an
  // embedded selector remains visible over the current settings list.
  if (overlayOpen_) renderOverlay();
  if (actionSelectorOpen_) renderActionSelectorOverlay();

  if (!embedded_) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    enteredHalfRefresh_ = true;
  }
}

void ReaderPresetsActivity::renderDetail() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int listTop = navigation::Menu::height + 20;
  const int visible = std::max(1, (screenH - listTop - 10) / kListItemHeight);
  const int rows = detailRowCount();
  const int maxScroll = std::max(0, rows - visible);
  detailScrollOffset_ = std::max(0, std::min(detailScrollOffset_, maxScroll));

  const char* title = detailSection_ == DetailSection::System ? "System" : "Buttons";
  renderer.text.render(MONTSERRAT_16_FONT_ID, 20, 20, title, true, EpdFontFamily::BOLD);
  renderer.bitmap.icon(Close, screenW - 60, 20, 40, 40);

  for (int i = 0; i < visible && detailScrollOffset_ + i < rows; ++i) {
    const int localRow = detailScrollOffset_ + i;
    const int itemY = listTop + i * kListItemHeight;
    const bool selected = localRow == detailSelectedRow_;
    const int textY = itemY + (kListItemHeight - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
    const char* label = "";
    const char* value = nullptr;
    bool toggle = false;
    bool checked = false;

    if (detailSection_ == DetailSection::System) {
      if (localRow == 0) {
        label = "Text Anti-Aliasing";
        toggle = true;
        checked = READER_SETTINGS.textAntiAliasing != 0;
      } else if (localRow == 1) {
        label = "Refresh Frequency";
        value = systemRefreshLabel();
      } else if (localRow == 2) {
        label = "Page Auto Turn";
        value = systemAutoTurnLabel();
      } else if (localRow == 3) {
        label = "Image Quality";
        value = readerQualityLabel(READER_SETTINGS.readerImageGrayscale);
      } else if (localRow == 4) {
        label = "Smart Refresh (Images)";
        toggle = true;
        checked = READER_SETTINGS.readerSmartRefreshOnImages != 0;
      } else {
        label = "Quick Actions";
        value = "Configure >";
      }
    } else if (detailSection_ == DetailSection::Buttons) {
      if (localRow < kButtonActionRowCount) {
        label = buttonActionRowLabel(localRow, renderer.deviceIsX3());
        value = readerButtonActionLabel(READER_SETTINGS.*(kButtonActionFields[localRow]));
      } else if (localRow == kButtonActionRowCount) {
        label = "Power Button (short)";
        value = readerButtonActionLabel(READER_SETTINGS.btnPowerShortAction);
      } else {
        label = "Quick Actions";
        value = "Configure >";
      }
    }

    renderer.rectangle.fill(0, itemY, screenW, kListItemHeight,
                           selected ? static_cast<int>(GfxRenderer::FillTone::Ink)
                                    : static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.text.render(MONTSERRAT_10_FONT_ID, 20, textY, label, !selected);
    if (toggle) {
      Toggle::render(renderer, screenW - kRowValueRightInset, itemY, kListItemHeight, checked, selected);
    } else if (value) {
      const int valueW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, value);
      renderer.text.render(MONTSERRAT_10_FONT_ID, screenW - kRowValueRightInset - valueW, textY, value, !selected);
    }
    if (i + 1 < visible && detailScrollOffset_ + i + 1 < rows) {
      renderer.line.render(0, itemY + kListItemHeight - 1, screenW, itemY + kListItemHeight - 1, !selected,
                           LineRender::Style::Dotted);
    }
  }
}

void ReaderPresetsActivity::renderOverlay() {
  if (embedded_) {
    updateRequired_ = true;
  }
  const int optionCount = overlayOptionCountFor(overlayPresetIndex_);
  std::vector<std::string> options;
  options.reserve(static_cast<size_t>(optionCount));
  for (int i = 0; i < optionCount; ++i) options.emplace_back(overlayOptionFor(overlayPresetIndex_, i));
  const PopUpBounds box = PopUp::bounds(renderer, optionCount);
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, READER_PRESETS.nameOf(overlayPresetIndex_));
  PopUp::list(renderer, box, options, overlaySel_, 0);
  PopUp::border(renderer, box);

  if (!embedded_) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void ReaderPresetsActivity::renderEmbedded() {
  if (!embedded_ || subActivity) return;
  render();
  updateRequired_ = false;
}

bool ReaderPresetsActivity::takeRenderRequest() {
  if (!embedded_ || subActivity || !updateRequired_) return false;
  updateRequired_ = false;
  return true;
}

void ReaderPresetsActivity::openGenericSelector(std::string title, std::vector<std::string> options,
                                                const int currentIndex, std::function<void(int)> onCommit) {
  selectorTitle_ = std::move(title);
  selectorOptions_ = std::move(options);
  selectorOnCommit_ = std::move(onCommit);
  actionSelectorOpen_ = true;
  actionSelectorSel_ =
      selectorOptions_.empty() ? 0 : std::max(0, std::min(currentIndex, static_cast<int>(selectorOptions_.size()) - 1));
  const int visibleRows = std::max(1, PopUp::bounds(renderer, static_cast<int>(selectorOptions_.size())).rows);
  actionSelectorScroll_ = std::max(0, actionSelectorSel_ - visibleRows / 2);
  updateRequired_ = true;
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

  const int systemLocalRow = isSystemSettingRow(row)
                                 ? ((embedded_ && detailSection_ == DetailSection::None) ? embeddedSystemLocalRow(row)
                                                                                         : row - systemHeaderRow())
                                 : -1;
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
}

void ReaderPresetsActivity::handleActionSelectorInput() {
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
  const int visibleRows = PopUp::bounds(renderer, optionCount).rows;
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
  if (embedded_) {
    updateRequired_ = true;
  }
  const int optionCount = static_cast<int>(selectorOptions_.size());
  if (optionCount <= 0) return;
  const PopUpBounds box = PopUp::bounds(renderer, optionCount);
  const int maxScroll = std::max(0, optionCount - box.rows);
  actionSelectorScroll_ = std::max(0, std::min(actionSelectorScroll_, maxScroll));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, selectorTitle_);
  PopUp::list(renderer, box, selectorOptions_, actionSelectorSel_, actionSelectorScroll_);
  PopUp::border(renderer, box);

  if (!embedded_) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void ReaderPresetsActivity::openEditor(int presetIndex) {
  enterNewActivity(
      new ReaderPresetEditorActivity(renderer, mappedInput, presetIndex, [this]() { subFinished_ = true; }));
}

void ReaderPresetsActivity::openQuickActionsScreen() {
  enterNewActivity(new QuickActionsSettingsActivity(renderer, mappedInput, [this]() { subFinished_ = true; }));
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
  if (embedded_ && !presetsOnly_ && detailSection_ == DetailSection::None) {
    if (isFontManagerRow(selectedRow_)) {
      enterNewActivity(new FontManagerActivity(renderer, mappedInput, [this]() { subFinished_ = true; }));
    } else if (isSystemSettingRow(selectedRow_)) {
      const int systemLocalRow = embeddedSystemLocalRow(selectedRow_);
      if (systemLocalRow == 1 || systemLocalRow == 5) {
        changeSystemSetting(selectedRow_, 0);
      } else {
        openSelectorForRow(selectedRow_);
      }
    } else if (selectedRow_ == buttonsHeaderRow()) {
      openDetail(DetailSection::Buttons);
    }
    render();
    return;
  }

  if (!presetsOnly_ && selectedRow_ == systemHeaderRow()) {
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
    if (systemLocalRow == 6) {
      openQuickActionsScreen();
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
    const int rows = rowCount();
    if (rows > 0) {
      selectedRow_ = selectedRow_ < 0 ? rows - 1 : (selectedRow_ - 1 + rows) % rows;
      if (selectedRow_ < scrollOffset_) scrollOffset_ = selectedRow_;
      if (selectedRow_ >= scrollOffset_ + itemsPerPage_) scrollOffset_ = selectedRow_ - itemsPerPage_ + 1;
      scrollOffset_ = std::max(0, std::min(scrollOffset_, std::max(0, rows - itemsPerPage_)));
      render();
    }
    return;
  }
  if (mappedInput.wasPressed(MenuNav::itemNext())) {
    const int rows = rowCount();
    if (rows > 0) {
      selectedRow_ = selectedRow_ < 0 ? 0 : (selectedRow_ + 1) % rows;
      if (selectedRow_ < scrollOffset_) scrollOffset_ = selectedRow_;
      if (selectedRow_ >= scrollOffset_ + itemsPerPage_) scrollOffset_ = selectedRow_ - itemsPerPage_ + 1;
      scrollOffset_ = std::max(0, std::min(scrollOffset_, std::max(0, rows - itemsPerPage_)));
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

void ReaderPresetsActivity::handleDetailInput() {
  const int rows = detailRowCount();
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    closeDetail();
    render();
    return;
  }

  const bool previousPressed = mappedInput.wasPressed(MenuNav::itemPrev());
  const bool nextPressed = mappedInput.wasPressed(MenuNav::itemNext());
  if (previousPressed || nextPressed) {
    if (rows <= 0) return;
    if (detailSelectedRow_ < 0) {
      detailSelectedRow_ = previousPressed ? rows - 1 : 0;
    } else if (previousPressed) {
      detailSelectedRow_ = (detailSelectedRow_ - 1 + rows) % rows;
    } else {
      detailSelectedRow_ = (detailSelectedRow_ + 1) % rows;
    }

    const int visible = std::max(1, (renderer.getScreenHeight() - (navigation::Menu::height + 20) - 10) /
                                      kListItemHeight);
    if (detailSelectedRow_ < detailScrollOffset_) detailScrollOffset_ = detailSelectedRow_;
    if (detailSelectedRow_ >= detailScrollOffset_ + visible) {
      detailScrollOffset_ = detailSelectedRow_ - visible + 1;
    }
    render();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && detailSelectedRow_ >= 0) {
    if (detailSection_ == DetailSection::Buttons && detailSelectedRow_ == kButtonActionRowCount + 1) {
      openQuickActionsScreen();
      return;
    }
    const int globalRow = detailGlobalRow(detailSelectedRow_);
    if (detailSection_ == DetailSection::System && (detailSelectedRow_ == 0 || detailSelectedRow_ == 4)) {
      changeSystemSetting(globalRow, 0);
      render();
    } else if (detailSection_ == DetailSection::System && detailSelectedRow_ == 5) {
      openQuickActionsScreen();
    } else {
      openSelectorForRow(globalRow);
      render();
    }
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
  } else if (embedded_ && detailSection_ != DetailSection::None) {
    handleDetailInput();
  } else {
    handleListInput();
  }
}
