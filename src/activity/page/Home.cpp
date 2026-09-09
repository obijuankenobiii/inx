/**
 * @file Home.cpp
 * @brief Empty Home page shell for the inx-pro UI migration.
 */

#include "Home.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "components/widget/Carousel.h"
#include "images/Hamburger.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

Home::Home(GfxRenderer& renderer, MappedInputManager& mappedInput) : Page("Home", renderer, mappedInput) {}

void Home::onEnter() {
  Page::onEnter();
  sidebarOpen = false;
}

void Home::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    sidebarOpen = !sidebarOpen;
    requestRender();
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
  renderer.text.render(ATKINSON_HYPERLEGIBLE_16_FONT_ID,
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
