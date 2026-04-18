#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct BleStoredDevice {
  std::string address;
  std::string name;
  /** NimBLE address type (`BLE_ADDR_PUBLIC`=0, `BLE_ADDR_RANDOM`=1, …). Default 1 for legacy files. */
  uint8_t addrType = 1;
};

/**
 * @brief Persisted list of BLE keyboards the user has seen with usable names (separate from settings.bin).
 */
class BleDeviceStore {
  BleDeviceStore() = default;
  static BleDeviceStore instance;

 public:
  BleDeviceStore(const BleDeviceStore&) = delete;
  BleDeviceStore& operator=(const BleDeviceStore&) = delete;

  static BleDeviceStore& getInstance() { return instance; }

  bool loadFromFile();
  bool saveToFile();

  const std::vector<BleStoredDevice>& devices() const { return m_devices; }

  /** Name is non-empty after trim and not the generic "Unknown" placeholder (scan noise). */
  static bool isDisplayableName(const std::string& name);

  /** Adds or updates by address; ignores if name not displayable. Returns true if list changed. */
  bool addOrUpdate(const std::string& address, const std::string& name, uint8_t addrType);

  void removeAt(size_t index);

  /** Writes this device as the preferred keyboard (SETTINGS.bleSaved* + save). */
  void applyPreferred(const std::string& address, const std::string& name, uint8_t addrType);

  /** Index in m_devices, or -1. */
  int findIndexByAddress(const std::string& address) const;

 private:
  static void toLowerInPlace(std::string& s);
  static std::string addressKey(const std::string& address);

  std::vector<BleStoredDevice> m_devices;
  static constexpr size_t kMaxDevices = 24;
  static constexpr uint8_t kFileVersion = 2;
};

#define BLE_DEVICES BleDeviceStore::getInstance()
