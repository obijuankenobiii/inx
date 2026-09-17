#pragma once

namespace SdIoMutex {

/** Serializes filesystem/SPI transactions across activity and cache tasks. */
void lock();
void unlock();

class Lock final {
 public:
  Lock() { lock(); }
  ~Lock() { unlock(); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

}  // namespace SdIoMutex
