#pragma once

/**
 * @file Page.h
 * @brief Minimal visual page shell for the UI migration.
 */

#include "../Activity.h"
#include "navigation/Menu.h"

#include <functional>

/**
 * Empty page foundation.
 *
 * The shell deliberately has no navigation or input behavior yet. Derived
 * pages can add content while retaining the shared bottom menu.
 */
class Page : public Activity, public navigation::Menu {
 public:
  Page(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);
  Page(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~Page() override = default;

  const char* name() const override { return getName(); }
  void onEnter() override;
  void loop() override;

 protected:
  virtual bool back();
  virtual void search();
  virtual void content();
  virtual void menu();
  void requestRender() { updateRequired = true; }
  void renderIfNeeded();

 private:
  void render();

  bool updateRequired = true;
};
