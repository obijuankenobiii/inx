/**
 * @file CalibreConnectActivity.cpp
 * @brief Definitions for CalibreConnectActivity.
 */

#include "CalibreConnectActivity.h"

#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <SDCardManager.h>

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <fcntl.h>
#include <errno.h>

#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"
#include "system/Fonts.h"
#include "WifiSelectionActivity.h"

namespace {

    /**
     * @brief mDNS hostname for device discovery on local network
     */
    constexpr const char* HOSTNAME = "crosspoint";

    /**
     * @brief HTTP port for Calibre wireless connection
     */
    constexpr uint16_t HTTP_PORT = 8080;

    /**
     * @brief Left/right margin for text content
     */
    constexpr int CONTENT_MARGIN = 25;

    /**
     * @brief Vertical spacing between text lines
     */
    constexpr int LINE_SPACING = 28;

    /**
     * @brief Small vertical spacing between elements
     */
    constexpr int SMALL_SPACING = 25;

    /**
     * @brief Large vertical spacing between sections
     */
    constexpr int SECTION_SPACING = 40;

    /**
     * @brief Height of the tab bar at top of screen
     */
    constexpr int TAB_BAR_HEIGHT = 40;

    /**
     * @brief Y offset for header title positioning
     */
    constexpr int HEADER_TITLE_Y_OFFSET = 10;

    /**
     * @brief Y offset for subtitle positioning
     */
    constexpr int SUBTITLE_Y_OFFSET = 40;

    /**
     * @brief Padding above/below divider lines
     */
    constexpr int DIVIDER_PADDING = 10;

    /**
     * @brief Height of bottom area for button hints
     */
    constexpr int BOTTOM_AREA_HEIGHT = 80;

    /**
     * @brief HTML response for Calibre web interface root page
     */
    const char* HTML_HEADER = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html><html><head><title>CrossPoint Reader</title>"
        "<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<style>body{font-family:sans-serif;padding:2rem;text-align:center}</style></head>"
        "<body><h1>CrossPoint Reader</h1><p>Calibre wireless device connection active</p>"
        "<p>Use the Calibre desktop app to send books</p></body></html>";

    /**
     * @brief Response for Calibre device protocol (CDP) endpoint
     */
    const char* CDP_RESPONSE = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 2\r\n"
        "Connection: close\r\n"
        "\r\n"
        "OK";

    /**
     * @brief Renders the header section for the activity
     * @param renderer Graphics renderer instance
     * @param startY Starting Y coordinate
     * @param title Header title text
     * @param subtitle Optional subtitle text
     */
    void renderActivityHeader(const GfxRenderer& renderer, int startY, const char* title, const char* subtitle = nullptr) {
        const int headerHeight = TAB_BAR_HEIGHT;
        
        renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, CONTENT_MARGIN,
                         startY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2 + HEADER_TITLE_Y_OFFSET,
                         title, true, EpdFontFamily::BOLD);
        
        if (subtitle) {
            int subtitleY = startY + SUBTITLE_Y_OFFSET;
            renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, CONTENT_MARGIN, subtitleY, subtitle, true);
            
            int dividerY = subtitleY + renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + DIVIDER_PADDING;
            renderer.line.render(0, dividerY, renderer.getScreenWidth(), dividerY);
        }
    }

    /**
     * @brief Truncates a string to a maximum length, adding ellipsis if needed
     * @param str Input string to truncate
     * @param maxLength Maximum allowed length
     * @return Truncated string with ellipsis if original exceeds maxLength
     */
    std::string truncateString(const std::string& str, int maxLength) {
        if (str.length() <= maxLength) return str;
        std::string result = str;
        result.replace(maxLength - 3, result.length() - (maxLength - 3), "...");
        return result;
    }
}  

/**
 * @brief Web server context structure managing connection state and upload progress
 */
struct WebServerContext {
    int serverSocket = -1;     /**< Server socket file descriptor */
    int clientSocket = -1;     /**< Client socket file descriptor */
    bool running = false;      /**< Flag indicating if server is running */
    
    bool uploadInProgress = false;      /**< Flag indicating if upload is active */
    size_t uploadReceived = 0;          /**< Bytes received so far */
    size_t uploadTotal = 0;             /**< Total bytes expected */
    std::string uploadFilename;         /**< Name of file being uploaded */
    unsigned long lastActivity = 0;     /**< Timestamp of last network activity */
    unsigned long lastCompleteAt = 0;   /**< Timestamp of last completed upload */
    std::string lastCompleteName;       /**< Name of last completed upload */
    
    FsFile uploadFile;                  /**< File handle for writing upload data */
};

/**
 * @brief Destructor - stops web server and cleans up resources
 */
CalibreConnectActivity::~CalibreConnectActivity() {
    stopWebServer();
}

/**
 * @brief Static trampoline function for FreeRTOS task creation
 * @param param Pointer to CalibreConnectActivity instance
 */
void CalibreConnectActivity::taskTrampoline(void* param) {
    auto* self = static_cast<CalibreConnectActivity*>(param);
    self->displayTaskLoop();
}

/**
 * @brief Initializes the activity when entering, sets up WiFi or web server
 */
void CalibreConnectActivity::onEnter() {
    ActivityWithSubactivity::onEnter();

    renderingMutex = xSemaphoreCreateMutex();
    updateRequired = true;
    state = CalibreConnectState::WIFI_SELECTION;
    connectedIP.clear();
    connectedSSID.clear();
    lastHandleClientTime = 0;
    lastProgressReceived = 0;
    lastProgressTotal = 0;
    currentUploadName.clear();
    lastCompleteName.clear();
    lastCompleteAt = 0;
    exitRequested = false;

    xTaskCreate(&CalibreConnectActivity::taskTrampoline, "CalibreConnectTask",
                3072, this, 1, &displayTaskHandle);

    if (WiFi.status() != WL_CONNECTED) {
        enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                         [this](const bool connected) { onWifiSelectionComplete(connected); }));
    } else {
        connectedIP = WiFi.localIP().toString().c_str();
        connectedSSID = WiFi.SSID().c_str();
        startWebServer();
    }
}

/**
 * @brief Cleans up resources when exiting the activity
 */
void CalibreConnectActivity::onExit() {
    ActivityWithSubactivity::onExit();

    stopWebServer();
    MDNS.end();

    delay(50);
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);

    if (renderingMutex) {
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        if (displayTaskHandle) {
            vTaskDelete(displayTaskHandle);
            displayTaskHandle = nullptr;
        }
        vSemaphoreDelete(renderingMutex);
        renderingMutex = nullptr;
    }
}

/**
 * @brief Callback handler for WiFi selection completion
 * @param connected True if WiFi connection successful
 */
void CalibreConnectActivity::onWifiSelectionComplete(const bool connected) {
    if (!connected || WiFi.status() != WL_CONNECTED) {
        exitActivity();
        onComplete();
        return;
    }

    if (subActivity) {
        connectedIP = static_cast<WifiSelectionActivity*>(subActivity.get())->getConnectedIP();
    } else {
        connectedIP = WiFi.localIP().toString().c_str();
    }
    connectedSSID = WiFi.SSID().c_str();
    exitActivity();
    startWebServer();
}

/**
 * @brief Initializes and starts the web server for Calibre wireless connection
 */
void CalibreConnectActivity::startWebServer() {
    state = CalibreConnectState::SERVER_STARTING;
    updateRequired = true;

    if (MDNS.begin(HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        Serial.printf("[CAL] mDNS started: http://%s.local:%d/\n", HOSTNAME, HTTP_PORT);
    }

    serverCtx = new WebServerContext();
    
    serverCtx->serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverCtx->serverSocket < 0) {
        state = CalibreConnectState::ERROR;
        updateRequired = true;
        return;
    }

    int opt = 1;
    setsockopt(serverCtx->serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(HTTP_PORT);
    
    if (bind(serverCtx->serverSocket, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        close(serverCtx->serverSocket);
        serverCtx->serverSocket = -1;
        state = CalibreConnectState::ERROR;
        updateRequired = true;
        return;
    }
    
    if (listen(serverCtx->serverSocket, 5) < 0) {
        close(serverCtx->serverSocket);
        serverCtx->serverSocket = -1;
        state = CalibreConnectState::ERROR;
        updateRequired = true;
        return;
    }
    
    int flags = fcntl(serverCtx->serverSocket, F_GETFL, 0);
    fcntl(serverCtx->serverSocket, F_SETFL, flags | O_NONBLOCK);
    
    serverCtx->running = true;
    serverCtx->lastActivity = millis();
    state = CalibreConnectState::SERVER_RUNNING;
    updateRequired = true;
    
    Serial.printf("[CAL] Web server started on port %d\n", HTTP_PORT);
}

/**
 * @brief Stops the web server and cleans up all related resources
 */
void CalibreConnectActivity::stopWebServer() {
    if (serverCtx) {
        if (serverCtx->clientSocket >= 0) {
            close(serverCtx->clientSocket);
        }
        if (serverCtx->serverSocket >= 0) {
            close(serverCtx->serverSocket);
        }
        if (serverCtx->uploadFile) {
            serverCtx->uploadFile.close();
        }
        delete serverCtx;
        serverCtx = nullptr;
    }
}

/**
 * @brief Main loop processing WiFi connections and HTTP requests
 */
void CalibreConnectActivity::loop() {
    if (subActivity) {
        subActivity->loop();
        return;
    }

    if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
        exitRequested = true;
        return;
    }

    if (serverCtx && serverCtx->running && state == CalibreConnectState::SERVER_RUNNING) {
        esp_task_wdt_reset();
        
        if (serverCtx->clientSocket < 0) {
            struct sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            serverCtx->clientSocket = accept(serverCtx->serverSocket, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
            
            if (serverCtx->clientSocket >= 0) {
                int flags = fcntl(serverCtx->clientSocket, F_GETFL, 0);
                fcntl(serverCtx->clientSocket, F_SETFL, flags | O_NONBLOCK);
                serverCtx->lastActivity = millis();
            }
        }
        
        if (serverCtx->clientSocket >= 0) {
            char buffer[512];
            int bytesRead = recv(serverCtx->clientSocket, buffer, sizeof(buffer) - 1, 0);
            
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                serverCtx->lastActivity = millis();
                
                if (strstr(buffer, "GET / ") || strstr(buffer, "GET /index.html")) {
                    send(serverCtx->clientSocket, HTML_HEADER, strlen(HTML_HEADER), 0);
                    close(serverCtx->clientSocket);
                    serverCtx->clientSocket = -1;
                }
                else if (strstr(buffer, "POST /cdp")) {
                    send(serverCtx->clientSocket, CDP_RESPONSE, strlen(CDP_RESPONSE), 0);
                    close(serverCtx->clientSocket);
                    serverCtx->clientSocket = -1;
                }
                else if (strstr(buffer, "POST /upload")) {
                    const char* filenameStart = strstr(buffer, "filename=\"");
                    if (filenameStart) {
                        filenameStart += 10;
                        const char* filenameEnd = strchr(filenameStart, '"');
                        if (filenameEnd) {
                            serverCtx->uploadFilename = std::string(filenameStart, filenameEnd - filenameStart);
                            serverCtx->uploadInProgress = true;
                            serverCtx->uploadReceived = 0;
                            serverCtx->uploadTotal = 0;
                            
                            std::string savePath = "/" + serverCtx->uploadFilename;
                            if (SdMan.openFileForWrite("CAL", savePath.c_str(), serverCtx->uploadFile)) {
                                Serial.printf("[CAL] Saving upload to: %s\n", savePath.c_str());
                            }
                            
                            const char* dataStart = strstr(buffer, "\r\n\r\n");
                            if (dataStart) {
                                dataStart += 4;
                                size_t headerLen = dataStart - buffer;
                                size_t dataLen = bytesRead - headerLen;
                                
                                if (dataLen > 0 && serverCtx->uploadFile) {
                                    serverCtx->uploadFile.write(reinterpret_cast<const uint8_t*>(dataStart), dataLen);
                                    serverCtx->uploadReceived += dataLen;
                                }
                            }
                            
                            lastProgressReceived = serverCtx->uploadReceived;
                            lastProgressTotal = serverCtx->uploadTotal;
                            currentUploadName = serverCtx->uploadFilename;
                            updateRequired = true;
                        }
                    }
                    
                    const char* response = "HTTP/1.1 200 OK\r\n"
                                          "Content-Type: text/plain\r\n"
                                          "Transfer-Encoding: chunked\r\n"
                                          "\r\n";
                    send(serverCtx->clientSocket, response, strlen(response), 0);
                }
                else {
                    if (serverCtx->uploadInProgress && serverCtx->uploadFile) {
                        serverCtx->uploadFile.write(reinterpret_cast<const uint8_t*>(buffer), bytesRead);
                        serverCtx->uploadReceived += bytesRead;
                        
                        lastProgressReceived = serverCtx->uploadReceived;
                        updateRequired = true;
                        
                        if (bytesRead < static_cast<int>(sizeof(buffer)) || strstr(buffer, "0\r\n\r\n")) {
                            serverCtx->uploadInProgress = false;
                            serverCtx->uploadFile.close();
                            serverCtx->lastCompleteAt = millis();
                            serverCtx->lastCompleteName = serverCtx->uploadFilename;
                            
                            lastCompleteAt = serverCtx->lastCompleteAt;
                            lastCompleteName = serverCtx->lastCompleteName;
                            lastProgressReceived = 0;
                            lastProgressTotal = 0;
                            currentUploadName.clear();
                            updateRequired = true;
                            
                            const char* finalResp = "0\r\n\r\n";
                            send(serverCtx->clientSocket, finalResp, strlen(finalResp), 0);
                            close(serverCtx->clientSocket);
                            serverCtx->clientSocket = -1;
                        }
                    }
                }
            }
            else if (bytesRead == 0 || errno != EAGAIN) {
                if (serverCtx->uploadInProgress && serverCtx->uploadFile) {
                    serverCtx->uploadFile.close();
                }
                close(serverCtx->clientSocket);
                serverCtx->clientSocket = -1;
                serverCtx->uploadInProgress = false;
            }
        }
        
        if (serverCtx->clientSocket >= 0 && millis() - serverCtx->lastActivity > 30000) {
            if (serverCtx->uploadInProgress && serverCtx->uploadFile) {
                serverCtx->uploadFile.close();
            }
            close(serverCtx->clientSocket);
            serverCtx->clientSocket = -1;
            serverCtx->uploadInProgress = false;
        }
        
        lastHandleClientTime = millis();
    }

    if (exitRequested) {
        onComplete();
    }
}

/**
 * @brief Background task loop that handles display updates
 */
void CalibreConnectActivity::displayTaskLoop() {
    while (true) {
        if (updateRequired) {
            updateRequired = false;
            if (renderingMutex && xSemaphoreTake(renderingMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                render();
                xSemaphoreGive(renderingMutex);
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Main rendering function that dispatches to appropriate state renderers
 */
void CalibreConnectActivity::render() const {
    renderer.clearScreen();
    renderTabBar(renderer);
    
    int screenWidth = renderer.getScreenWidth();
    int screenHeight = renderer.getScreenHeight();
    int startY = TAB_BAR_HEIGHT;

    if (state == CalibreConnectState::SERVER_RUNNING) {
        renderServerRunning(screenWidth, screenHeight, startY);
    } else if (state == CalibreConnectState::SERVER_STARTING) {
        renderActivityHeader(renderer, startY, "Connect to Calibre", "Starting server...");
        
        int contentStart = startY + SUBTITLE_Y_OFFSET + 
                          renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 
                          DIVIDER_PADDING;
        int centerY = contentStart + (screenHeight - contentStart - BOTTOM_AREA_HEIGHT) / 2;
        
        renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY, "Please wait...");
        
        auto labels = mappedInput.mapLabels("« Exit", "", "", "");
        renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID,
                                labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else if (state == CalibreConnectState::ERROR) {
        renderActivityHeader(renderer, startY, "Connect to Calibre", "Setup Failed");
        
        int contentStart = startY + SUBTITLE_Y_OFFSET + 
                          renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 
                          DIVIDER_PADDING;
        int centerY = contentStart + (screenHeight - contentStart - BOTTOM_AREA_HEIGHT) / 2;
        
        renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY - 20, "Could not start server");
        renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, centerY + 10, "Press Exit to try again");
        
        auto labels = mappedInput.mapLabels("« Exit", "", "", "");
        renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID,
                                labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }

    renderer.displayBuffer();
}

/**
 * @brief Renders the server running state UI with network info and upload progress
 * @param screenWidth Width of the display
 * @param screenHeight Height of the display
 * @param startY Starting Y coordinate for content
 */
void CalibreConnectActivity::renderServerRunning(int screenWidth, int screenHeight, int startY) const {
    renderActivityHeader(renderer, startY, "Connect to Calibre", "Server Running");
    
    int currentY = startY + SUBTITLE_Y_OFFSET + 
                   renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 
                   DIVIDER_PADDING + SECTION_SPACING - 10;

    renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, CONTENT_MARGIN, currentY, "Network", true, EpdFontFamily::BOLD);
    currentY += LINE_SPACING;
    
    std::string ssidInfo = connectedSSID;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, CONTENT_MARGIN, currentY, 
                     truncateString(ssidInfo, 34).c_str());
    currentY += LINE_SPACING;
    
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, CONTENT_MARGIN, currentY,
                     connectedIP.c_str());
    currentY += LINE_SPACING * 2;

    renderer.line.render(CONTENT_MARGIN, currentY - 10, screenWidth - CONTENT_MARGIN, currentY - 10);
    currentY += SECTION_SPACING;

    renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, CONTENT_MARGIN, currentY, "Setup", true, EpdFontFamily::BOLD);
    currentY += LINE_SPACING;
    
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, CONTENT_MARGIN, currentY,
                     "1.) Install CrossPoint Reader plugin");
    currentY += SMALL_SPACING;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, CONTENT_MARGIN, currentY,
                     "2.) Be on the same WiFi network");
    currentY += SMALL_SPACING;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, CONTENT_MARGIN, currentY,
                     "3.) In Calibre: \"Send to device\"");
    currentY += SMALL_SPACING + 20;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, CONTENT_MARGIN, currentY,
                     "Keep this screen open while sending");
    currentY += SMALL_SPACING * 2;

    renderer.line.render(CONTENT_MARGIN, currentY - 10, screenWidth - CONTENT_MARGIN, currentY - 10);
    currentY += SECTION_SPACING;

    renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, CONTENT_MARGIN, currentY, "Status", true, EpdFontFamily::BOLD);
    currentY += LINE_SPACING;

    if (lastProgressTotal > 0 && lastProgressReceived <= lastProgressTotal) {
        std::string label = "Receiving";
        if (!currentUploadName.empty()) {
            label += ": " + truncateString(currentUploadName, 30);
        }
        renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, CONTENT_MARGIN, currentY, label.c_str());
        
        constexpr int barWidth = 300;
        constexpr int barHeight = 16;
        constexpr int barX = (480 - barWidth) / 2;
        ScreenComponents::drawProgressBar(renderer, barX, currentY + 22, barWidth, barHeight,
                                         lastProgressReceived, lastProgressTotal);
        currentY += 50;
    }

    if (lastCompleteAt > 0 && (millis() - lastCompleteAt) < 6000) {
        std::string msg = "Received: " + truncateString(lastCompleteName, 30);
        renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, CONTENT_MARGIN, currentY, msg.c_str());
    }

    auto labels = mappedInput.mapLabels("« Exit", "", "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID,
                            labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}