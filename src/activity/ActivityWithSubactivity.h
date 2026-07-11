#pragma once

/**
 * @file ActivityWithSubactivity.h
 * @brief Public interface and types for ActivityWithSubactivity.
 */

#include <memory>

#include "Activity.h"

class ActivityWithSubactivity : public Activity {
 protected:
  std::unique_ptr<Activity> subActivity = nullptr;
  /** Exits and destroys the current subactivity, if any. */
  void exitActivity();
  /** Replaces the current subactivity with the given one and enters it. */
  void enterNewActivity(Activity* activity);

 public:
  /** Constructs an ActivityWithSubactivity with the given name and dependencies. */
  explicit ActivityWithSubactivity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity(std::move(name), renderer, mappedInput) {}

  /** Forwards the loop call to the active subactivity, if any. */
  void loop() override;
  /** Cleans up the current subactivity on exit. */
  void onExit() override;
};
