/**
 * @file Page.cpp
 * @brief Minimal visual page shell for the UI migration.
 */

#include "Page.h"

#include <GfxRenderer.h>

Page::Page(const char* pageName, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(pageName, renderer, mappedInput), navigation::Menu(renderer) {}

Page::Page(GfxRenderer& renderer, MappedInputManager& mappedInput) : Page("Page", renderer, mappedInput) {}

void Page::onEnter() {
  Activity::onEnter();
  updateRequired = true;
}

void Page::loop() {
  if (!updateRequired) return;
  render();
  updateRequired = false;
}

void Page::content() {}

void Page::menu() { navigation::Menu::render(); }

void Page::render() {
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  content();
  menu();
  renderer.displayBuffer();
}
