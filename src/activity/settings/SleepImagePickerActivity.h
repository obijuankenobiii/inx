#pragma once

/**
 * @file SleepImagePickerActivity.h
 * @brief Public interface and types for SleepImagePickerActivity.
 */

#include <functional>
#include <string>
#include <vector>

#include "activity/ActivityWithSubactivity.h"

/**
 * Lists sleep images under /sleep/ (BMP/JPG/JPEG + optional SD-root sleep.bmp/jpg/jpeg) so the user can
 * pin one image or leave selection random for each sleep.
 */
class SleepImagePickerActivity final : public ActivityWithSubactivity {
 public:
  explicit SleepImagePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    const std::function<void()>& onBack)
      : ActivityWithSubactivity("SleepImagePicker", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  struct Row {
    std::string label;
    std::string value;
    std::string previewPath;
  };

  bool updateRequired = false;

  std::vector<Row> rows;
  int selectedIndex = 0;
  bool randomEnabled = false;

  const std::function<void()> onBack;

  void rebuildRows();
  void render();
  void applySelection();
  void requestRedraw();
};
