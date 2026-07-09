#pragma once

#ifdef SIMULATOR

#include <cstddef>

#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT 0
#endif

inline size_t heap_caps_get_total_size(int) { return 1024 * 1024; }
inline size_t heap_caps_get_free_size(int) { return 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(int) { return 1024 * 1024; }

#else
#error "src/simulator/esp_heap_caps.h is only for simulator builds"
#endif
