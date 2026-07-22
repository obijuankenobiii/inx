#pragma once

/**
 * @file CalibreSettingsActivity.h
 * @brief Public interface and types for CalibreSettingsActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "activity/ActivityWithSubactivity.h"

/**
 * Submenu for OPDS Browser settings.
 * Shows OPDS Server URL and HTTP authentication options.
 */
class CalibreSettingsActivity final : public ActivityWithSubactivity {
 public:
  /** Constructs the activity with its render context and back callback. */
  explicit CalibreSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::function<void()>& onBack)
      : ActivityWithSubactivity("CalibreSettings", renderer, mappedInput), onBack(onBack) {}

  /** Starts the display task and initializes selection state. */
  void onEnter() override;
  /** Stops the display task and releases synchronization resources. */
  void onExit() override;
  /** Handles one iteration of input processing for the menu. */
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  int selectedIndex = 0;
  const std::function<void()> onBack;

  /** FreeRTOS task entry point that forwards to displayTaskLoop. */
  static void taskTrampoline(void* param);
  /** Background task loop that redraws the menu when required. */
  [[noreturn]] void displayTaskLoop();
  /** Draws the OPDS settings menu. */
  void render();
  /** Opens the keyboard entry activity for the currently selected setting. */
  void handleSelection();
};
