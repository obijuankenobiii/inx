#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "activity/page/Page.h"
#include "activity/page/components/global/Button.h"
#include "system/FontPackageManager.h"
#include "system/UiLayout.h"

/** Downloads compiled SD-font packages and refreshes the local FontManager catalog. */
class FontManagerActivity final : public ActivityWithSubactivity {
 public:
  explicit FontManagerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const std::function<void()>& goBack)
      : ActivityWithSubactivity("FontManager", renderer, mappedInput), goBack_(goBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return state_ == State::Downloading; }

 private:
  enum class State : uint8_t { Ready, Downloading, Failed };
  enum class CategoryFilter : uint8_t { All, SansSerif, Serif };

  static constexpr int kRowHeight = UiLayout::LIST_ITEM_HEIGHT;
  static constexpr int kTabHeight = Button::height - 10;
  static constexpr int kTabWidth = 120;
  static constexpr int kTabGap = 0;

  const std::function<void()> goBack_;
  std::vector<FontPackageManager::Package> packages_;
  std::string status_;
  int activeVariant_ = 1;
  int selectedIndex_ = 0;
  int scrollOffset_ = 0;
  bool selectedVisible_ = false;
  bool categoryFilterOpen_ = false;
  CategoryFilter categoryFilter_ = CategoryFilter::All;
  int installingPackageIndex_ = -1;
  bool installedPopupOpen_ = false;
  int installedPopupPackageIndex_ = -1;
  int installedPopupActionIndex_ = 0;
  volatile size_t progressDownloaded_ = 0;
  volatile size_t progressTotal_ = 0;
  volatile bool updateRequired_ = false;
  volatile State state_ = State::Ready;
  TaskHandle_t displayTaskHandle_ = nullptr;
  TaskHandle_t installTaskHandle_ = nullptr;
  SemaphoreHandle_t renderingMutex_ = nullptr;
  volatile bool shuttingDown_ = false;
  volatile int lastProgressPercent_ = -1;
  volatile unsigned long lastProgressUpdateMs_ = 0;

  void loadPackages();
  void installSelected();
  void removeSelected();
  void openInstalledPopup();
  bool handleInstalledPopupInput();
  void renderInstalledPopup() const;
  void startInstallation();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  static void displayTaskTrampoline(void* param);
  static void installTaskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void installTaskLoop();
  void render();
  void updateDisplay();
  int visibleRowCount(int bodyTop) const;
  int listTop(int bodyTop) const { return bodyTop + kTabHeight + kTabGap + 20; }
  const char* activeVariantName() const { return activeVariant_ == 0 ? "1-bit" : "2-bit"; }
  int visiblePackageCount() const;
  int packageIndexAt(int visibleIndex) const;
  void selectVariant(int variant);
  int categoryFilterCount() const;
  const char* categoryFilterLabel() const;
  bool matchesCategory(const FontPackageManager::Package& package) const;
  ButtonBounds categoryFilterBounds() const;
  void categoryFilterDropdown() const;
  void handleCategoryFilterTap(int tapX, int tapY);
  void applyCategoryFilter(int index);
};
