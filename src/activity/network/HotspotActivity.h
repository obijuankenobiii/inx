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
          displayTaskHandle(nullptr),
          renderingMutex(nullptr),
          updateRequired(false),
          state(HotspotState::STARTING),
          lastHandleClientTime(0),
          onGoBack(onGoBack) {
        tabSelectorIndex = 3;
    }
    
    /**
     * @brief Called when activity becomes active
     * @details Initializes hotspot and web server
     */
    void onEnter() override;
    
    /**
     * @brief Called when activity is exited
     * @details Stops web server and cleans up resources
     */
    void onExit() override;
    
    /** @brief Main loop - processes network connections */
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
    /**
     * @brief Static trampoline function for FreeRTOS task
     * @param param Pointer to HotspotActivity instance
     */
    static void taskTrampoline(void* param);
    
    /**
     * @brief Background task loop for display updates
     * @details Never returns - [[noreturn]] attribute
     */
    [[noreturn]] void displayTaskLoop();
    
    /** @brief Main rendering function */
    void render() const;
    
    /** @brief Renders the server running state UI */
    void renderServerRunning() const;
    
    /** @brief Initializes and starts the WiFi access point */
    void startAccessPoint();
    
    /** @brief Initializes and starts the web server */
    void startWebServer();
    
    /** @brief Stops the web server and cleans up resources */
    void stopWebServer();
    
    /**
     * @brief Draws a QR code on the display
     * @param x X-coordinate for QR code position
     * @param y Y-coordinate for QR code position
     * @param data String data to encode in QR code
     */
    void drawQRCode(int x, int y, const std::string& data) const;
    
    /** @brief Navigate to selected menu tab (not used in this activity) */
    void navigateToSelectedMenu() override {}

    TaskHandle_t displayTaskHandle;           /**< Handle for display update task */
    SemaphoreHandle_t renderingMutex;         /**< Mutex for thread-safe rendering */
    bool updateRequired;                      /**< Flag indicating render update needed */
    HotspotState state;                       /**< Current activity state */

    std::unique_ptr<LocalServer> webServer;   /**< Web server instance */
    std::string connectedIP;                  /**< IP address of the access point */
    std::string connectedSSID;                /**< SSID of the access point */
    unsigned long lastHandleClientTime;       /**< Timestamp of last client handling */

    const std::function<void()> onGoBack;     /**< Callback invoked when going back */
};