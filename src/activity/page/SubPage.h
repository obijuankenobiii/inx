#pragma once

/**
 * @file SubPage.h
 * @brief Minimal visual sub-page shell for the UI migration.
 */

#include "Page.h"

/** Empty sub-page shell; behavior will be migrated in a later slice. */
class SubPage : public Page {
 public:
  SubPage(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);
  SubPage(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~SubPage() override = default;

 protected:
  void title() const override;
};
