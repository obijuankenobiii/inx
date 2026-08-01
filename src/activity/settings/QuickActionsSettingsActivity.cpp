#include "QuickActionsSettingsActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "ReaderFontSettingsDraw.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/UiTheme.h"

namespace {
constexpr int kBodyFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
constexpr int kRowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT;
constexpr int kValueColumnRight = 30;

/** Every mappable action except None (nothing to run) and Quick Actions itself (would recurse),
 *  A-Z by label so the checklist doesn't depend on READER_BUTTON_ACTION's enum declaration order. */
std::vector<uint8_t> eligibleActions() {
  std::vector<uint8_t> actions;
  for (int i = 1; i < static_cast<int>(SystemSetting::READER_BUTTON_ACTION_COUNT); ++i) {
    if (i == SystemSetting::BTN_ACTION_QUICK_ACTIONS) {
      continue;
    }
    actions.push_back(static_cast<uint8_t>(i));
  }
  std::sort(actions.begin(), actions.end(), [](const uint8_t a, const uint8_t b) {
    return strcmp(SystemSetting::readerButtonActionLabel(a), SystemSetting::readerButtonActionLabel(b)) < 0;
  });
  return actions;
}
}  // namespace

void QuickActionsSettingsActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  render();
}

void QuickActionsSettingsActivity::toggleSelected() {
  const std::vector<uint8_t> actions = eligibleActions();
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(actions.size())) {
    return;
  }
  const uint32_t bit = 1u << actions[static_cast<size_t>(selectedIndex_)];
  READER_SETTINGS.quickActionsMask ^= bit;
  READER_SETTINGS.saveToFile();
}

void QuickActionsSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onDone_();
    return;
  }

  const std::vector<uint8_t> actions = eligibleActions();
  const int total = static_cast<int>(actions.size());
  if (total == 0) {
    return;
  }

  if (mappedInput.wasPressed(MenuNav::itemNext())) {
    selectedIndex_ = (selectedIndex_ + 1) % total;
    render();
    return;
  }
  if (mappedInput.wasPressed(MenuNav::itemPrev())) {
    selectedIndex_ = (selectedIndex_ + total - 1) % total;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleSelected();
    render();
    return;
  }
}

void QuickActionsSettingsActivity::render() {
  renderer.clearScreen();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int bodyTop = INX_THEME.drawPageHeader(renderer, "Quick Actions");

  const std::vector<uint8_t> actions = eligibleActions();
  const int total = static_cast<int>(actions.size());
  if (total == 0) {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(kBodyFont, centerY, "No actions available.", true, EpdFontFamily::BOLD);
    const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
    renderer.ui.buttonHints(kBodyFont, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
    renderer.displayBuffer();
    return;
  }

  const int listBottom = screenH - 44;
  const int visibleRows = std::max(1, (listBottom - bodyTop) / kRowH);
  if (selectedIndex_ < scrollOffset_) {
    scrollOffset_ = selectedIndex_;
  } else if (selectedIndex_ >= scrollOffset_ + visibleRows) {
    scrollOffset_ = selectedIndex_ - visibleRows + 1;
  }
  const int maxScroll = std::max(0, total - visibleRows);
  scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll));
  const int endIndex = std::min(total, scrollOffset_ + visibleRows);

  for (int i = scrollOffset_; i < endIndex; ++i) {
    const int y = bodyTop + (i - scrollOffset_) * kRowH;
    const bool selected = i == selectedIndex_;
    const uint8_t action = actions[static_cast<size_t>(i)];
    const bool checked = (READER_SETTINGS.quickActionsMask & (1u << action)) != 0;
    if (selected) {
      renderer.rectangle.fill(0, y, screenW, kRowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    const int titleY = y + (kRowH - renderer.text.getLineHeight(kBodyFont)) / 2;
    renderer.text.render(kBodyFont, 20, titleY, SystemSetting::readerButtonActionLabel(action), !selected,
                         EpdFontFamily::REGULAR);
    ReaderFontSettingsDraw::drawToggleCheckbox(renderer, screenW - kValueColumnRight, y, kRowH, selected, checked);
    renderer.line.render(0, y + kRowH - 1, screenW, y + kRowH - 1, true, LineRender::Style::Dotted);
  }

  const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "Toggle", "Up", "Down");
  renderer.ui.buttonHints(kBodyFont, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer();
}
