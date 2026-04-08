#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "../Menu.h"
#include "activity/ActivityWithSubactivity.h"

class SystemSetting;
struct SettingInfo;

class SettingsActivity final : public ActivityWithSubactivity, public Menu {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;

  bool updateRequired = false;
  static constexpr int categoryCount = 4;
  static const char* categoryNames[categoryCount];
  int selectedCategoryIndex = 0;

  bool isIndexing = false;
  int indexingProgress = 0;
  int indexingTotal = 0;
  char currentIndexingPath[256] = {0};
  bool showingAbout = false;
  int selectedAboutIndex = 0;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();

  void render() const;
  void renderSettingsList() const;
  void enterCategory(int categoryIndex);

  void startLibraryIndexing();
  void showIndexingProgress();
  void renderIndexingProgress(int progress, int total, const char* currentPath) const;

  const std::function<void()> onRecentOpen;
  const std::function<void()> onLibraryOpen;
  const std::function<void()> onSyncOpen;

  void navigateToSelectedMenu() override {
    if (tabSelectorIndex == 1 && onLibraryOpen) {
      onLibraryOpen();
    }

    if (tabSelectorIndex == 3 && onSyncOpen) {
      onSyncOpen();
    }
  }

 public:
  SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                   const std::function<void()>& onRecentOpen = nullptr,
                   const std::function<void()>& onLibraryOpen = nullptr,
                   const std::function<void()>& onSyncOpen = nullptr)
      : ActivityWithSubactivity("Settings", renderer, mappedInput),
        Menu(),
        onRecentOpen(onRecentOpen),
        onLibraryOpen(onLibraryOpen),
        onSyncOpen(onSyncOpen) {
    tabSelectorIndex = 2;
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void showAboutPage();
};