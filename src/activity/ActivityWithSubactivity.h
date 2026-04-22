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
  void exitActivity();
  void enterNewActivity(Activity* activity);

 public:
  explicit ActivityWithSubactivity(
    std::string name, 
    GfxRenderer& renderer, 
    MappedInputManager& mappedInput
  ) : Activity(std::move(name), renderer, mappedInput) {}
  
  void loop() override;
  void onExit() override;
};
