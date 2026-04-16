#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "activity/ActivityWithSubactivity.h"
#include "../Menu.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public ActivityWithSubactivity, public Menu {
  enum State {
    WIFI_SELECTION,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  const std::function<void()> goBack;
  State state = WIFI_SELECTION;
  OtaUpdater updater;

  void onWifiSelectionComplete(bool success);
  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render();

  void navigateToSelectedMenu() override {}

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::function<void()>& goBack)
      : ActivityWithSubactivity("OtaUpdate", renderer, mappedInput), Menu(), goBack(goBack), updater() {
    tabSelectorIndex = 3;
  }
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS; }
};
