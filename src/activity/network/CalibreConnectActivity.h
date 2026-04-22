#pragma once

/**
 * @file CalibreConnectActivity.h
 * @brief Public interface and types for CalibreConnectActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>

#include "activity/ActivityWithSubactivity.h"
#include "activity/Menu.h"

/** Forward declaration of WebServerContext structure */
struct WebServerContext;

/**
 * @brief Possible states for the CalibreConnect activity
 */
enum class CalibreConnectState {
    WIFI_SELECTION,      /**< Waiting for WiFi connection */
    SERVER_STARTING,     /**< Web server is being initialized */
    SERVER_RUNNING,      /**< Web server is actively running */
    ERROR                /**< Error state - server failed to start */
};

/**
 * @brief Activity for Calibre wireless device connection
 * @details Allows sending books from Calibre desktop app via WiFi
 */
class CalibreConnectActivity final : public ActivityWithSubactivity, public Menu {
public:
    /**
     * @brief Constructor for CalibreConnectActivity
     * @param renderer Graphics renderer instance
     * @param mappedInput Input manager for button handling
     * @param onComplete Callback function invoked when activity exits
     */
    explicit CalibreConnectActivity(GfxRenderer& renderer,
                                   MappedInputManager& mappedInput,
                                   const std::function<void()>& onComplete)
        : ActivityWithSubactivity("CalibreConnect", renderer, mappedInput),
          Menu(),
          onComplete(onComplete) {
        tabSelectorIndex = 3;  /**< Select Sync tab (index 3) by default */
    }

    /** Destructor - cleans up web server resources */
    ~CalibreConnectActivity();

    /**
     * @brief Called when activity becomes active
     * @details Initializes WiFi or starts web server
     */
    void onEnter() override;

    /**
     * @brief Called when activity is exited
     * @details Stops web server and cleans up resources
     */
    void onExit() override;

    /** Main loop - processes network connections and HTTP requests */
    void loop() override;

    /**
     * @brief Skip delay when server is running for responsive network handling
     * @return True when server is running
     */
    bool skipLoopDelay() override { return state == CalibreConnectState::SERVER_RUNNING; }

    /**
     * @brief Prevent auto sleep when server is running
     * @return True when server is running
     */
    bool preventAutoSleep() override { return state == CalibreConnectState::SERVER_RUNNING; }

private:
    /**
     * @brief Static trampoline function for FreeRTOS task
     * @param param Pointer to CalibreConnectActivity instance
     */
    static void taskTrampoline(void* param);

    /**
     * @brief Background task loop for display updates
     * @details Never returns - [[noreturn]] attribute
     */
    [[noreturn]] void displayTaskLoop();

    /** Main rendering function - dispatches to state-specific renderers */
    void render() const;

    /**
     * @brief Renders the server running state UI
     * @param screenWidth Width of the display
     * @param screenHeight Height of the display
     * @param startY Starting Y coordinate for content
     */
    void renderServerRunning(int screenWidth, int screenHeight, int startY) const;

    /**
     * @brief Callback for WiFi selection completion
     * @param connected True if WiFi connection successful
     */
    void onWifiSelectionComplete(bool connected);

    /** Initializes and starts the web server */
    void startWebServer();

    /** Stops the web server and cleans up resources */
    void stopWebServer();

    /** Navigate to selected menu tab (not used in this activity) */
    void navigateToSelectedMenu() override {}

    TaskHandle_t displayTaskHandle = nullptr;      /**< Handle for display update task */
    SemaphoreHandle_t renderingMutex = nullptr;    /**< Mutex for thread-safe rendering */
    bool updateRequired = false;                   /**< Flag indicating render update needed */
    bool exitRequested = false;                    /**< Flag indicating exit was requested */

    CalibreConnectState state = CalibreConnectState::WIFI_SELECTION;  /**< Current activity state */
    std::string connectedIP;                       /**< IP address of connected WiFi */
    std::string connectedSSID;                     /**< SSID of connected WiFi network */

    WebServerContext* serverCtx = nullptr;         /**< Web server context (raw pointer) */
    unsigned long lastHandleClientTime = 0;        /**< Timestamp of last client handling */

    size_t lastProgressReceived = 0;               /**< Last reported bytes received for upload */
    size_t lastProgressTotal = 0;                  /**< Last reported total bytes for upload */
    std::string currentUploadName;                 /**< Name of file currently being uploaded */
    std::string lastCompleteName;                  /**< Name of last completed upload */
    unsigned long lastCompleteAt = 0;              /**< Timestamp of last completed upload */

    const std::function<void()> onComplete;        /**< Callback invoked on activity exit */
};