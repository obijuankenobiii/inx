#pragma once

#ifdef SIMULATOR

#include <HalStorage.h>

#ifndef FILE_READ
#define FILE_READ O_READ
#endif

#ifndef FILE_WRITE
#define FILE_WRITE (O_WRITE | O_CREAT | O_TRUNC)
#endif

#else
#error "src/simulator/SdFat.h is only for simulator builds"
#endif
