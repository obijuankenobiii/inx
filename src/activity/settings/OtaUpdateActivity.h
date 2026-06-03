#pragma once

/**
 * @file OtaUpdateActivity.h
 * @brief Public interface and types for OtaUpdateActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <string>
#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "../Menu.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public ActivityWithSubactivity, public Menu {
  enum State {
    SOURCE_SELECTION,
    WIFI_SELECTION,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    WAITING_SD_SELECTION,
    WAITING_SD_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  int sourceSelectedIndex = 0;
  int sdFirmwareSelectedIndex = 0;
  int sdFirmwareScrollOffset = 0;
  std::vector<std::string> sdFirmwareFiles;
  const std::function<void()> goBack;
  State state = SOURCE_SELECTION;
  OtaUpdater updater;

  void onWifiSelectionComplete(bool success);
  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render();
  void scanSdFirmwareFiles();
  const std::string& selectedSdFirmwarePath() const;

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
