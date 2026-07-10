#pragma once
// Minimal Arduino/ESP32 shims for native unit-test builds.
// Only the symbols actually referenced by PngToBmpConverter.cpp are needed.

#include <cstdarg>
#include <cstdint>
#include <cstdio>

struct HardwareSerialStub {
  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
  }
};

extern HardwareSerialStub Serial;
