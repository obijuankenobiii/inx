#pragma once

/**
 * @file Home.h
 * @brief Empty Home page shell for the inx-pro UI migration.
 */

#include "Page.h"
#include "components/global/PopUp.h"
#include "components/widget/Recent.h"
#include "navigation/Sidebar.h"

#include <string>

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
  bool recentPopupInput();
  void renderRecentPopup() const;
  void removeSelectedRecent();
  void deleteSelectedRecentCache();

 private:
  bool sidebarOpen = false;
  bool ignoreBackReleaseOnEnter_ = false;
  bool ignoreBackReleaseAfterPopup_ = false;
  bool confirmLongPressProcessed_ = false;
  int recentIndex_ = 0;
  int recentPopupAction_ = 0;
  std::string recentPopupPath_;
  widget::Recent recentWidget;
};
