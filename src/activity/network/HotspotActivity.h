#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <memory>
#include <string>

#include "activity/Activity.h"
#include "activity/Menu.h"
#include "network/LocalServer.h"

/**
 * @brief Represents the operational states of the hotspot activity
 */
enum class HotspotState {
    STARTING,        /**< Access point and server initialization in progress */
    RUNNING,         /**< Hotspot active and accepting connections */
    ERROR            /**< Error state, unable to proceed */
};

/**
 * @brief Activity that creates a WiFi hotspot for device configuration and file transfer
 * 
 * This activity sets up the ESP32 as a WiFi access point and starts a web server
 * for device configuration, file management, and Calibre wireless transfers.
 * It displays connection information including a QR code for easy access.
 */
class HotspotActivity final : public Activity, public Menu {
public:
    /**
     * @brief Constructs a new HotspotActivity
     * @param renderer Graphics renderer for display output
     * @param mappedInput Input manager for handling user interactions
     * @param onGoBack Callback function invoked when user requests to go back
     */
    explicit HotspotActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::function<void()>& onGoBack)
        : Activity("Hotspot", renderer, mappedInput), 
          Menu(),
          onGoBack(onGoBack) {
        tabSelectorIndex = 3;
    }
    
    void onEnter() override;
    void onExit() override;
    void loop() override;
    
    /**
     * @brief Determines if loop delay should be skipped
     * @return true when web server is running to maintain responsiveness
     */
    bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
    
    /**
     * @brief Prevents auto sleep during active connections
     * @return true when web server is running to maintain connection
     */
    bool preventAutoSleep() override { return webServer && webServer->isRunning(); }

private:
    static void taskTrampoline(void* param);
    [[noreturn]] void displayTaskLoop();
    void render() const;
    void renderServerRunning() const;
    void startAccessPoint();
    void startWebServer();
    void stopWebServer();
    void drawQRCode(int x, int y, const std::string& data) const;
    void navigateToSelectedMenu() override {}

    TaskHandle_t displayTaskHandle;
    SemaphoreHandle_t renderingMutex;
    bool updateRequired;
    HotspotState state;

    std::unique_ptr<LocalServer> webServer;
    std::string connectedIP;
    std::string connectedSSID;
    unsigned long lastHandleClientTime;

    const std::function<void()> onGoBack;
};