#pragma once

#ifdef SIMULATOR

#include <netdb.h>

#else
#error "src/simulator/lwip/netdb.h is only for simulator builds"
#endif
