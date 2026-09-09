/**
 * @file Home.cpp
 * @brief Empty Home page shell for the inx-pro UI migration.
 */

#include "Home.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <functional>
#include <string>

#include "components/widget/Carousel.h"
#include "images/Hamburger.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

extern void onGoToLibrary(const std::string& path);
extern void onGoToSettings();
extern void onGoToFileTransfer();

namespace {

constexpr unsigned long kDoubleBackWindowMs = 450;
unsigned long lastBackReleaseMs = 0;

}  // namespace

Home::Home(GfxRenderer& renderer, MappedInputManager& mappedInput) : Page("Home", renderer, mappedInput) {}

void Home::onEnter() {
  Page::onEnter();
  sidebarOpen = false;
  ignoreBackReleaseOnEnter_ = mappedInput.isPressed(MappedInputManager::Button::Back);
}

void Home::loop() {
  if (ignoreBackReleaseOnEnter_) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back)) return;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ignoreBackReleaseOnEnter_ = false;
      return;
    }
    ignoreBackReleaseOnEnter_ = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    const unsigned long now = millis();
    const bool doubleBack = lastBackReleaseMs != 0 && now - lastBackReleaseMs <= kDoubleBackWindowMs;
    lastBackReleaseMs = doubleBack ? 0 : now;
    if (doubleBack) {
      sidebarOpen = true;
    } else {
      sidebarOpen = !sidebarOpen;
    }
    requestRender();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
    return;
  }

  Page::loop();
}

void Home::menu() {
  Page::menu();
  if (sidebarOpen) navigation::Sidebar::render(renderer);
}

void Home::title() const {
  renderer.bitmap.icon(Hamburger, UiLayout::MENU_LEFT_MARGIN, UiLayout::MENU_TOP_PADDING, UiLayout::MENU_ICON_SIZE,
                       UiLayout::MENU_ICON_SIZE);
  renderer.text.render(MONTSERRAT_16_FONT_ID,
                       UiLayout::MENU_LEFT_MARGIN + UiLayout::MENU_ICON_SIZE + 12, UiLayout::MENU_TOP_PADDING, name(), true,
                       EpdFontFamily::BOLD);
}

int Home::top() const { return navigation::Menu::height; }

int Home::bottom() const { return renderer.getScreenHeight() - navigation::Menu::bottomHeight; }

void Home::content() {
  const int contentHeight = std::max(0, bottom() - top());
  if (contentHeight <= 0) {
    return;
  }

  widget::Carousel carousel(renderer);
  // Hardcoded to the first recent book until Home navigation/state is migrated.
  carousel.render(0, 0, top(), renderer.getScreenWidth(), std::min(widget::Carousel::kHeight, contentHeight));
}

void Home::navigateToSelectedMenu() {
  switch (tabSelectorIndex) {
    case 1:
      onGoToLibrary("/");
      break;
    case 2:
      onGoToSettings();
      break;
    case 3:
      onGoToFileTransfer();
      break;
    case 4:
      search();
      break;
    default:
      break;
  }
}
