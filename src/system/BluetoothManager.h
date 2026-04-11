#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <algorithm>

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
    
    void onScanResult(void* device);

private:
    BluetoothManager();
    ~BluetoothManager();
    
    bool m_enabled;
    bool m_scanning;
    std::vector<Device> m_devices;
    std::vector<std::string> m_connected;
    KeyCallback m_keyCallback;
    
    static BluetoothManager* s_instance;
};

#endif