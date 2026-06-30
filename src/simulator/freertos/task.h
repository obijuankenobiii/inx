#pragma once

#ifdef SIMULATOR

#include_next <freertos/task.h>

#include "FreeRTOS.h"

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

#ifndef pdPASS
#define pdPASS 1
#endif

#else
#error "src/simulator/freertos/task.h is only for simulator builds"
#endif
