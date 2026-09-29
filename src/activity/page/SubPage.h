#pragma once

/**
 * @file SubPage.h
 * @brief Minimal visual sub-page shell for the UI migration.
 */

#include "Page.h"

/** Shared sub-page shell and header/input helpers. */
class SubPage : public Page {
 public:
  SubPage(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);
  SubPage(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~SubPage() override = default;

  static int header(const GfxRenderer& renderer, const char* name);
  static bool closeInput(GfxRenderer& renderer, MappedInputManager& mappedInput,
                         const std::function<void()>& close, bool closeOnSwipeUp = true);

 protected:
  void title() const override;
};
