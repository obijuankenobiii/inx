#ifndef ABOUT_PAGE_H
#define ABOUT_PAGE_H

#include <functional>
#include <string>

#include "GfxRenderer.h"
#include "system/MappedInputManager.h"

class AboutPage {
public:
  using DismissCallback = std::function<void()>;

  AboutPage(GfxRenderer& renderer, MappedInputManager& mappedInput, DismissCallback onDismiss);
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
  DismissCallback onDismiss;

  bool visible;
  bool dismissed;
  uint32_t lastInputTime;
};

#endif // ABOUT_PAGE_H