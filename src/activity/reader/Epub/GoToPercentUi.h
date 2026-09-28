#pragma once

/**
 * @file GoToPercentUi.h
 * @brief In-book percentage navigation popup.
 */

class EpubActivity;

class GoToPercentUi {
 public:
  bool isActive() const { return active_; }

  void enter(EpubActivity& act);
  void handleInput(EpubActivity& act);

 private:
  void render(EpubActivity& act);

  bool active_ = false;
  int percent_ = 0;
  bool changed_ = false;
};
