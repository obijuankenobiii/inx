#pragma once

/**
 * @file TxtReaderActivity.h
 * @brief Public interface and types for TxtReaderActivity.
 */

#include <Txt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <vector>

#include "state/SystemSetting.h"
#include "activity/ActivityWithSubactivity.h"

class TxtReaderActivity final : public ActivityWithSubactivity {
  std::unique_ptr<Txt> txt;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;
  bool updateRequired = false;
  const std::function<void()> onGoBack;
  const std::function<void()> onGoToRecent;

  
  std::vector<size_t> pageOffsets;  
  std::vector<std::string> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;

  
  int cachedFontId = 0;
  int cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = SystemSetting::LEFT_ALIGN;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void renderScreen();
  void renderPage();
  void renderStatusBar(int orientedMarginRight, int orientedMarginBottom, int orientedMarginLeft) const;

  void initializeReader();
  bool loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset);
  void buildPageIndex();
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  void saveProgress() const;
  void loadProgress();

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Txt> txt,
                             const std::function<void()>& onGoBack, const std::function<void()>& onGoToRecent)
      : ActivityWithSubactivity("TxtReader", renderer, mappedInput),
        txt(std::move(txt)),
        onGoBack(onGoBack),
        onGoToRecent(onGoToRecent) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
