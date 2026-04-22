#pragma once

/**
 * @file SleepImagePickerActivity.h
 * @brief Public interface and types for SleepImagePickerActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "activity/ActivityWithSubactivity.h"

/**
 * Lists BMPs under /sleep/ (and optional /sleep.bmp at card root) so the user can
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
  };

  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  std::vector<Row> rows;
  int selectedIndex = 0;
  int scrollOffset = 0;
  int itemsPerPage = 1;

  const std::function<void()> onBack;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void rebuildRows();
  void render();
  void applySelection();
};
