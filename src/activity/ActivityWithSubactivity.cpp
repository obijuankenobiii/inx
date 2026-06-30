/**
 * @file ActivityWithSubactivity.cpp
 * @brief Definitions for ActivityWithSubactivity.
 */

#include "ActivityWithSubactivity.h"

#include <Arduino.h>

void ActivityWithSubactivity::exitActivity() {
  if (subActivity) {
    subActivity->onExit();
    subActivity.reset();
  }
}

void ActivityWithSubactivity::enterNewActivity(Activity* activity) {
  subActivity.reset(activity);
#ifdef SIMULATOR
  Serial.printf("[%lu] [SIM] Subactivity: %s\n", millis(), subActivity->getName());
#endif
  subActivity->onEnter();
}

void ActivityWithSubactivity::loop() {
  if (subActivity) {
    subActivity->loop();
  }
}

void ActivityWithSubactivity::onExit() {
  Activity::onExit();
  exitActivity();
}
