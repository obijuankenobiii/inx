#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <algorithm>
#include <atomic>
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
    /** NimBLE peer address type (`BLE_ADDR_PUBLIC`=0, `BLE_ADDR_RANDOM`=1, …). */
    uint8_t addrType = 0;
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

  /** @param hintAddrType NimBLE address type, or 0xFF to resolve from last scan / ble_devices.bin (else random). */
  bool connectToDevice(const std::string& address, uint8_t hintAddrType = 0xFF);

  /** If a synchronous connect() is in progress, ask the controller to cancel it (safe from another task). */
  void cancelPendingConnect();

  void disconnectAll();
  bool isConnected(const std::string& address) const;
  std::vector<std::string> getConnectedDevices() const { return m_connected; }

  using KeyCallback = std::function<void(uint8_t keycode, bool pressed)>;
  void setKeyCallback(KeyCallback cb) { m_keyCallback = cb; }

  /** BLE HID injects into the same HalGPIO path as physical keys (set once at startup). */
  void setHalGpio(HalGPIO* gpio) { m_halGpio = gpio; }

  /** True after reader enables "Use pageturner" and link is up (or was taken over from drawer). */
  bool readerPageTurnerSession() const { return m_readerPageTurnerSession; }
  void setReaderPageTurnerSession(bool active) { m_readerPageTurnerSession = active; }

  /** FreeRTOS task: enable BLE if needed, connect SETTINGS.bleSavedAddress, set reader session on success. */
  void startReaderPageTurnerConnectTask();

  /** Reader settings drawer button: connect saved BLE keyboard and mark reader session (closes drawer). */
  void activateReaderPageTurnerFromBookDrawer();

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
  bool m_readerPageTurnerSession = false;

  /** Set while connect() is blocked so cancelPendingConnect can call ble_gap_conn_cancel. */
  std::atomic<void*> m_pendingConnectClient{nullptr};

  static BluetoothManager* s_instance;
};

#endif
