/**
 * @file SubPage.cpp
 * @brief Minimal visual sub-page shell for the UI migration.
 */

#include "SubPage.h"

#include <GfxRenderer.h>

#include "images/Close.h"
#include "system/Fonts.h"

SubPage::SubPage(const char* pageName, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Page(pageName, renderer, mappedInput) {}

SubPage::SubPage(GfxRenderer& renderer, MappedInputManager& mappedInput) : SubPage("SubPage", renderer, mappedInput) {}

void SubPage::title() const {
  renderer.bitmap.icon(Close, 20, 15, 40, 40);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_16_FONT_ID, 72, 26, name(), true, EpdFontFamily::BOLD);
}
