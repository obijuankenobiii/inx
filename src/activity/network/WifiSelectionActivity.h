#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "activity/Menu.h"

/**
 * @struct WifiNetworkInfo
 * @brief Structure containing information about a detected WiFi network
 */
struct WifiNetworkInfo {
  std::string ssid;           ///< Network name (SSID)
  int32_t rssi;               ///< Signal strength in dBm
  bool isEncrypted;           ///< Whether the network requires a password
  bool hasSavedPassword;      ///< Whether saved credentials exist for this network
};

/**
 * @enum WifiSelectionState
 * @brief Possible states of the WiFi selection process
 */
enum class WifiSelectionState {
  SCANNING,           ///< Actively scanning for available networks
  NETWORK_LIST,       ///< Displaying list of found networks
  PASSWORD_ENTRY,     ///< Password entry screen for encrypted network
  CONNECTING,         ///< Attempting to establish connection
  CONNECTED,          ///< Successfully connected to network
  SAVE_PROMPT,        ///< Asking user whether to save the password
  CONNECTION_FAILED,  ///< Connection attempt failed
  FORGET_PROMPT       ///< Asking user whether to forget a saved network
};

/**
 * @class WifiSelectionActivity
 * @brief Activity for scanning and connecting to WiFi networks
 * 
 * Handles the complete WiFi connection flow including:
 * - Scanning for available networks
 * - Displaying sorted list of networks with signal strength
 * - Password entry for encrypted networks
 * - Connection attempt with timeout
 * - Password saving prompt
 * - Option to forget saved networks on failure
 * 
 * The activity calls onComplete with true if connected successfully,
 * false if cancelled or connection failed.
 */
class WifiSelectionActivity final : public ActivityWithSubactivity, public Menu {
public:
    /**
     * @brief Constructs a new WifiSelectionActivity
     * @param renderer Reference to the graphics renderer
     * @param mappedInput Reference to the input manager
     * @param onComplete Callback when connection process completes (true=connected, false=cancelled/failed)
     * @param onTabChange Optional callback for tab navigation changes
     */
    explicit WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::function<void(bool connected)>& onComplete,
                                   const std::function<void(int)>& onTabChange = nullptr)
        : ActivityWithSubactivity("WifiSelection", renderer, mappedInput), 
          Menu(),
          onComplete(onComplete),
          onTabChange(onTabChange) {
            tabSelectorIndex = 3;
    }
    
    /**
     * @brief Called when entering the activity
     */
    void onEnter() override;
    
    /**
     * @brief Called when exiting the activity
     */
    void onExit() override;
    
    /**
     * @brief Main loop function called repeatedly while activity is active
     */
    void loop() override;

    /**
     * @brief Gets the IP address after successful connection
     * @return IP address string, empty if not connected
     */
    const std::string& getConnectedIP() const { return connectedIP; }

private:
    TaskHandle_t displayTaskHandle = nullptr;           ///< Handle for display task
    SemaphoreHandle_t renderingMutex = nullptr;          ///< Mutex for rendering synchronization
    bool updateRequired = false;                          ///< Flag indicating screen needs update
    WifiSelectionState state = WifiSelectionState::SCANNING; ///< Current activity state
    int selectedNetworkIndex = 0;                         ///< Currently selected network index
    std::vector<WifiNetworkInfo> networks;                ///< List of found networks
    const std::function<void(bool connected)> onComplete; ///< Connection completion callback
    std::function<void(int)> onTabChange;                 ///< Tab navigation callback

    std::string selectedSSID;                             ///< SSID of selected network
    bool selectedRequiresPassword = false;                 ///< Whether selected network requires password

    std::string connectedIP;                               ///< IP address after successful connection
    std::string connectionError;                           ///< Error message if connection failed

    std::string enteredPassword;                           ///< Password entered by user or from saved credentials
    std::string cachedMacAddress;                          ///< Cached MAC address for display

    bool usedSavedPassword = false;                        ///< Whether saved password was used (skip save prompt)

    int savePromptSelection = 0;                           ///< Save prompt selection (0=Yes, 1=No)
    int forgetPromptSelection = 0;                         ///< Forget prompt selection (0=Cancel, 1=Forget)

    static constexpr unsigned long CONNECTION_TIMEOUT_MS = 15000; ///< Connection attempt timeout
    unsigned long connectionStartTime = 0;                 ///< Start time of current connection attempt

    /**
     * @brief Static trampoline function for the display task
     * @param param Pointer to the WifiSelectionActivity instance
     */
    static void taskTrampoline(void* param);
    
    /**
     * @brief Main display task loop running on separate thread
     */
    [[noreturn]] void displayTaskLoop();
    
    /**
     * @brief Renders the current screen content
     */
    void render() const;
    
    /**
     * @brief Renders the scanning state screen
     * @param screenWidth Width of the screen
     * @param screenHeight Height of the screen
     * @param startY Starting Y coordinate for content
     */
    void renderScanning(int screenWidth, int screenHeight, int startY) const;
    
    /**
     * @brief Renders the network list screen
     * @param screenWidth Width of the screen
     * @param screenHeight Height of the screen
     * @param startY Starting Y coordinate for content
     */
    void renderNetworkList(int screenWidth, int screenHeight, int startY) const;
    
    /**
     * @brief Renders the connecting state screen
     * @param screenWidth Width of the screen
     * @param screenHeight Height of the screen
     * @param startY Starting Y coordinate for content
     */
    void renderConnecting(int screenWidth, int screenHeight, int startY) const;
    
    /**
     * @brief Renders the save password prompt screen
     * @param screenWidth Width of the screen
     * @param screenHeight Height of the screen
     * @param startY Starting Y coordinate for content
     */
    void renderSavePrompt(int screenWidth, int screenHeight, int startY) const;
    
    /**
     * @brief Renders the connection failed screen
     * @param screenWidth Width of the screen
     * @param screenHeight Height of the screen
     * @param startY Starting Y coordinate for content
     */
    void renderConnectionFailed(int screenWidth, int screenHeight, int startY) const;
    
    /**
     * @brief Renders the forget network prompt screen
     * @param screenWidth Width of the screen
     * @param screenHeight Height of the screen
     * @param startY Starting Y coordinate for content
     */
    void renderForgetPrompt(int screenWidth, int screenHeight, int startY) const;

    /**
     * @brief Starts an asynchronous WiFi network scan
     */
    void startWifiScan();
    
    /**
     * @brief Processes the results of a WiFi scan
     */
    void processWifiScanResults();
    
    /**
     * @brief Selects a network from the list to connect to
     * @param index Index of the network to select
     */
    void selectNetwork(int index);
    
    /**
     * @brief Attempts to connect to the selected network
     */
    void attemptConnection();
    
    /**
     * @brief Checks the status of an ongoing connection attempt
     */
    void checkConnectionStatus();
    
    /**
     * @brief Draws a WiFi signal strength icon
     * @param x X coordinate for the icon
     * @param y Y coordinate for the icon
     * @param rssi Signal strength in dBm
     * @param isSelected Whether the current item is selected
     */
    void drawWifiIcon(int x, int y, int32_t rssi, bool isSelected) const;

    /**
     * @brief Navigates to the selected menu (required by Menu interface)
     */
    void navigateToSelectedMenu() override {}
};