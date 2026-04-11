#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "../ActivityWithSubactivity.h"
#include "../Menu.h"

enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT, ADD_BLUETOOTH };

class SyncActivity final : public ActivityWithSubactivity, public Menu {
 public:
  SyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
               const std::function<void(NetworkMode)>& onModeSelected,
               const std::function<void()>& onRecentOpen = nullptr,
               const std::function<void()>& onStatisticsOpen = nullptr,
               const std::function<void()>& onSettingsOpen = nullptr)
      : ActivityWithSubactivity("Network Settings", renderer, mappedInput),
        Menu(),
        onModeSelected(onModeSelected),
        onRecentOpen(onRecentOpen),
        onStatisticsOpen(onStatisticsOpen),
        onSettingsOpen(onSettingsOpen) {
    tabSelectorIndex = 3;
  };

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  int selectedIndex = 0;
  bool updateRequired = false;

  const std::function<void(NetworkMode)> onModeSelected;
  const std::function<void()> onRecentOpen;
  const std::function<void()> onStatisticsOpen;
  const std::function<void()> onSettingsOpen;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;

  void navigateToSelectedMenu() override {
    if (tabSelectorIndex == 2 && onSettingsOpen) onSettingsOpen();
    if (tabSelectorIndex == 4 && onStatisticsOpen) onStatisticsOpen();
  }
};