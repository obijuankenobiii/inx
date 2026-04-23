#pragma once

/**
 * @file LocalServer.h
 * @brief Public interface and types for LocalServer.
 */

#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiUdp.h>

#include <memory>
#include <string>
#include <vector>


struct FileInfo {
  String name;
  size_t size;
  bool isEpub;
  bool isDirectory;
};

class LocalServer {
 public:
  struct WsUploadStatus {
    bool inProgress = false;
    size_t received = 0;
    size_t total = 0;
    std::string filename;
    std::string lastCompleteName;
    size_t lastCompleteSize = 0;
    unsigned long lastCompleteAt = 0;
  };

  LocalServer();
  ~LocalServer();

  
  void begin();

  
  void stop();

  
  void handleClient();

  
  bool isRunning() const { return running; }

  WsUploadStatus getWsUploadStatus() const;

  
  uint16_t getPort() const { return port; }

 private:
  std::unique_ptr<WebServer> server = nullptr;
  std::unique_ptr<WebSocketsServer> wsServer = nullptr;
  bool running = false;
  bool apMode = false;  
  uint16_t port = 80;
  uint16_t wsPort = 81;  
  WiFiUDP udp;
  bool udpActive = false;

  
  void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  static void wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length);

  
  void scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const;
  String formatFileSize(size_t bytes) const;
  bool isEpubFile(const String& filename) const;

  
  void handleRoot() const;
  void handleFontManagerPage() const;
  void handleNotFound() const;
  void handleStatus() const;
  void handleFileList() const;
  void handleFileListData() const;
  void handleDownload() const;
  void handleUpload() const;
  void handleUploadPost() const;
  void handleCreateFolder() const;
  void handleDelete() const;

  
  void handleSettingsPage() const;
  void handleSettingsGet() const;
  void handleSettingsUpdate() const;

  void handleWifiGet() const;
  void handleWifiPost() const;
  void handleWifiDelete() const;
  void handleKOReaderGet() const;
  void handleKOReaderPost() const;

  void handleFontsRescan() const;
};