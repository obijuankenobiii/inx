#pragma once

/**
 * @file SettingsActivity.h
 * @brief Public interface and types for SettingsActivity.
 */

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
  bool updateRequired = false;
  SettingsPanel currentPanel = SettingsPanel::System;

  bool isIndexing = false;
  int indexingProgress = 0;
  int indexingTotal = 0;
  char currentIndexingPath[256] = {0};
  int lastRenderedIndexingProgress = -1;
  int lastRenderedIndexingTotal = -1;
  unsigned long nextIndexingRenderMs = 0;
  bool showingAbout = false;
  int selectedAboutIndex = 0;
  bool panelSwapPending = false;

  // onRecentOpen/onLibraryOpen/onSyncOpen/onStatisticsOpen ultimately reach switchTo<T>() in main.cpp, which
  // deletes the current top-level Activity (this SettingsActivity, or its owner) before constructing the next
  // one. Sub-activities (CategorySettingsActivity, ReaderPresetsActivity) invoke these from deep inside their
  // own loop(), so calling them synchronously would leave code still running on `this` after it's freed - e.g.
  // subActivity->loop() (which can recurse into one of these) is followed by processPendingPanelSwap() here.
  // Deferring to a pending callback consumed as the very last thing in loop() (with nothing touching `this`
  // afterward) avoids that use-after-free.
  std::function<void()> pendingExternalNav_;
  void deferExternalNav(const std::function<void()>& fn) { pendingExternalNav_ = fn; }
  // Returns true if a deferred navigation was pending and has just been invoked. Callers must not touch
  // `this` after this returns true.
  bool runPendingExternalNav() {
    if (!pendingExternalNav_) {
      return false;
    }
    const std::function<void()> nav = std::move(pendingExternalNav_);
    pendingExternalNav_ = nullptr;
    nav();
    return true;
  }

  void openCurrentPanel();
  void swapPanelAndReopen();
  void requestPanelSwap();
  void processPendingPanelSwap();

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
      deferExternalNav(onRecentOpen);
    } else if (tabSelectorIndex == 1 && onLibraryOpen) {
      deferExternalNav(onLibraryOpen);
    } else if (tabSelectorIndex == 3 && onSyncOpen) {
      deferExternalNav(onSyncOpen);
    } else if (tabSelectorIndex == 4 && onStatisticsOpen) {
      deferExternalNav(onStatisticsOpen);
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
