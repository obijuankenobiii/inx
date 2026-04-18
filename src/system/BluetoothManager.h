#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

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

  /** Maps BLE HID keys to HalGPIO button indices (see onHidReport). */
  using BleButtonSink = std::function<void(uint8_t halButtonIndex)>;
  void setBleButtonSink(BleButtonSink sink) { m_bleButtonSink = std::move(sink); }

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
  BleButtonSink m_bleButtonSink;

  static BluetoothManager* s_instance;
};

#endif
