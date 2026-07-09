#pragma once

#ifdef SIMULATOR

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#else
#error "src/simulator/lwip/sockets.h is only for simulator builds"
#endif
