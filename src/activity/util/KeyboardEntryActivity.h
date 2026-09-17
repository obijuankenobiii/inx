#pragma once

/**
 * @file KeyboardEntryActivity.h
 * @brief Public interface and types for KeyboardEntryActivity.
 */

#include <GfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <utility>

#include "../Activity.h"
#include "activity/page/components/search/SearchKeyboard.h"

/**
 * Reusable keyboard entry activity for text input.
 * Can be started from any activity that needs text entry.
 *
 * Usage:
 *   1. Create a KeyboardEntryActivity instance
 *   2. Set callbacks with setOnComplete() and setOnCancel()
 *   3. Call onEnter() to start the activity
 *   4. Call loop() in your main loop
 *   5. When complete or cancelled, callbacks will be invoked
 */
class KeyboardEntryActivity : public Activity {
 public:
  using OnCompleteCallback = std::function<void(const std::string&)>;
  using OnCancelCallback = std::function<void()>;

  /**
   * Constructor
   * @param renderer Reference to the GfxRenderer for drawing
   * @param mappedInput Reference to MappedInputManager for handling input
   * @param title Title to display above the keyboard
   * @param initialText Initial text to show in the input field
   * @param startY Y position to start rendering the keyboard
   * @param maxLength Maximum length of input text (0 for unlimited)
   * @param isPassword If true, display asterisks instead of actual characters
   * @param onComplete Callback invoked when input is complete
   * @param onCancel Callback invoked when input is cancelled
   */
  explicit KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string title = "Enter Text", std::string initialText = "", const int startY = 10,
                                 const size_t maxLength = 0, const bool isPassword = false,
                                 OnCompleteCallback onComplete = nullptr, OnCancelCallback onCancel = nullptr)
      : Activity("KeyboardEntry", renderer, mappedInput),
        title(std::move(title)),
        text(std::move(initialText)),
        startY(startY),
        maxLength(maxLength),
        isPassword(isPassword),
        onComplete(std::move(onComplete)),
        onCancel(std::move(onCancel)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  std::string title;
  int startY;
  std::string text;
  size_t maxLength;
  bool isPassword;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  SearchKeyboard keyboard;

  OnCompleteCallback onComplete;
  OnCancelCallback onCancel;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
};
