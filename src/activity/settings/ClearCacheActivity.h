#pragma once

/**
 * @file ClearCacheActivity.h
 * @brief Public interface and types for ClearCacheActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "activity/ActivityWithSubactivity.h"

class ClearCacheActivity final : public ActivityWithSubactivity {
 public:
  explicit ClearCacheActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::function<void()>& goBack)
      : ActivityWithSubactivity("ClearCache", renderer, mappedInput), goBack(goBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  enum State { WARNING, CLEARING, SUCCESS, FAILED };
  enum CacheGroup : uint8_t {
    GROUP_DISPLAY = 0,
    GROUP_BOOK = 1,
    GROUP_THUMBNAILS = 2,
    GROUP_RECENT = 3,
    GROUP_LIBRARY_INDEX = 4,
    GROUP_NETWORK = 5,
    GROUP_COUNT = 6
  };

  State state = WARNING;
  TaskHandle_t displayTaskHandle = nullptr;
  TaskHandle_t clearTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  const std::function<void()> goBack;

  int clearedCount = 0;
  int failedCount = 0;
  int selectedGroup = -1;
  bool selectedGroups[GROUP_COUNT] = {true, true, false, true, false, false};

  static void taskTrampoline(void* param);
  static void clearTaskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render();
  void startClearTask();
  void clearCache();
  bool anyGroupSelected() const;
};
