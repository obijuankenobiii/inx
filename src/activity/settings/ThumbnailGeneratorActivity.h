#pragma once

/**
 * @file ThumbnailGeneratorActivity.h
 * @brief Generate missing book thumbnails from Settings.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "activity/ActivityWithSubactivity.h"

class ThumbnailGeneratorActivity final : public ActivityWithSubactivity {
 public:
  explicit ThumbnailGeneratorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                      const std::function<void()>& goBack)
      : ActivityWithSubactivity("ThumbnailGenerator", renderer, mappedInput), goBack(goBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  enum State : uint8_t { READY, RUNNING, SUCCESS, CANCELLED, FAILED };

  static void displayTaskTrampoline(void* param);
  static void workerTaskTrampoline(void* param);

  [[noreturn]] void displayTaskLoop();
  void workerTaskLoop();
  void render();
  void startGeneration();
  bool scanPath(const std::string& path);
  bool processBook(const std::string& path);
  bool isSupportedBookFile(const std::string& filename) const;
  bool shouldSkipPath(const char* name) const;

  const std::function<void()> goBack;
  TaskHandle_t displayTaskHandle = nullptr;
  TaskHandle_t workerTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  volatile bool updateRequired = false;
  volatile bool cancelRequested = false;
  volatile State state = READY;

  int processedCount = 0;
  int generatedCount = 0;
  int skippedCount = 0;
  int failedCount = 0;
  char currentPath[256] = {0};
};
