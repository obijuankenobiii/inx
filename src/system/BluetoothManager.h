#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <string>
#include <vector>
#include <functional>

class BluetoothManager {
public:
    struct Device {
        std::string address;
        std::string name;
        int rssi;
        bool isHID;
    };

    static BluetoothManager& getInstance();
    
    bool enable();
    bool disable();
    bool isEnabled() { return m_enabled; }
    
    void startScan(uint32_t durationMs);
    void stopScan();
    bool isScanning() { return m_scanning; }
    std::vector<Device> getDiscoveredDevices() { return m_devices; }
    
    bool connectToDevice(const std::string& address);
    void disconnectAll();
    
    void setKeyCallback(std::function<void(uint8_t, bool)> cb) { m_callback = cb; }
    
    // Public for callbacks
    void onScanResult(void* device);
    static BluetoothManager* getInstancePtr() { return s_instance; }

private:
    BluetoothManager();
    ~BluetoothManager();
    
    bool m_enabled;
    bool m_scanning;
    std::vector<Device> m_devices;
    std::vector<std::string> m_connected;
    std::function<void(uint8_t, bool)> m_callback;
    
    static BluetoothManager* s_instance;
};

#endif