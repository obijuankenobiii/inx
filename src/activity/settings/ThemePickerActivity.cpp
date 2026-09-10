#include "ThemePickerActivity.h"

#include <GfxRenderer.h>

#include "activity/page/SubPage.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"

namespace {

int modeIndex(const widget::Recent::Mode mode) { return static_cast<int>(mode); }

widget::Recent::Mode modeAt(const int index) {
  const int normalized = (index % 3 + 3) % 3;
  return static_cast<widget::Recent::Mode>(normalized);
}

}  // namespace

void ThemePickerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  selected_ = widget::Recent::modeFromSetting(SETTINGS.recentLibraryMode);
  render();
}

void ThemePickerActivity::render() {
  renderer.clearScreen();
  const int top = SubPage::header(renderer, "Theme");
  recent_.preview(selected_, 0, top, renderer.getScreenWidth(), renderer.getScreenHeight() - top - 50);
  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Prev", "Next");
  renderer.ui.buttonHints(MONTSERRAT_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void ThemePickerActivity::applySelection() {
  switch (selected_) {
    case widget::Recent::Mode::Grid:
      SETTINGS.recentLibraryMode = SystemSetting::RECENT_GRID;
      break;
    case widget::Recent::Mode::List:
      SETTINGS.recentLibraryMode = SystemSetting::RECENT_BOOK_LIST;
      break;
    case widget::Recent::Mode::Flow:
    default:
      SETTINGS.recentLibraryMode = SystemSetting::RECENT_FLOW;
      break;
  }
  SETTINGS.saveToFile();
  if (onBack_) onBack_();
}

void ThemePickerActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onBack_) onBack_();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    applySelection();
    return;
  }

  const bool previous = mappedInput.wasPressed(MenuNav::tabPrev()) || mappedInput.wasPressed(MenuNav::itemPrev());
  const bool next = mappedInput.wasPressed(MenuNav::tabNext()) || mappedInput.wasPressed(MenuNav::itemNext());
  if (previous || next) {
    selected_ = modeAt(modeIndex(selected_) + (next ? 1 : -1));
    render();
  }
}
