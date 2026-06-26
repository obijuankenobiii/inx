#pragma once

/**
 * @file SleepImagePickerActivity.h
 * @brief Public interface and types for SleepImagePickerActivity.
 */

#include <functional>
#include <cstdint>
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
  int renderedPageStart = -1;
  uint8_t* gridBuffer = nullptr;
  bool gridBufferStored = false;
  int gridBufferPageStart = -1;

  const std::function<void()> onBack;

  void rebuildRows();
  void render();
  void drawPickerChrome(int pageStart, int rowCount, bool hasImages, bool localRandomEnabled, bool drawCells = true);
  void drawPickerThumbnails(int pageStart, int rowCount);
  void drawSelectionFrame(int pageStart, int rowCount, int index);
  int pageStartForIndex(int index) const;
  int slotForIndex(int pageStart, int index) const;
  int indexForSlot(int pageStart, int slot) const;
  bool storeGridBuffer(int pageStart);
  bool restoreGridBuffer(int pageStart);
  void freeGridBuffer();
  void applySelection();
  void requestRedraw();
};
