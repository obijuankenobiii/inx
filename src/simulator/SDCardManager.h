#pragma once

#ifdef SIMULATOR

#include <HalStorage.h>

using SDCardManager = HalStorage;

#ifndef FILE_READ
#define FILE_READ O_READ
#endif

#ifndef FILE_WRITE
#define FILE_WRITE (O_WRITE | O_CREAT | O_TRUNC)
#endif

#ifndef SdMan
#define SdMan Storage
#endif

#else
#error "src/simulator/SDCardManager.h is only for simulator builds"
#endif
