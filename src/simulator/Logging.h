#pragma once

#ifdef SIMULATOR

#include <HardwareSerial.h>

#include <cstdarg>
#include <cstdio>

class MySerialImpl : public HWCDC {
 public:
  static MySerialImpl instance;

  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  void flush() override;
  size_t printf(const char* format, ...);
};

static HWCDC logSerial;

#ifdef Serial
#undef Serial
#endif
#define Serial MySerialImpl::instance

#define LOG_DBG(tag, fmt, ...) Serial.printf("[%lu] [%s] " fmt "\n", millis(), tag, ##__VA_ARGS__)
#define LOG_ERR(tag, fmt, ...) Serial.printf("[%lu] [%s] " fmt "\n", millis(), tag, ##__VA_ARGS__)

#else
#error "src/simulator/Logging.h is only for simulator builds"
#endif
