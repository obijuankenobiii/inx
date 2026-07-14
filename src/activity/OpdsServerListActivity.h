#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "activity/ActivityWithSubactivity.h"

class OpdsServerListActivity final : public ActivityWithSubactivity {
 public:
  /** Constructs an OpdsServerListActivity with a callback invoked when the user goes back. */
  explicit OpdsServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  const std::function<void()>& onBack)
      : ActivityWithSubactivity("OpdsServerList", renderer, mappedInput), onBack(onBack) {}

  /** Loads the OPDS server list and starts the display task. */
  void onEnter() override;
  /** Stops the display task and cleans up rendering resources. */
  void onExit() override;
  /** Handles input for navigating and selecting an OPDS server. */
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  int selectedIndex = 0;
  const std::function<void()> onBack;

  /** Static trampoline that dispatches to the instance's displayTaskLoop. */
  static void taskTrampoline(void* param);
  /** Background task loop that renders the screen when an update is required. */
  [[noreturn]] void displayTaskLoop();
  /** Renders the list of OPDS servers. */
  void render();
  /** Enters the book browser subactivity for the currently selected server. */
  void handleSelection();
};
