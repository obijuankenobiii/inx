#pragma once

#ifdef SIMULATOR

#ifdef __cplusplus
#include <chrono>
#include <cstdint>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
using TickType_t = uint32_t;
using BaseType_t = int;
using UBaseType_t = unsigned int;
#else
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
#endif

#ifndef pdPASS
#define pdPASS 1
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

#ifndef SNTP_SYNC_STATUS_RESET
#define SNTP_SYNC_STATUS_RESET 0
#endif

#ifdef __cplusplus
inline void sntp_set_sync_status(int) {}
#endif

#ifndef vSemaphoreDelete
#define vSemaphoreDelete(sem) delete (sem)
#endif

#ifdef __cplusplus
inline TickType_t xTaskGetTickCount() {
  using namespace std::chrono;
  static const steady_clock::time_point start = steady_clock::now();
  return static_cast<TickType_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}
#endif

#ifndef MANUAL_REFRESH
#define MANUAL_REFRESH FAST_REFRESH
#endif

#ifndef STRONG_FAST_REFRESH
#define STRONG_FAST_REFRESH FAST_REFRESH
#endif

#endif
