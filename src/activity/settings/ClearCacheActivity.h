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
    GROUP_RECENT = 2,
    GROUP_NETWORK = 3,
    GROUP_COUNT = 4
  };

  State state = WARNING;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  const std::function<void()> goBack;

  int clearedCount = 0;
  int failedCount = 0;
  int selectedGroup = 0;
  bool selectedGroups[GROUP_COUNT] = {true, true, true, false};

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render();
  void clearCache();
  bool anyGroupSelected() const;
};
