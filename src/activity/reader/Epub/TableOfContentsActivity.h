#pragma once

/**
 * @file TableOfContentsActivity.h
 * @brief Public interface and types for TableOfContentsActivity.
 */

#include <Epub.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <memory>

#include "../../ActivityWithSubactivity.h"

class TableOfContentsActivity final : public ActivityWithSubactivity {
  std::shared_ptr<Epub> epub;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  int currentSpineIndex = 0;
  int selectorIndex = 0;
  bool updateRequired = false;
  
  const std::function<void(int newSpineIndex)> onSelectSpineIndex;

  int getPageItems() const;
  
  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void renderScreen();

public:
  explicit TableOfContentsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::shared_ptr<Epub>& epub,
                                   const int currentSpineIndex,
                                   const std::function<void(int newSpineIndex)>& onSelectSpineIndex)
      : ActivityWithSubactivity("TableOfContents", renderer, mappedInput),
        epub(epub),
        currentSpineIndex(currentSpineIndex),
        onSelectSpineIndex(onSelectSpineIndex) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
};