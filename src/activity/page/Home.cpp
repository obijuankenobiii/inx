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

#include "images/Hamburger.h"
#include "state/RecentBooks.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

extern void onGoToLibrary(const std::string& path);
extern void onGoToReader(const std::string& path);
extern void onGoToSettings();
extern void onGoToFileTransfer();

namespace {

constexpr unsigned long kDoubleBackWindowMs = 450;
unsigned long lastBackReleaseMs = 0;

}  // namespace

Home::Home(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Page("Home", renderer, mappedInput), recentWidget(renderer) {}

void Home::onEnter() {
  Page::onEnter();
  sidebarOpen = false;
  tabSelectorIndex = 0;
  recentIndex_ = 0;
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

  // These are the same item controls used by RecentActivity's Flow/Grid/List
  // views in the linked inx source. The bottom menu keeps left/right for tab
  // navigation, while up/down select a recent book and Confirm opens it.
  const auto& books = RECENT_BOOKS.getBooks();
  const int recentCount = std::min(static_cast<int>(books.size()), std::max(1, static_cast<int>(SETTINGS.recentVisibleCount)));
  if (recentCount > 0 && mappedInput.wasPressed(itemNextButton())) {
    recentIndex_ = (recentIndex_ + 1) % recentCount;
    requestRender();
    return;
  }
  if (recentCount > 0 && mappedInput.wasPressed(itemPrevButton())) {
    recentIndex_ = (recentIndex_ + recentCount - 1) % recentCount;
    requestRender();
    return;
  }
  if (recentCount > 0 && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int index = std::max(0, std::min(recentIndex_, recentCount - 1));
    onGoToReader(books[static_cast<size_t>(index)].path);
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

  recentWidget.render(widget::Recent::modeFromSetting(SETTINGS.recentLibraryMode), 0, top(), renderer.getScreenWidth(),
                      contentHeight, recentIndex_);
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
