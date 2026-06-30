#pragma once

#ifdef SIMULATOR

#include_next <freertos/FreeRTOS.h>

#include <cstdint>

using TickType_t = uint32_t;
using BaseType_t = int;
using UBaseType_t = unsigned int;

#ifndef pdPASS
#define pdPASS 1
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

#else
#error "src/simulator/freertos/FreeRTOS.h is only for simulator builds"
#endif
