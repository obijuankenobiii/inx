#pragma once

#ifdef SIMULATOR

#include_next <freertos/semphr.h>

inline void vSemaphoreDelete(SemaphoreHandle_t sem) { delete sem; }

#else
#error "src/simulator/freertos/semphr.h is only for simulator builds"
#endif
