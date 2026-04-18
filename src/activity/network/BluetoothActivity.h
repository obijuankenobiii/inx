#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "activity/Menu.h"

class BluetoothManager;

/**
 * @brief Represents the operational states of the Bluetooth activity
 */
enum class BluetoothState {
    SCANNING,           /**< Scanning for Bluetooth devices */
    DEVICE_LIST,        /**< Showing list of discovered devices */
    CONNECTING,         /**< Connecting to selected device */
    CONNECTED,          /**< Connected to device */
    CONNECTION_FAILED,  /**< Connection attempt failed */
    KEY_MAP_PREV,       /**< Choose HID code for reader page back */
    KEY_MAP_NEXT        /**< Choose HID code for reader page forward */
};

/**
 * @brief Activity for connecting Bluetooth keyboards
 */
class BluetoothActivity final : public ActivityWithSubactivity, public Menu {
public:
    /**
     * @brief Constructs a new BluetoothActivity
     * @param renderer Graphics renderer for display output
     * @param mappedInput Input manager for handling user interactions
     * @param onGoBack Callback function invoked when user requests to go back
     */
    explicit BluetoothActivity(GfxRenderer& renderer, 
                               MappedInputManager& mappedInput,
                               const std::function<void()>& onGoBack)
        : ActivityWithSubactivity("Bluetooth", renderer, mappedInput), 
          Menu(),
          btManager(nullptr),
          displayTaskHandle(nullptr),
          renderingMutex(nullptr),
          updateRequired(false),
          state(BluetoothState::SCANNING),
          selectedIndex(0),
          scanStartTime(0),
          onGoBack(onGoBack) {
        tabSelectorIndex = 3;
    }
    
    /**
     * @brief Called when activity becomes active
     */
    void onEnter() override;
    
    /**
     * @brief Called when activity is exited
     */
    void onExit() override;
    
    /** @brief Main loop - processes Bluetooth connections */
    void loop() override;

private:
    struct DeviceInfo {
        std::string name;
        std::string address;
        int rssi;
    };

    /**
     * @brief Static trampoline function for FreeRTOS task
     * @param param Pointer to BluetoothActivity instance
     */
    static void taskTrampoline(void* param);
    
    /**
     * @brief Background task loop for display updates
     */
    void displayTaskLoop();
    
    /** @brief Main rendering function */
    void render() const;
    
    /** @brief Starts Bluetooth device scan */
    void startScan();
    
    /** @brief Processes scan results */
    void processScanResults();
    
    /** @brief Connects to selected device */
    void connectToDevice(int index);
    
    /** @brief Renders scanning state */
    void renderScanning() const;
    
    /** @brief Renders device list state */
    void renderDeviceList() const;
    
    /** @brief Renders connecting state */
    void renderConnecting() const;
    
    /** @brief Renders connected state */
    void renderConnected() const;
    
    /** @brief Renders connection failed state */
    void renderConnectionFailed() const;

    void renderKeyMapPrev() const;
    void renderKeyMapNext() const;
    
    /** @brief Draws Bluetooth signal strength icon */
    void drawBluetoothIcon(int x, int y, int rssi, bool isSelected) const;
    
    /** @brief Navigate to selected menu tab (not used) */
    void navigateToSelectedMenu() override {}

    BluetoothManager* btManager;

    TaskHandle_t displayTaskHandle;
    SemaphoreHandle_t renderingMutex;
    bool updateRequired;
    BluetoothState state;
    int selectedIndex;
    std::vector<DeviceInfo> devices;
    std::string connectionError;
    unsigned long scanStartTime;
    int keyMapPresetIndex = 0;

    const std::function<void()> onGoBack;
};