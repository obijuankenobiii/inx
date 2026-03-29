#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <memory>
#include <string>

#include "activity/ActivityWithSubactivity.h"
#include "activity/Menu.h"
#include "network/LocalServer.h"
#include "WifiSelectionActivity.h"

/**
 * @brief Represents the operational states of the local network activity
 */
enum class LocalNetworkState {
    WIFI_SELECTION,  /**< Waiting for WiFi network selection */
    SERVER_STARTING, /**< Web server initialization in progress */
    SERVER_RUNNING,  /**< Web server active and accepting connections */
    ERROR            /**< Error state, unable to proceed */
};

/**
 * @brief Activity that enables file transfer over local WiFi network
 * 
 * This activity connects to an existing WiFi network and starts a web server
 * for file transfers. It handles WiFi connection setup, server initialization,
 * and provides connection information to users.
 */
class LocalNetworkActivity final : public ActivityWithSubactivity, public Menu {
public:
    /**
     * @brief Constructs a new LocalNetworkActivity
     * @param renderer Graphics renderer for display output
     * @param mappedInput Input manager for handling user interactions
     * @param onGoBack Callback function invoked when user requests to go back
     */
    explicit LocalNetworkActivity(GfxRenderer& renderer, 
                                 MappedInputManager& mappedInput,
                                 const std::function<void()>& onGoBack)
        : ActivityWithSubactivity("LocalNetwork", renderer, mappedInput), 
          Menu(),
          onGoBack(onGoBack) {
        tabSelectorIndex = 3;
    }
    
    void onEnter() override;
    void onExit() override;
    void loop() override;
    
    /**
     * @brief Determines if loop delay should be skipped
     * @return true when server is running to maintain responsiveness
     */
    bool skipLoopDelay() override { return state == LocalNetworkState::SERVER_RUNNING; }
    
    /**
     * @brief Prevents auto sleep during active connections
     * @return true when server is running to maintain connection
     */
    bool preventAutoSleep() override { return state == LocalNetworkState::SERVER_RUNNING; }

private:
    static void taskTrampoline(void* param);
    [[noreturn]] void displayTaskLoop();
    void render() const;
    void renderServerRunning() const;
    void onWifiSelectionComplete(bool connected);
    void startWebServer();
    void stopWebServer();
    void navigateToSelectedMenu() override {}

    TaskHandle_t displayTaskHandle;
    SemaphoreHandle_t renderingMutex;
    bool updateRequired;
    LocalNetworkState state;

    std::string connectedIP;
    std::string connectedSSID;
    std::unique_ptr<LocalServer> webServer;
    unsigned long lastHandleClientTime;

    const std::function<void()> onGoBack;
};