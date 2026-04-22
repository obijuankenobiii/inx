/**
 * @file LocalServer.cpp
 * @brief Definitions for LocalServer.
 */

#include "LocalServer.h"

#include <ArduinoJson.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <SDCardManager.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include <algorithm>

#include "html/FilesPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/SettingsPageHtml.generated.h"
#include "util/StringUtils.h"
#include "state/SystemSetting.h"
#include "KOReaderCredentialStore.h"
#include "state/NetworkCredential.h"

namespace {


const char* HIDDEN_ITEMS[] = {"System Volume Information", ".metadata"};
constexpr size_t HIDDEN_ITEMS_COUNT = sizeof(HIDDEN_ITEMS) / sizeof(HIDDEN_ITEMS[0]);
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;


LocalServer* wsInstance = nullptr;


FsFile wsUploadFile;
String wsUploadFileName;
String wsUploadPath;
size_t wsUploadSize = 0;
size_t wsUploadReceived = 0;
unsigned long wsUploadStartTime = 0;
bool wsUploadInProgress = false;
String wsLastCompleteName;
size_t wsLastCompleteSize = 0;
unsigned long wsLastCompleteAt = 0;


void clearEpubCacheIfNeeded(const String& filePath) {
  
  if (StringUtils::checkFileExtension(filePath, ".epub")) {
    Epub(filePath.c_str(), "/.metadata").clearCache();
    Serial.printf("[%lu] [WEB] Cleared epub cache for: %s\n", millis(), filePath.c_str());
  }
}
}  





LocalServer::LocalServer() {}

LocalServer::~LocalServer() { stop(); }

void LocalServer::begin() {
  if (running) {
    Serial.printf("[%lu] [WEB] Web server already running\n", millis());
    return;
  }

  
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);  

  if (!isStaConnected && !isInApMode) {
    Serial.printf("[%lu] [WEB] Cannot start webserver - no valid network (mode=%d, status=%d)\n", millis(), wifiMode,
                  WiFi.status());
    return;
  }

  
  apMode = isInApMode;

  Serial.printf("[%lu] [WEB] [MEM] Free heap before begin: %d bytes\n", millis(), ESP.getFreeHeap());
  Serial.printf("[%lu] [WEB] Network mode: %s\n", millis(), apMode ? "AP" : "STA");

  Serial.printf("[%lu] [WEB] Creating web server on port %d...\n", millis(), port);
  server.reset(new WebServer(port));

  
  
  WiFi.setSleep(false);

  
  

  Serial.printf("[%lu] [WEB] [MEM] Free heap after WebServer allocation: %d bytes\n", millis(), ESP.getFreeHeap());

  if (!server) {
    Serial.printf("[%lu] [WEB] Failed to create WebServer!\n", millis());
    return;
  }

  
  Serial.printf("[%lu] [WEB] Setting up routes...\n", millis());
  server->on("/", HTTP_GET, [this] { handleRoot(); });
  server->on("/files", HTTP_GET, [this] { handleFileList(); });

  server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });
  server->on("/download", HTTP_GET, [this] { handleDownload(); });

  
  server->on("/upload", HTTP_POST, [this] { handleUploadPost(); }, [this] { handleUpload(); });

  
  server->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });

  
  server->on("/delete", HTTP_POST, [this] { handleDelete(); });


  server->on("/settings", HTTP_GET, [this] { handleSettingsPage(); });
  server->on("/api/settings", HTTP_GET, [this] { handleSettingsGet(); });
  server->on("/api/settings", HTTP_POST, [this] { handleSettingsUpdate(); });

  server->on("/api/wifi", HTTP_GET, [this] { handleWifiGet(); });
  server->on("/api/wifi", HTTP_POST, [this] { handleWifiPost(); });
  server->on("/api/wifi/*", HTTP_DELETE, [this] { handleWifiDelete(); });
  server->on("/api/koreader", HTTP_GET, [this] { handleKOReaderGet(); });
  server->on("/api/koreader", HTTP_POST, [this] { handleKOReaderPost(); });

  server->onNotFound([this] { handleNotFound(); });
  Serial.printf("[%lu] [WEB] [MEM] Free heap after route setup: %d bytes\n", millis(), ESP.getFreeHeap());
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
  } else {
    if (SPIFFS.exists("/js/jszip.min.js")) {
      Serial.println("✓ jszip.min.js found in SPIFFS!");
      File f = SPIFFS.open("/js/jszip.min.js", "r");
      Serial.printf("  Size: %d bytes\n", f.size());
      f.close();
      server->serveStatic("/js", SPIFFS, "/js");
    }

  }
  
  server->begin();

  
  Serial.printf("[%lu] [WEB] Starting WebSocket server on port %d...\n", millis(), wsPort);
  wsServer.reset(new WebSocketsServer(wsPort));
  wsInstance = const_cast<LocalServer*>(this);
  wsServer->begin();
  wsServer->onEvent(wsEventCallback);
  Serial.printf("[%lu] [WEB] WebSocket server started\n", millis());

  udpActive = udp.begin(LOCAL_UDP_PORT);
  Serial.printf("[%lu] [WEB] Discovery UDP %s on port %d\n", millis(), udpActive ? "enabled" : "failed",
                LOCAL_UDP_PORT);

  running = true;

  Serial.printf("[%lu] [WEB] Web server started on port %d\n", millis(), port);
  
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  Serial.printf("[%lu] [WEB] Access at http://%s/\n", millis(), ipAddr.c_str());
  Serial.printf("[%lu] [WEB] WebSocket at ws://%s:%d/\n", millis(), ipAddr.c_str(), wsPort);
  Serial.printf("[%lu] [WEB] [MEM] Free heap after server.begin(): %d bytes\n", millis(), ESP.getFreeHeap());
}

void LocalServer::stop() {
  if (!running || !server) {
    Serial.printf("[%lu] [WEB] stop() called but already stopped (running=%d, server=%p)\n", millis(), running,
                  server.get());
    return;
  }

  Serial.printf("[%lu] [WEB] STOP INITIATED - setting running=false first\n", millis());
  running = false;  

  Serial.printf("[%lu] [WEB] [MEM] Free heap before stop: %d bytes\n", millis(), ESP.getFreeHeap());

  
  if (wsUploadInProgress && wsUploadFile) {
    wsUploadFile.close();
    wsUploadInProgress = false;
  }

  
  if (wsServer) {
    Serial.printf("[%lu] [WEB] Stopping WebSocket server...\n", millis());
    wsServer->close();
    wsServer.reset();
    wsInstance = nullptr;
    Serial.printf("[%lu] [WEB] WebSocket server stopped\n", millis());
  }

  if (udpActive) {
    udp.stop();
    udpActive = false;
  }

  
  delay(20);

  server->stop();
  Serial.printf("[%lu] [WEB] [MEM] Free heap after server->stop(): %d bytes\n", millis(), ESP.getFreeHeap());

  
  delay(10);

  server.reset();
  Serial.printf("[%lu] [WEB] Web server stopped and deleted\n", millis());
  Serial.printf("[%lu] [WEB] [MEM] Free heap after delete server: %d bytes\n", millis(), ESP.getFreeHeap());

  
  
  Serial.printf("[%lu] [WEB] [MEM] Free heap final: %d bytes\n", millis(), ESP.getFreeHeap());
}

void LocalServer::handleClient() {
  static unsigned long lastDebugPrint = 0;

  
  if (!running) {
    return;
  }

  
  if (!server) {
    Serial.printf("[%lu] [WEB] WARNING: handleClient called with null server!\n", millis());
    return;
  }

  
  if (millis() - lastDebugPrint > 10000) {
    Serial.printf("[%lu] [WEB] handleClient active, server running on port %d\n", millis(), port);
    lastDebugPrint = millis();
  }

  server->handleClient();

  
  if (wsServer) {
    wsServer->loop();
  }

  
  if (udpActive) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buffer[16];
      int len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "hello") == 0) {
          String hostname = WiFi.getHostname();
          if (hostname.isEmpty()) {
            hostname = "crosspoint";
          }
          String message = "crosspoint (on " + hostname + ");" + String(wsPort);
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
          udp.endPacket();
        }
      }
    }
  }
}

LocalServer::WsUploadStatus LocalServer::getWsUploadStatus() const {
  WsUploadStatus status;
  status.inProgress = wsUploadInProgress;
  status.received = wsUploadReceived;
  status.total = wsUploadSize;
  status.filename = wsUploadFileName.c_str();
  status.lastCompleteName = wsLastCompleteName.c_str();
  status.lastCompleteSize = wsLastCompleteSize;
  status.lastCompleteAt = wsLastCompleteAt;
  return status;
}

void LocalServer::handleRoot() const {
  server->send(200, "text/html", HomePageHtml);
  Serial.printf("[%lu] [WEB] Served root page\n", millis());
}

void LocalServer::handleNotFound() const {
  String message = "404 Not Found\n\n";
  message += "URI: " + server->uri() + "\n";
  server->send(404, "text/plain", message);
}

void LocalServer::handleStatus() const {
  
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  JsonDocument doc;
  doc["version"] = INX_VERSION;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void LocalServer::scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const {
  FsFile root = SdMan.open(path);
  if (!root) {
    Serial.printf("[%lu] [WEB] Failed to open directory: %s\n", millis(), path);
    return;
  }

  if (!root.isDirectory()) {
    Serial.printf("[%lu] [WEB] Not a directory: %s\n", millis(), path);
    root.close();
    return;
  }

  Serial.printf("[%lu] [WEB] Scanning files in: %s\n", millis(), path);

  FsFile file = root.openNextFile();
  char name[500];
  while (file) {
    file.getName(name, sizeof(name));
    auto fileName = String(name);

    
    bool shouldHide = fileName.startsWith(".");

    
    if (!shouldHide) {
      for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
        if (fileName.equals(HIDDEN_ITEMS[i])) {
          shouldHide = true;
          break;
        }
      }
    }

    if (!shouldHide) {
      FileInfo info;
      info.name = fileName;
      info.isDirectory = file.isDirectory();

      if (info.isDirectory) {
        info.size = 0;
        info.isEpub = false;
      } else {
        info.size = file.size();
        info.isEpub = isEpubFile(info.name);
      }

      callback(info);
    }

    file.close();
    yield();               
    esp_task_wdt_reset();  
    file = root.openNextFile();
  }
  root.close();
}

bool LocalServer::isEpubFile(const String& filename) const {
  String lower = filename;
  lower.toLowerCase();
  return lower.endsWith(".epub");
}

void LocalServer::handleFileList() const { server->send(200, "text/html", FilesPageHtml); }

void LocalServer::handleFileListData() const {
  
  String currentPath = "/";
  if (server->hasArg("path")) {
    currentPath = server->arg("path");
    
    if (!currentPath.startsWith("/")) {
      currentPath = "/" + currentPath;
    }
    
    if (currentPath.length() > 1 && currentPath.endsWith("/")) {
      currentPath = currentPath.substring(0, currentPath.length() - 1);
    }
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  scanFiles(currentPath.c_str(), [this, &output, &doc, seenFirst](const FileInfo& info) mutable {
    doc.clear();
    doc["name"] = info.name;
    doc["size"] = info.size;
    doc["isDirectory"] = info.isDirectory;
    doc["isEpub"] = info.isEpub;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      
      Serial.printf("[%lu] [WEB] Skipping file entry with oversized JSON for name: %s\n", millis(), info.name.c_str());
      return;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  });
  server->sendContent("]");
  
  server->sendContent("");
  Serial.printf("[%lu] [WEB] Served file listing page for path: %s\n", millis(), currentPath.c_str());
}

void LocalServer::handleDownload() const {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = server->arg("path");
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (!itemPath.startsWith("/")) {
    itemPath = "/" + itemPath;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".")) {
    server->send(403, "text/plain", "Cannot access system files");
    return;
  }
  for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
    if (itemName.equals(HIDDEN_ITEMS[i])) {
      server->send(403, "text/plain", "Cannot access protected items");
      return;
    }
  }

  if (!SdMan.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  FsFile file = SdMan.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Path is a directory");
    return;
  }

  String contentType = "application/octet-stream";
  if (isEpubFile(itemPath)) {
    contentType = "application/epub+zip";
  }

  char nameBuf[128] = {0};
  String filename = "download";
  if (file.getName(nameBuf, sizeof(nameBuf))) {
    filename = nameBuf;
  }

  server->setContentLength(file.size());
  server->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server->send(200, contentType.c_str(), "");

  WiFiClient client = server->client();
  client.write(file);
  file.close();
}


static FsFile uploadFile;
static String uploadFileName;
static String uploadPath = "/";
static size_t uploadSize = 0;
static bool uploadSuccess = false;
static String uploadError = "";




constexpr size_t UPLOAD_BUFFER_SIZE = 4096;  
static uint8_t uploadBuffer[UPLOAD_BUFFER_SIZE];
static size_t uploadBufferPos = 0;


static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

static bool flushUploadBuffer() {
  if (uploadBufferPos > 0 && uploadFile) {
    esp_task_wdt_reset();  
    const unsigned long writeStart = millis();
    const size_t written = uploadFile.write(uploadBuffer, uploadBufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    esp_task_wdt_reset();  

    if (written != uploadBufferPos) {
      Serial.printf("[%lu] [WEB] [UPLOAD] Buffer flush failed: expected %d, wrote %d\n", millis(), uploadBufferPos,
                    written);
      uploadBufferPos = 0;
      return false;
    }
    uploadBufferPos = 0;
  }
  return true;
}

void LocalServer::handleUpload() const {
  static size_t lastLoggedSize = 0;

  
  esp_task_wdt_reset();

  
  if (!running || !server) {
    Serial.printf("[%lu] [WEB] [UPLOAD] ERROR: handleUpload called but server not running!\n", millis());
    return;
  }

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    
    esp_task_wdt_reset();

    uploadFileName = upload.filename;
    uploadSize = 0;
    uploadSuccess = false;
    uploadError = "";
    uploadStartTime = millis();
    lastLoggedSize = 0;
    uploadBufferPos = 0;
    totalWriteTime = 0;
    writeCount = 0;

    
    
    
    if (server->hasArg("path")) {
      uploadPath = server->arg("path");
      
      if (!uploadPath.startsWith("/")) {
        uploadPath = "/" + uploadPath;
      }
      
      if (uploadPath.length() > 1 && uploadPath.endsWith("/")) {
        uploadPath = uploadPath.substring(0, uploadPath.length() - 1);
      }
    } else {
      uploadPath = "/";
    }

    Serial.printf("[%lu] [WEB] [UPLOAD] START: %s to path: %s\n", millis(), uploadFileName.c_str(), uploadPath.c_str());
    Serial.printf("[%lu] [WEB] [UPLOAD] Free heap: %d bytes\n", millis(), ESP.getFreeHeap());

    
    String filePath = uploadPath;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += uploadFileName;

    
    esp_task_wdt_reset();
    if (SdMan.exists(filePath.c_str())) {
      Serial.printf("[%lu] [WEB] [UPLOAD] Overwriting existing file: %s\n", millis(), filePath.c_str());
      esp_task_wdt_reset();
      SdMan.remove(filePath.c_str());
    }

    
    esp_task_wdt_reset();
    if (!SdMan.openFileForWrite("WEB", filePath, uploadFile)) {
      uploadError = "Failed to create file on SD card";
      Serial.printf("[%lu] [WEB] [UPLOAD] FAILED to create file: %s\n", millis(), filePath.c_str());
      return;
    }
    esp_task_wdt_reset();

    Serial.printf("[%lu] [WEB] [UPLOAD] File created successfully: %s\n", millis(), filePath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && uploadError.isEmpty()) {
      
      
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = UPLOAD_BUFFER_SIZE - uploadBufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(uploadBuffer + uploadBufferPos, data, toCopy);
        uploadBufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        
        if (uploadBufferPos >= UPLOAD_BUFFER_SIZE) {
          if (!flushUploadBuffer()) {
            uploadError = "Failed to write to SD card - disk may be full";
            uploadFile.close();
            return;
          }
        }
      }

      uploadSize += upload.currentSize;

      
      if (uploadSize - lastLoggedSize >= 102400) {
        const unsigned long elapsed = millis() - uploadStartTime;
        const float kbps = (elapsed > 0) ? (uploadSize / 1024.0) / (elapsed / 1000.0) : 0;
        Serial.printf("[%lu] [WEB] [UPLOAD] %d bytes (%.1f KB), %.1f KB/s, %d writes\n", millis(), uploadSize,
                      uploadSize / 1024.0, kbps, writeCount);
        lastLoggedSize = uploadSize;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      
      if (!flushUploadBuffer()) {
        uploadError = "Failed to write final data to SD card";
      }
      uploadFile.close();

      if (uploadError.isEmpty()) {
        uploadSuccess = true;
        const unsigned long elapsed = millis() - uploadStartTime;
        const float avgKbps = (elapsed > 0) ? (uploadSize / 1024.0) / (elapsed / 1000.0) : 0;
        const float writePercent = (elapsed > 0) ? (totalWriteTime * 100.0 / elapsed) : 0;
        Serial.printf("[%lu] [WEB] [UPLOAD] Complete: %s (%d bytes in %lu ms, avg %.1f KB/s)\n", millis(),
                      uploadFileName.c_str(), uploadSize, elapsed, avgKbps);
        Serial.printf("[%lu] [WEB] [UPLOAD] Diagnostics: %d writes, total write time: %lu ms (%.1f%%)\n", millis(),
                      writeCount, totalWriteTime, writePercent);

        
        String filePath = uploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += uploadFileName;
        clearEpubCacheIfNeeded(filePath);
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadBufferPos = 0;  
    if (uploadFile) {
      uploadFile.close();
      
      String filePath = uploadPath;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += uploadFileName;
      SdMan.remove(filePath.c_str());
    }
    uploadError = "Upload aborted";
    Serial.printf("[%lu] [WEB] Upload aborted\n", millis());
  }
}

void LocalServer::handleUploadPost() const {
  if (uploadSuccess) {
    server->send(200, "text/plain", "File uploaded successfully: " + uploadFileName);
  } else {
    const String error = uploadError.isEmpty() ? "Unknown error during upload" : uploadError;
    server->send(400, "text/plain", error);
  }
}

void LocalServer::handleCreateFolder() const {
  
  if (!server->hasArg("name")) {
    server->send(400, "text/plain", "Missing folder name");
    return;
  }

  const String folderName = server->arg("name");

  
  if (folderName.isEmpty()) {
    server->send(400, "text/plain", "Folder name cannot be empty");
    return;
  }

  
  String parentPath = "/";
  if (server->hasArg("path")) {
    parentPath = server->arg("path");
    if (!parentPath.startsWith("/")) {
      parentPath = "/" + parentPath;
    }
    if (parentPath.length() > 1 && parentPath.endsWith("/")) {
      parentPath = parentPath.substring(0, parentPath.length() - 1);
    }
  }

  
  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;

  Serial.printf("[%lu] [WEB] Creating folder: %s\n", millis(), folderPath.c_str());

  
  if (SdMan.exists(folderPath.c_str())) {
    server->send(400, "text/plain", "Folder already exists");
    return;
  }

  
  if (SdMan.mkdir(folderPath.c_str())) {
    Serial.printf("[%lu] [WEB] Folder created successfully: %s\n", millis(), folderPath.c_str());
    server->send(200, "text/plain", "Folder created: " + folderName);
  } else {
    Serial.printf("[%lu] [WEB] Failed to create folder: %s\n", millis(), folderPath.c_str());
    server->send(500, "text/plain", "Failed to create folder");
  }
}

void LocalServer::handleDelete() const {
  
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = server->arg("path");
  const String itemType = server->hasArg("type") ? server->arg("type") : "file";

  
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Cannot delete root directory");
    return;
  }

  
  if (!itemPath.startsWith("/")) {
    itemPath = "/" + itemPath;
  }

  
  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);

  
  if (itemName.startsWith(".")) {
    Serial.printf("[%lu] [WEB] Delete rejected - hidden/system item: %s\n", millis(), itemPath.c_str());
    server->send(403, "text/plain", "Cannot delete system files");
    return;
  }

  
  for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
    if (itemName.equals(HIDDEN_ITEMS[i])) {
      Serial.printf("[%lu] [WEB] Delete rejected - protected item: %s\n", millis(), itemPath.c_str());
      server->send(403, "text/plain", "Cannot delete protected items");
      return;
    }
  }

  
  if (!SdMan.exists(itemPath.c_str())) {
    Serial.printf("[%lu] [WEB] Delete failed - item not found: %s\n", millis(), itemPath.c_str());
    server->send(404, "text/plain", "Item not found");
    return;
  }

  Serial.printf("[%lu] [WEB] Attempting to delete %s: %s\n", millis(), itemType.c_str(), itemPath.c_str());

  bool success = false;

  if (itemType == "folder") {
    
    FsFile dir = SdMan.open(itemPath.c_str());
    if (dir && dir.isDirectory()) {
      
      FsFile entry = dir.openNextFile();
      if (entry) {
        
        entry.close();
        dir.close();
        Serial.printf("[%lu] [WEB] Delete failed - folder not empty: %s\n", millis(), itemPath.c_str());
        server->send(400, "text/plain", "Folder is not empty. Delete contents first.");
        return;
      }
      dir.close();
    }
    success = SdMan.rmdir(itemPath.c_str());
  } else {
    
    success = SdMan.remove(itemPath.c_str());
  }

  if (success) {
    Serial.printf("[%lu] [WEB] Successfully deleted: %s\n", millis(), itemPath.c_str());
    server->send(200, "text/plain", "Deleted successfully");
  } else {
    Serial.printf("[%lu] [WEB] Failed to delete: %s\n", millis(), itemPath.c_str());
    server->send(500, "text/plain", "Failed to delete item");
  }
}


void LocalServer::wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (wsInstance) {
    wsInstance->onWebSocketEvent(num, type, payload, length);
  }
}







void LocalServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%lu] [WS] Client %u disconnected\n", millis(), num);
      
      if (wsUploadInProgress && wsUploadFile) {
        wsUploadFile.close();
        
        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        SdMan.remove(filePath.c_str());
        Serial.printf("[%lu] [WS] Deleted incomplete upload: %s\n", millis(), filePath.c_str());
      }
      wsUploadInProgress = false;
      break;

    case WStype_CONNECTED: {
      Serial.printf("[%lu] [WS] Client %u connected\n", millis(), num);
      break;
    }

    case WStype_TEXT: {
      
      String msg = String((char*)payload);
      Serial.printf("[%lu] [WS] Text from client %u: %s\n", millis(), num, msg.c_str());

      if (msg.startsWith("START:")) {
        
        int firstColon = msg.indexOf(':', 6);
        int secondColon = msg.indexOf(':', firstColon + 1);

        if (firstColon > 0 && secondColon > 0) {
          wsUploadFileName = msg.substring(6, firstColon);
          wsUploadSize = msg.substring(firstColon + 1, secondColon).toInt();
          wsUploadPath = msg.substring(secondColon + 1);
          wsUploadReceived = 0;
          wsUploadStartTime = millis();

          
          if (!wsUploadPath.startsWith("/")) wsUploadPath = "/" + wsUploadPath;
          if (wsUploadPath.length() > 1 && wsUploadPath.endsWith("/")) {
            wsUploadPath = wsUploadPath.substring(0, wsUploadPath.length() - 1);
          }

          
          String filePath = wsUploadPath;
          if (!filePath.endsWith("/")) filePath += "/";
          filePath += wsUploadFileName;

          Serial.printf("[%lu] [WS] Starting upload: %s (%d bytes) to %s\n", millis(), wsUploadFileName.c_str(),
                        wsUploadSize, filePath.c_str());

          
          esp_task_wdt_reset();
          if (SdMan.exists(filePath.c_str())) {
            SdMan.remove(filePath.c_str());
          }

          
          esp_task_wdt_reset();
          if (!SdMan.openFileForWrite("WS", filePath, wsUploadFile)) {
            wsServer->sendTXT(num, "ERROR:Failed to create file");
            wsUploadInProgress = false;
            return;
          }
          esp_task_wdt_reset();

          wsUploadInProgress = true;
          wsServer->sendTXT(num, "READY");
        } else {
          wsServer->sendTXT(num, "ERROR:Invalid START format");
        }
      }
      break;
    }

    case WStype_BIN: {
      if (!wsUploadInProgress || !wsUploadFile) {
        wsServer->sendTXT(num, "ERROR:No upload in progress");
        return;
      }

      
      esp_task_wdt_reset();
      size_t written = wsUploadFile.write(payload, length);
      esp_task_wdt_reset();

      if (written != length) {
        wsUploadFile.close();
        wsUploadInProgress = false;
        wsServer->sendTXT(num, "ERROR:Write failed - disk full?");
        return;
      }

      wsUploadReceived += written;

      
      static size_t lastProgressSent = 0;
      if (wsUploadReceived - lastProgressSent >= 65536 || wsUploadReceived >= wsUploadSize) {
        String progress = "PROGRESS:" + String(wsUploadReceived) + ":" + String(wsUploadSize);
        wsServer->sendTXT(num, progress);
        lastProgressSent = wsUploadReceived;
      }

      
      if (wsUploadReceived >= wsUploadSize) {
        wsUploadFile.close();
        wsUploadInProgress = false;

        wsLastCompleteName = wsUploadFileName;
        wsLastCompleteSize = wsUploadSize;
        wsLastCompleteAt = millis();

        unsigned long elapsed = millis() - wsUploadStartTime;
        float kbps = (elapsed > 0) ? (wsUploadSize / 1024.0) / (elapsed / 1000.0) : 0;

        Serial.printf("[%lu] [WS] Upload complete: %s (%d bytes in %lu ms, %.1f KB/s)\n", millis(),
                      wsUploadFileName.c_str(), wsUploadSize, elapsed, kbps);

        
        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        clearEpubCacheIfNeeded(filePath);

        wsServer->sendTXT(num, "DONE");
        lastProgressSent = 0;
      }
      break;
    }

    default:
      break;
  }
}

void LocalServer::handleSettingsPage() const {
  server->send(200, "text/html", SettingsPageHtml);
  Serial.printf("[%lu] [WEB] Served settings page\n", millis());
}

void LocalServer::handleSettingsGet() const {
  JsonDocument doc;
  
  
  doc["sleepScreen"] = SETTINGS.sleepScreen;
  doc["sleepScreenCoverMode"] = SETTINGS.sleepScreenCoverMode;
  doc["sleepScreenCoverFilter"] = SETTINGS.sleepScreenCoverFilter;
  doc["sleepScreenCoverGrayscale"] = SETTINGS.sleepScreenCoverGrayscale;
  doc["sleepCustomBmp"] = SETTINGS.sleepCustomBmp;
  doc["hideBatteryPercentage"] = SETTINGS.hideBatteryPercentage;
  doc["recentLibraryMode"] = SETTINGS.recentLibraryMode;
  
  
  doc["fontFamily"] = SETTINGS.fontFamily;
  doc["fontSize"] = SETTINGS.fontSize;
  
  
  doc["lineSpacing"] = SETTINGS.lineSpacing;
  doc["screenMargin"] = SETTINGS.screenMargin;
  doc["paragraphAlignment"] = SETTINGS.paragraphAlignment;
  doc["paragraphCssIndentEnabled"] = SETTINGS.paragraphCssIndentEnabled;
  doc["extraParagraphSpacing"] = SETTINGS.extraParagraphSpacing;
  doc["orientation"] = SETTINGS.orientation;
  doc["hyphenationEnabled"] = SETTINGS.hyphenationEnabled;
  
  
  doc["readerDirectionMapping"] = SETTINGS.readerDirectionMapping;
  doc["readerMenuButton"] = SETTINGS.readerMenuButton;
  doc["longPressChapterSkip"] = SETTINGS.longPressChapterSkip;
  doc["readerShortPwrBtn"] = SETTINGS.readerShortPwrBtn;
  
  
  doc["textAntiAliasing"] = SETTINGS.textAntiAliasing;
  doc["refreshFrequency"] = SETTINGS.refreshFrequency;
  doc["readerImageGrayscale"] = SETTINGS.readerImageGrayscale;
  doc["readerSmartRefreshOnImages"] = SETTINGS.readerSmartRefreshOnImages;
  doc["readerImagePresentation"] = SETTINGS.readerImagePresentation;
  doc["readerImageDither"] = SETTINGS.readerImageDither;
  doc["displayImageDither"] = SETTINGS.displayImageDither;
  doc["displayImagePresentation"] = SETTINGS.displayImagePresentation;
  doc["statusBar"] = SETTINGS.statusBar;
  doc["statusBarLeft"] = SETTINGS.statusBarLeft;
  doc["statusBarMiddle"] = SETTINGS.statusBarMiddle;
  doc["statusBarRight"] = SETTINGS.statusBarRight;
  
  
  doc["frontButtonLayout"] = SETTINGS.frontButtonLayout;
  doc["shortPwrBtn"] = SETTINGS.shortPwrBtn;
  
  
  doc["sleepTimeout"] = SETTINGS.sleepTimeout;
  doc["useLibraryIndex"] = SETTINGS.useLibraryIndex;
  doc["bootSetting"] = SETTINGS.bootSetting;
  
  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void LocalServer::handleSettingsUpdate() const {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }
  
  String body = server->arg("plain");
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }
  
  bool changed = false;
  
  
  for (JsonPair kv : doc.as<JsonObject>()) {
    const char* key = kv.key().c_str();
    int value = kv.value().as<int>();
    
    if (strcmp(key, "sleepScreen") == 0) {
      SETTINGS.sleepScreen = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "sleepScreenCoverMode") == 0) {
      SETTINGS.sleepScreenCoverMode = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "sleepScreenCoverFilter") == 0) {
      SETTINGS.sleepScreenCoverFilter = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "sleepScreenCoverGrayscale") == 0) {
      SETTINGS.sleepScreenCoverGrayscale = (uint8_t)value ? 1 : 0;
      changed = true;
    }
    else if (strcmp(key, "sleepCustomBmp") == 0) {
      if (kv.value().isNull()) {
        SETTINGS.setSleepCustomBmpFromInput(nullptr);
      } else {
        SETTINGS.setSleepCustomBmpFromInput(kv.value().as<const char*>());
      }
      changed = true;
    }
    else if (strcmp(key, "hideBatteryPercentage") == 0) {
      SETTINGS.hideBatteryPercentage = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "recentLibraryMode") == 0) {
      SETTINGS.recentLibraryMode = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "fontFamily") == 0) {
      SETTINGS.fontFamily = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "fontSize") == 0) {
      SETTINGS.fontSize = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "lineSpacing") == 0) {
      SETTINGS.lineSpacing = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "screenMargin") == 0) {
      SETTINGS.screenMargin = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "paragraphAlignment") == 0) {
      SETTINGS.paragraphAlignment = (uint8_t)value;
      if (SETTINGS.paragraphAlignment >= SystemSetting::PARAGRAPH_ALIGNMENT_COUNT) {
        SETTINGS.paragraphAlignment = SystemSetting::JUSTIFIED;
      }
      changed = true;
    }
    else if (strcmp(key, "extraParagraphSpacing") == 0) {
      SETTINGS.extraParagraphSpacing = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "paragraphCssIndentEnabled") == 0) {
      SETTINGS.paragraphCssIndentEnabled = (uint8_t)value ? 1 : 0;
      changed = true;
    }
    else if (strcmp(key, "orientation") == 0) {
      SETTINGS.orientation = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "hyphenationEnabled") == 0) {
      SETTINGS.hyphenationEnabled = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "readerDirectionMapping") == 0) {
      SETTINGS.readerDirectionMapping = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "readerMenuButton") == 0) {
      SETTINGS.readerMenuButton = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "longPressChapterSkip") == 0) {
      SETTINGS.longPressChapterSkip = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "readerShortPwrBtn") == 0) {
      SETTINGS.readerShortPwrBtn = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "textAntiAliasing") == 0) {
      SETTINGS.textAntiAliasing = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "refreshFrequency") == 0) {
      SETTINGS.refreshFrequency = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "readerImageGrayscale") == 0) {
      SETTINGS.readerImageGrayscale = (uint8_t)value ? 1 : 0;
      changed = true;
    }
    else if (strcmp(key, "readerSmartRefreshOnImages") == 0) {
      SETTINGS.readerSmartRefreshOnImages = (uint8_t)value ? 1 : 0;
      changed = true;
    }
    else if (strcmp(key, "readerImagePresentation") == 0) {
      if (value >= 0 && value < SystemSetting::READER_IMAGE_PRESENTATION_COUNT) {
        SETTINGS.readerImagePresentation = (uint8_t)value;
        changed = true;
      }
    }
    else if (strcmp(key, "readerImageDither") == 0) {
      if (value >= 0 && value < SystemSetting::READER_IMAGE_DITHER_COUNT) {
        SETTINGS.readerImageDither = (uint8_t)value;
        changed = true;
      }
    }
    else if (strcmp(key, "displayImageDither") == 0) {
      if (value >= 0 && value < SystemSetting::READER_IMAGE_DITHER_COUNT) {
        SETTINGS.displayImageDither = (uint8_t)value;
        changed = true;
      }
    }
    else if (strcmp(key, "displayImagePresentation") == 0) {
      if (value >= 0 && value < SystemSetting::READER_IMAGE_PRESENTATION_COUNT) {
        SETTINGS.displayImagePresentation = (uint8_t)value;
        changed = true;
      }
    }
    else if (strcmp(key, "statusBar") == 0) {
      SETTINGS.statusBar = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "statusBarLeft") == 0) {
      SETTINGS.statusBarLeft = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "statusBarMiddle") == 0) {
      SETTINGS.statusBarMiddle = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "statusBarRight") == 0) {
      SETTINGS.statusBarRight = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "frontButtonLayout") == 0) {
      SETTINGS.frontButtonLayout = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "shortPwrBtn") == 0) {
      SETTINGS.shortPwrBtn = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "sleepTimeout") == 0) {
      SETTINGS.sleepTimeout = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "useLibraryIndex") == 0) {
      SETTINGS.useLibraryIndex = (uint8_t)value;
      changed = true;
    }
    else if (strcmp(key, "bootSetting") == 0) {
      SETTINGS.bootSetting = (uint8_t)value;
      changed = true;
    }
  }
  
  if (changed) {
    SETTINGS.saveToFile();
    Serial.printf("[%lu] [WEB] Settings updated and saved\n", millis());
  }
  
  server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void LocalServer::handleWifiGet() const {
    JsonDocument doc;
    const auto& creds = WIFI_STORE.getCredentials();
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& cred : creds) {
        JsonObject obj = arr.add<JsonObject>();
        obj["ssid"] = cred.ssid;
    }
    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
}

void LocalServer::handleWifiPost() const {
    if (!server->hasArg("plain")) {
        server->send(400, "text/plain", "Missing JSON");
        return;
    }
    
    JsonDocument doc;
    deserializeJson(doc, server->arg("plain"));
    String ssid = doc["ssid"];
    String password = doc["password"] | "";
    
    if (WIFI_STORE.addCredential(ssid.c_str(), password.c_str())) {
        WIFI_STORE.saveToFile();
        server->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        server->send(500, "text/plain", "Failed to save");
    }
}

void LocalServer::handleWifiDelete() const {
    String uri = server->uri();
    int lastSlash = uri.lastIndexOf('/');
    String ssid = uri.substring(lastSlash + 1);
    ssid.replace("%20", " ");
    
    if (WIFI_STORE.removeCredential(ssid.c_str())) {
        WIFI_STORE.saveToFile();
        server->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        server->send(404, "text/plain", "Not found");
    }
}

void LocalServer::handleKOReaderGet() const {
    JsonDocument doc;
    doc["username"] = KOREADER_STORE.getUsername();
    doc["serverUrl"] = KOREADER_STORE.getServerUrl();
    doc["matchMethod"] = (int)KOREADER_STORE.getMatchMethod();
    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
}

void LocalServer::handleKOReaderPost() const {
    if (!server->hasArg("plain")) {
        server->send(400, "text/plain", "Missing JSON");
        return;
    }
    
    JsonDocument doc;
    deserializeJson(doc, server->arg("plain"));
    
    String username = doc["username"] | "";
    String password = doc["password"] | "";
    String serverUrl = doc["serverUrl"] | "";
    int matchMethod = doc["matchMethod"] | 0;
    
    KOREADER_STORE.setCredentials(username.c_str(), password.c_str());
    KOREADER_STORE.setServerUrl(serverUrl.c_str());
    KOREADER_STORE.setMatchMethod((DocumentMatchMethod)matchMethod);
    KOREADER_STORE.saveToFile();
    
    server->send(200, "application/json", "{\"status\":\"ok\"}");
}