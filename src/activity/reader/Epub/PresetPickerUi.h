#pragma once

/**
 * @file PresetPickerUi.h
 * @brief Popup for quickly applying a saved reader preset from the EPUB reader.
 */

#include <vector>

class EpubActivity;

class PresetPickerUi {
 public:
  bool isActive() const { return mode_; }

  void enter(EpubActivity& act);
  void handleInput(EpubActivity& act);

 private:
  void rebuildVisiblePresets();
  void clampScroll();
  void render(EpubActivity& act);

  bool mode_ = false;
  int selected_ = 0;  // visible row index
  int scroll_ = 0;
  std::vector<int> visiblePresetIndices_;
};
