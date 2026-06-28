#pragma once

#ifdef SIMULATOR

#include_next <HalDisplay.h>

#ifndef MANUAL_REFRESH
#define MANUAL_REFRESH FAST_REFRESH
#endif

#ifndef STRONG_FAST_REFRESH
#define STRONG_FAST_REFRESH FAST_REFRESH
#endif

#else
#error "src/simulator/HalDisplay.h is only for simulator builds"
#endif
