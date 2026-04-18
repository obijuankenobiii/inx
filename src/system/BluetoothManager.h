#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

class HalGPIO;
class NimBLEClient;

class BluetoothManager {
 public:
  struct Device {
    std::string address;
    std::string name;
    int rssi;
    bool isHID;
  };

  static BluetoothManager& getInstance();
  static BluetoothManager* getInstancePtr();

  bool enable();
  bool disable();
  bool isEnabled() const;

  void startScan(uint32_t durationMs);
  void stopScan();
  bool isScanning() const { return m_scanning; }
  std::vector<Device> getDiscoveredDevices() const { return m_devices; }

  bool connectToDevice(const std::string& address);
  void disconnectAll();
  bool isConnected(const std::string& address) const;
  std::vector<std::string> getConnectedDevices() const { return m_connected; }

  using KeyCallback = std::function<void(uint8_t keycode, bool pressed)>;
  void setKeyCallback(KeyCallback cb) { m_keyCallback = cb; }

  /** BLE HID injects into the same HalGPIO path as physical keys (set once at startup). */
  void setHalGpio(HalGPIO* gpio) { m_halGpio = gpio; }

  /** Non-blocking: after boot, try to reconnect to SETTINGS.bleSavedAddress if enabled. */
  void scheduleStartupReconnect();

  /** Blocking connect to saved device; returns whether connected. */
  bool tryReconnectSavedDevice();

  void onScanResult(void* device);

  /** Called from NimBLE notify (HID boot keyboard report). */
  void onHidReport(const uint8_t* data, size_t len);

  /** NimBLE client disconnect callback (must be public for friend-free callbacks). */
  void handleClientDisconnected(NimBLEClient* pClient);

 private:
  BluetoothManager();
  ~BluetoothManager();

  void trySubscribeHid(NimBLEClient* pClient);

  bool m_enabled;
  bool m_scanning;
  std::vector<Device> m_devices;
  std::vector<std::string> m_connected;
  std::map<std::string, NimBLEClient*> m_clientsByAddr;
  KeyCallback m_keyCallback;
  HalGPIO* m_halGpio = nullptr;
  uint8_t m_prevHidBoot[8]{};

  static BluetoothManager* s_instance;
};

#endif
