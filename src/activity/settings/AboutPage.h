/**
 * @file AboutPage.h
 * @brief Public interface and types for AboutPage.
 */

#ifndef ABOUT_PAGE_H
#define ABOUT_PAGE_H

#include "GfxRenderer.h"
#include "system/MappedInputManager.h"

class AboutPage {
 public:
  AboutPage(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~AboutPage();

  void show();
  void hide();
  void handleInput();
  void render();
  bool isVisible() const { return visible; }
  bool isDismissed() const { return dismissed; }

 private:
  void renderWithRefresh();

  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

  bool visible;
  bool dismissed;
  uint32_t lastInputTime;
};

#endif  
