#pragma once

/**
 * @file Home.h
 * @brief Empty Home page shell for the inx-pro UI migration.
 */

#include "Page.h"
#include "navigation/Sidebar.h"

/** Home page with inx-pro header, carousel content, and bottom navigation chrome. */
class Home final : public Page {
 public:
  Home(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~Home() override = default;

 protected:
  void onEnter() override;
  void loop() override;
  void menu() override;
  void title() const override;
  void content() override;
  void navigateToSelectedMenu() override;
  int top() const;
  int bottom() const;

 private:
  bool sidebarOpen = false;
  bool ignoreBackReleaseOnEnter_ = false;
};
