/**
 * @file SubPage.cpp
 * @brief Minimal visual sub-page shell for the UI migration.
 */

#include "SubPage.h"

#include <GfxRenderer.h>

#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

SubPage::SubPage(const char* pageName, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Page(pageName, renderer, mappedInput) {}

SubPage::SubPage(GfxRenderer& renderer, MappedInputManager& mappedInput) : SubPage("SubPage", renderer, mappedInput) {}

int SubPage::header(const GfxRenderer& renderer, const char* name) {
  return ScreenComponents::drawSubPageHeader(renderer, name);
}

bool SubPage::closeInput(GfxRenderer& renderer, MappedInputManager& mappedInput,
                         const std::function<void()>& close, const bool closeOnSwipeUp) {
  (void)renderer;
  (void)closeOnSwipeUp;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (close) close();
    return true;
  }
  return false;
}

void SubPage::title() const {
  ScreenComponents::drawSubPageHeader(renderer, name());
}
