#include "SdIoMutex.h"

#if defined(ARDUINO_ARCH_ESP32)

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
StaticSemaphore_t gStorage;
SemaphoreHandle_t gMutex = nullptr;
portMUX_TYPE gInitMux = portMUX_INITIALIZER_UNLOCKED;

void ensureMutex() {
  if (gMutex != nullptr) return;
  portENTER_CRITICAL(&gInitMux);
  if (gMutex == nullptr) gMutex = xSemaphoreCreateRecursiveMutexStatic(&gStorage);
  portEXIT_CRITICAL(&gInitMux);
}
}  // namespace

namespace SdIoMutex {
void lock() {
  ensureMutex();
  if (gMutex != nullptr) xSemaphoreTakeRecursive(gMutex, portMAX_DELAY);
}

void unlock() {
  if (gMutex != nullptr) xSemaphoreGiveRecursive(gMutex);
}
}  // namespace SdIoMutex

#else

namespace SdIoMutex {
void lock() {}
void unlock() {}
}  // namespace SdIoMutex

#endif
