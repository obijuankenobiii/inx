#pragma once

#ifdef SIMULATOR

#include <NetworkUdp.h>

using WiFiUDP = NetworkUDP;

#else
#error "src/simulator/WiFiUdp.h is only for simulator builds"
#endif
