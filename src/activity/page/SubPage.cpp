/**
 * @file SubPage.cpp
 * @brief Minimal visual sub-page shell for the UI migration.
 */

#include "SubPage.h"

#include <GfxRenderer.h>

#include "images/Close.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

SubPage::SubPage(const char* pageName, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Page(pageName, renderer, mappedInput) {}

SubPage::SubPage(GfxRenderer& renderer, MappedInputManager& mappedInput) : SubPage("SubPage", renderer, mappedInput) {}

int SubPage::header(const GfxRenderer& renderer, const char* name) {
  constexpr int top = 20;
  constexpr int size = 40;
  const int font = MONTSERRAT_16_FONT_ID;
  const int textY = top + (size - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, 20, textY, name ? name : "", true, EpdFontFamily::BOLD);
  renderer.bitmap.icon(Close, renderer.getScreenWidth() - 60, top, size, size);
  return top + size + 20;
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
  renderer.bitmap.icon(Close, 20, 15, 40, 40);
  renderer.text.render(MONTSERRAT_16_FONT_ID, 72, 26, name(), true, EpdFontFamily::BOLD);
}
