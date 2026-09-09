/**
 * @file Page.cpp
 * @brief Minimal visual page shell for the UI migration.
 */

#include "Page.h"

#include <GfxRenderer.h>

extern void onGoToRecent();

Page::Page(const char* pageName, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(pageName, renderer, mappedInput), navigation::Menu(renderer) {}

Page::Page(GfxRenderer& renderer, MappedInputManager& mappedInput) : Page("Page", renderer, mappedInput) {}

void Page::onEnter() {
  Activity::onEnter();
  updateRequired = true;
}

void Page::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    back();
    return;
  }
  if (mappedInput.wasPressed(tabPrevButton())) {
    handleTabNavigation(true, false);
    return;
  }
  if (mappedInput.wasPressed(tabNextButton())) {
    handleTabNavigation(false, true);
    return;
  }
  if (!updateRequired) return;
  render();
  updateRequired = false;
}

bool Page::back() {
  onGoToRecent();
  return true;
}

void Page::content() {}

void Page::menu() { navigation::Menu::render(); }

void Page::renderIfNeeded() {
  if (!updateRequired) return;
  render();
  updateRequired = false;
}

void Page::render() {
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  content();
  menu();
  renderer.displayBuffer();
}
