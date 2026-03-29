#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>

#include "activity/ActivityWithSubactivity.h"
#include "activity/Menu.h"

// Forward declaration
struct WebServerContext;

enum class CalibreConnectState { 
    WIFI_SELECTION, 
    SERVER_STARTING, 
    SERVER_RUNNING, 
    ERROR 
};

class CalibreConnectActivity final : public ActivityWithSubactivity, public Menu {
public:
    explicit CalibreConnectActivity(GfxRenderer& renderer, 
                                   MappedInputManager& mappedInput,
                                   const std::function<void()>& onComplete)
        : ActivityWithSubactivity("CalibreConnect", renderer, mappedInput), 
          Menu(),
          onComplete(onComplete) {
        tabSelectorIndex = 3;
    }
    
    ~CalibreConnectActivity();
    
    void onEnter() override;
    void onExit() override;
    void loop() override;
    bool skipLoopDelay() override { return state == CalibreConnectState::SERVER_RUNNING; }
    bool preventAutoSleep() override { return state == CalibreConnectState::SERVER_RUNNING; }

private:
    static void taskTrampoline(void* param);
    [[noreturn]] void displayTaskLoop();
    void render() const;
    void renderServerRunning(int screenWidth, int screenHeight, int startY) const;
    void onWifiSelectionComplete(bool connected);
    void startWebServer();
    void stopWebServer();
    void navigateToSelectedMenu() override {}

    TaskHandle_t displayTaskHandle = nullptr;
    SemaphoreHandle_t renderingMutex = nullptr;
    bool updateRequired = false;
    bool exitRequested = false;

    CalibreConnectState state = CalibreConnectState::WIFI_SELECTION;
    std::string connectedIP;
    std::string connectedSSID;

    // Use raw pointer instead of unique_ptr
    WebServerContext* serverCtx = nullptr;
    unsigned long lastHandleClientTime = 0;

    size_t lastProgressReceived = 0;
    size_t lastProgressTotal = 0;
    std::string currentUploadName;
    std::string lastCompleteName;
    unsigned long lastCompleteAt = 0;

    const std::function<void()> onComplete;
};