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
  /** Constructs the about page bound to a renderer and input manager. */
  AboutPage(GfxRenderer& renderer, MappedInputManager& mappedInput);
  /** Destroys the about page. */
  ~AboutPage();

  /** Makes the about popup visible and draws it. */
  void show();
  /** Hides the about popup and marks it dismissed. */
  void hide();
  /** Processes input while the popup is visible, closing it on Back. */
  void handleInput();
  /** Redraws the popup if it is currently visible. */
  void render();
  /** Returns whether the popup is currently shown. */
  bool isVisible() const { return visible; }
  /** Returns whether the popup was dismissed by the user. */
  bool isDismissed() const { return dismissed; }

 private:
  /** Draws the popup contents and pushes a display refresh. */
  void renderWithRefresh();

  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

  bool visible;
  bool dismissed;
  uint32_t lastInputTime;
};

#endif
