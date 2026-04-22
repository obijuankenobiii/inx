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
 * The BootActivity manages the application startup sequence, displaying a
 * logo and progress bar while loading system settings, application state,
 * recent books, and book state. After initialization completes, it transitions
 * either to the recent books view or directly to the last opened book.
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
   * Displays the Corgi logo, draws the progress bar outline, and resets
   * all boot state variables including progress tracking and stage counter.
   */
  void onEnter() override;
  
  /**
   * @brief Main update loop for the boot activity.
   * 
   * Advances the boot sequence at timed intervals. When boot is complete,
   * navigates to either the recent books view or directly to the last opened
   * book based on application settings and saved state.
   */
  void loop() override;
  
 private:
  int bootProgress = 0;           ///< Current boot progress percentage (0-100)
  unsigned long lastBootUpdate = 0; ///< Timestamp of the last progress update
  bool bootComplete = false;      ///< Flag indicating if boot sequence has finished
  int bootStage = 0;              ///< Current stage in the boot sequence
  
  /**
   * @brief Draws the boot progress bar on the screen.
   * 
   * Renders a horizontal progress bar centered on the screen, with the fill
   * width determined by the current bootProgress value.
   */
  void drawProgressBar();
  
  /**
   * @brief Advances the boot sequence to the next initialization stage.
   * 
   * Executes the next step in the boot process, loading system components
   * such as settings, app state, recent books, and book state in sequence.
   */
  void initializeNextStage();
};