#pragma once

/**
 * @file Activity.h
 * @brief Public interface and types for Activity.
 */

#include <HardwareSerial.h>
#include <string>
#include <utility>

class MappedInputManager;
class GfxRenderer;

/**
 * @brief Base class for all activities in the application
 * 
 * Activities represent different screens or modes of the device such as
 * reading, browsing library, viewing settings, etc. Each activity manages
 * its own rendering and input handling.
 */
class Activity {
 protected:
  std::string name;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

 public:
  /**
   * @brief Construct a new Activity object
   * @param name The name identifier for this activity
   * @param renderer Reference to the graphics renderer
   * @param mappedInput Reference to the input manager
   */
  explicit Activity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : name(std::move(name)), renderer(renderer), mappedInput(mappedInput) {}

  /**
   * @brief Virtual destructor for proper cleanup of derived classes
   */
  virtual ~Activity() = default;

  /**
   * @brief Called when the activity becomes active
   * 
   * Override this method to perform setup operations when the activity is entered,
   * such as initializing variables, loading resources, or setting up the display.
   */
  virtual void onEnter() {}

  /**
   * @brief Called when the activity is being deactivated
   * 
   * Override this method to perform cleanup operations when leaving the activity,
   * such as saving state, freeing resources, or unsubscribing from events.
   */
  virtual void onExit() {}

  /**
   * @brief Main update loop for the activity
   * 
   * Override this method to implement the activity's main behavior.
   * This is called repeatedly while the activity is active.
   */
  virtual void loop() {}

  /**
   * @brief Determines whether to skip the delay between loop iterations
   * @return true to skip the delay, false to use standard delay
   * 
   * Override this method to return true for activities that require
   * maximum responsiveness or continuous updates.
   */
  virtual bool skipLoopDelay() { return false; }

  /**
   * @brief Determines whether the activity should prevent auto-sleep
   * @return true to prevent auto-sleep, false to allow auto-sleep
   * 
   * Override this method to return true for activities that should keep
   * the device awake, such as during reading or when user interaction is expected.
   */
  virtual bool preventAutoSleep() { return false; }
};