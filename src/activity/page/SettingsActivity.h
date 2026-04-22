#pragma once

/**
 * @file SettingsActivity.h
 * @brief Public interface and types for SettingsActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "../Menu.h"
#include "../settings/AboutPage.h"
#include "activity/ActivityWithSubactivity.h"

class SystemSetting;
struct SettingInfo;

enum class SettingsPanel : uint8_t { System, Reader };

class SettingsActivity final : public ActivityWithSubactivity, public Menu {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;

  bool updateRequired = false;
  SettingsPanel currentPanel = SettingsPanel::System;

  bool isIndexing = false;
  int indexingProgress = 0;
  int indexingTotal = 0;
  char currentIndexingPath[256] = {0};
  bool showingAbout = false;
  int selectedAboutIndex = 0;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();

  void openCurrentPanel();
  void swapPanelAndReopen();

  void startLibraryIndexing();
  void showIndexingProgress();
  void renderIndexingProgress(int progress, int total, const char* currentPath) const;

  const std::function<void()> onRecentOpen;
  const std::function<void()> onLibraryOpen;
  const std::function<void()> onSyncOpen;
  const std::function<void()> onStatisticsOpen;

  void navigateToSelectedMenu() override {
    SETTINGS.saveToFile();
    if (tabSelectorIndex == 0 && onRecentOpen) {
      onRecentOpen();
    } else if (tabSelectorIndex == 1 && onLibraryOpen) {
      onLibraryOpen();
    } else if (tabSelectorIndex == 3 && onSyncOpen) {
      onSyncOpen();
    } else if (tabSelectorIndex == 4 && onStatisticsOpen) {
      onStatisticsOpen();
    }
  }

 private:
  AboutPage* aboutPage = nullptr;

  static const char* panelBackLabel(SettingsPanel panel);

 public:
  SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                   const std::function<void()>& onRecentOpen = nullptr,
                   const std::function<void()>& onLibraryOpen = nullptr,
                   const std::function<void()>& onSyncOpen = nullptr,
                   const std::function<void()>& onStatisticsOpen = nullptr)
      : ActivityWithSubactivity("Settings", renderer, mappedInput),
        Menu(),
        onRecentOpen(onRecentOpen),
        onLibraryOpen(onLibraryOpen),
        onSyncOpen(onSyncOpen),
        onStatisticsOpen(onStatisticsOpen) {
    tabSelectorIndex = 2;
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
};