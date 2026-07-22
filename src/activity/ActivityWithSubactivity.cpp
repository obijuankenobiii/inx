/**
 * @file ActivityWithSubactivity.cpp
 * @brief Definitions for ActivityWithSubactivity.
 */

#include "ActivityWithSubactivity.h"

#include <Arduino.h>

/** Exits and destroys the current subactivity, if any. */
void ActivityWithSubactivity::exitActivity() {
  if (subActivity) {
    subActivity->onExit();
    subActivity.reset();
  }
}

/** Replaces the current subactivity with the given one and enters it. */
void ActivityWithSubactivity::enterNewActivity(Activity* activity) {
  subActivity.reset(activity);
#ifdef SIMULATOR
  Serial.printf("[%lu] [SIM] Subactivity: %s\n", millis(), subActivity->getName());
#endif
  subActivity->onEnter();
}

/** Forwards the loop call to the active subactivity, if any. */
void ActivityWithSubactivity::loop() {
  if (subActivity) {
    subActivity->loop();
  }
}

/** Cleans up the current subactivity on exit. */
void ActivityWithSubactivity::onExit() {
  Activity::onExit();
  exitActivity();
}
