#pragma once

/**
 * @file BootActivity.h
 * @brief Public interface and types for BootActivity.
 */

#include "../Activity.h"
#include "system/MappedInputManager.h"

/**
 * @brief Boot activity that displays splash screen and loads system components.
 *
 * Loads system settings, application state, recent books, and book state while
 * showing a short progress indication. When KOReader sync settings exist on SD,
 * they are loaded during this phase. After initialization, transitions either to
 * the recent books view or directly to the last opened book per boot settings.
 */
class BootActivity : public Activity {
 public:
  /**
   * @brief Constructs the boot activity.
   *
   * @param renderer Graphics renderer for drawing the splash screen and progress bar
   * @param inputManager Input manager for handling user input during boot
   */
  BootActivity(GfxRenderer& renderer, MappedInputManager& inputManager);

  /**
   * @brief Initializes the boot activity when it becomes active.
   *
   * Displays the Corgi logo, draws the progress bar outline, loads persisted
   * state from SD, and completes boot in one pass (no staged delays).
   */
  void onEnter() override;

  /**
   * @brief Main update loop for the boot activity.
   *
   * When boot is complete, navigates to either the recent books view or the
   * last opened book based on application settings and saved state.
   */
  void loop() override;

 private:
  int bootProgress = 0;       ///< Current boot progress percentage (0-100)
  bool bootComplete = false;  ///< Flag indicating if boot sequence has finished

  /**
   * @brief Draws the boot progress bar on the screen.
   *
   * Renders a horizontal progress bar centered on the screen, with the fill
   * width determined by the current bootProgress value.
   */
  void drawProgressBar();
};
