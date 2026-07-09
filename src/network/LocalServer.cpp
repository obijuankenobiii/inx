/**
 * @file LocalServer.cpp
 * @brief Definitions for LocalServer.
 */

#include "LocalServer.h"

#include <ArduinoJson.h>
#ifndef INX_SIMULATOR_WEB_ONLY
#include <Epub.h>
#include <FsHelpers.h>
#include <HalGPIO.h>
#endif
#include <SDCardManager.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>

#include "../state/SystemSetting.h"
#include "html/EpubPageHtml.generated.h"
#include "html/EpubPageJs.generated.h"
#include "html/FilesPageJs.generated.h"
#include "html/FilesPageHtml.generated.h"
#include "html/FontManagerPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/InxFontPackJs.generated.h"
#include "html/JsZipMinJs.generated.h"
#include "html/SettingsPageHtml.generated.h"
#include "html/TagsPageHtml.generated.h"
#ifndef INX_SIMULATOR_WEB_ONLY
#include "activity/settings/LibraryIndexer.h"
#include "state/BookState.h"
#include "state/BookTags.h"
#include "state/RecentBooks.h"
#include "system/FontManager.h"
#include "util/StringUtils.h"
#endif
#include "KOReaderCredentialStore.h"
#include "state/NetworkCredential.h"
#ifndef INX_SIMULATOR_WEB_ONLY
#include "state/OpdsServerStore.h"
#endif

namespace {

const char* HIDDEN_ITEMS[] = {"System Volume Information", ".metadata"};
constexpr size_t HIDDEN_ITEMS_COUNT = sizeof(HIDDEN_ITEMS) / sizeof(HIDDEN_ITEMS[0]);
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;

LocalServer* wsInstance = nullptr;

volatile bool webLibraryIndexing = false;
volatile int webLibraryIndexCurrent = 0;
volatile int webLibraryIndexTotal = 0;
char webLibraryIndexPath[128] = "";

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

void copySettingString(char* dest, size_t destSize, const char* value) {
  if (destSize == 0) {
    return;
  }
  if (value == nullptr) {
    value = "";
  }
  strncpy(dest, value, destSize - 1);
  dest[destSize - 1] = '\0';
}

void clearEpubCacheIfNeeded(const String& filePath) {
#ifndef INX_SIMULATOR_WEB_ONLY
  if (StringUtils::checkFileExtension(filePath, ".epub")) {
    Epub(filePath.c_str(), "/.metadata").clearCache();
    Serial.printf("[%lu] [WEB] Cleared epub cache for: %s\n", millis(), filePath.c_str());
  }
#else
  (void)filePath;
#endif
}

bool clockSettingsAvailable() {
#ifndef INX_SIMULATOR_WEB_ONLY
  return gpio.deviceIsX3();
#else
  return false;
#endif
}

#ifndef INX_SIMULATOR_WEB_ONLY
struct IndexedBookInfo {
  String path;
  String title;
  String folder;
  String tag;
};

std::string lowerAscii(const String& value) {
  std::string lowered = value.c_str();
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lowered;
}

String jsonEscape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s.charAt(i);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

bool readExactString(FsFile& file, size_t len, String& out) {
  out = "";
  if (len == 0) {
    return true;
  }
  std::vector<char> buf(len + 1, 0);
  if (file.read(buf.data(), len) != static_cast<int>(len)) {
    return false;
  }
  out = String(buf.data());
  return true;
}

String indexedFolderName(const String& path) {
  if (path == "/" || path.length() == 0) {
    return "Library";
  }
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash < 0) {
    return path;
  }
  String name = path.substring(lastSlash + 1);
  return name.length() ? name : "Library";
}

bool readIndexedBook(FsFile& idxFile, IndexedBookInfo& out) {
  uint16_t pLen = 0;
  if (idxFile.read(&pLen, sizeof(pLen)) != sizeof(pLen)) return false;
  if (!readExactString(idxFile, pLen, out.path)) return false;

  uint8_t nLen = 0;
  if (idxFile.read(&nLen, sizeof(nLen)) != sizeof(nLen)) return false;
  idxFile.seek(idxFile.position() + nLen);

  uint8_t dLen = 0;
  if (idxFile.read(&dLen, sizeof(dLen)) != sizeof(dLen)) return false;
  if (!readExactString(idxFile, dLen, out.title)) return false;

  uint8_t fLen = 0;
  if (idxFile.read(&fLen, sizeof(fLen)) != sizeof(fLen)) return false;
  if (!readExactString(idxFile, fLen, out.folder)) return false;
  if (out.folder.length() == 0) {
    int slash = out.path.lastIndexOf('/');
    out.folder = slash <= 0 ? "Library" : indexedFolderName(out.path.substring(0, slash));
  }
  return true;
}

void skipIndexedDirectory(FsFile& idxFile) {
  uint16_t pathLen = 0;
  if (idxFile.read(&pathLen, sizeof(pathLen)) != sizeof(pathLen)) return;
  idxFile.seek(idxFile.position() + pathLen);
  uint16_t entryCount = 0;
  idxFile.read(&entryCount, sizeof(entryCount));
}

bool loadIndexedBooksWithTags(std::vector<IndexedBookInfo>& books) {
  books.clear();
  FsFile idxFile = SdMan.open("/.metadata/library/library.idx", O_READ);
  if (!idxFile) {
    return false;
  }

  char magic[4] = {};
  uint8_t version = 0;
  if (idxFile.read(magic, 4) != 4 || memcmp(magic, "LIBX", 4) != 0 || idxFile.read(&version, 1) != 1) {
    idxFile.close();
    return false;
  }

  std::vector<BookTags::Entry> tags;
  BookTags::load(tags);

  while (idxFile.available()) {
    uint8_t marker = 0;
    if (idxFile.read(&marker, 1) != 1) break;
    if (marker == 0x01) {
      IndexedBookInfo book;
      if (!readIndexedBook(idxFile, book)) break;
      const std::string tag = BookTags::find(tags, book.path.c_str());
      book.tag = tag.c_str();
      books.push_back(book);
      if ((books.size() % 64u) == 0u) {
        yield();
      }
    } else if (marker == 0xFF) {
      skipIndexedDirectory(idxFile);
    }
  }

  idxFile.close();
  return true;
}

void webLibraryIndexTask(void*) {
  webLibraryIndexCurrent = 0;
  webLibraryIndexTotal = 0;
  webLibraryIndexPath[0] = '\0';

  FsFile root = SdMan.open("/");
  if (root) {
    webLibraryIndexTotal = LibraryIndexer::countBooks(root);
    root.close();
  }

  vTaskDelay(pdMS_TO_TICKS(10));

  LibraryIndexer::indexAll([](int current, int total, const char* path) {
    webLibraryIndexCurrent = current;
    webLibraryIndexTotal = total;
    if (path) {
      strlcpy(webLibraryIndexPath, path, sizeof(webLibraryIndexPath));
    }
    if (current % 10 == 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  });

  SETTINGS.useLibraryIndex = 1;
  SETTINGS.saveToFile();
  webLibraryIndexing = false;
  vTaskDelete(nullptr);
}
#endif
}  // namespace

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
  server->on("/epub", HTTP_GET, [this] { handleEpubPage(); });
  server->on("/font-manager", HTTP_GET, [this] { handleFontManagerPage(); });
  server->on("/tags", HTTP_GET, [this] { handleTagsPage(); });
  server->on("/js/inx_font_pack.js", HTTP_GET, [this] { handleInxFontPackJs(); });
  server->on("/js/jszip.min.js", HTTP_GET, [this] { handleJsZipMinJs(); });
  server->on("/js/epub_page.js", HTTP_GET, [this] { handleEpubPageJs(); });
  server->on("/js/files_page.js", HTTP_GET, [this] { handleFilesPageJs(); });

  server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });
  server->on("/api/book-tags", HTTP_GET, [this] { handleBookTagsGet(); });
  server->on("/api/book-tags", HTTP_POST, [this] { handleBookTagsPost(); });
  server->on("/api/library-index/refresh", HTTP_POST, [this] { handleLibraryIndexRefresh(); });
  server->on("/api/library-index/status", HTTP_GET, [this] { handleLibraryIndexStatus(); });
  server->on("/download", HTTP_GET, [this] { handleDownload(); });

  server->on("/upload", HTTP_POST, [this] { handleUploadPost(); }, [this] { handleUpload(); });

  server->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });

  server->on("/delete", HTTP_POST, [this] { handleDelete(); });

  server->on("/rename", HTTP_POST, [this] { handleRename(); });

  server->on("/settings", HTTP_GET, [this] { handleSettingsPage(); });
  server->on("/api/settings", HTTP_GET, [this] { handleSettingsGet(); });
  server->on("/api/settings", HTTP_POST, [this] { handleSettingsUpdate(); });

  server->on("/api/wifi", HTTP_GET, [this] { handleWifiGet(); });
  server->on("/api/wifi", HTTP_POST, [this] { handleWifiPost(); });
  server->on("/api/wifi/*", HTTP_DELETE, [this] { handleWifiDelete(); });
  server->on("/api/koreader", HTTP_GET, [this] { handleKOReaderGet(); });
  server->on("/api/koreader", HTTP_POST, [this] { handleKOReaderPost(); });

#ifndef INX_SIMULATOR_WEB_ONLY
  server->on("/api/opds", HTTP_GET, [this] { handleOpdsGet(); });
  server->on("/api/opds", HTTP_POST, [this] { handleOpdsPost(); });
  server->on("/api/opds/*", HTTP_DELETE, [this] { handleOpdsDelete(); });
#endif

  server->on("/api/fonts/rescan", HTTP_POST, [this] { handleFontsRescan(); });

  server->onNotFound([this] { handleNotFound(); });
  Serial.printf("[%lu] [WEB] [MEM] Free heap after route setup: %d bytes\n", millis(), ESP.getFreeHeap());
  Serial.printf("✓ jszip.min.js from firmware flash (%u bytes)\n", static_cast<unsigned>(sizeof(JSZIP_MIN_JS) - 1));
  Serial.printf("✓ epub_page.js from firmware flash (%u bytes)\n", static_cast<unsigned>(sizeof(EPUB_PAGE_JS) - 1));
  Serial.printf("✓ files_page.js from firmware flash (%u bytes)\n", static_cast<unsigned>(sizeof(FILES_PAGE_JS) - 1));
  Serial.printf("✓ inx_font_pack.js from firmware flash (%u bytes)\n",
                static_cast<unsigned>(sizeof(INX_FONT_PACK_JS) - 1));

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
  std::string lower = filename.c_str();
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".epub") == 0;
}

void LocalServer::handleFileList() const { server->send(200, "text/html", FilesPageHtml); }

void LocalServer::handleEpubPage() const {
  server->send_P(200, PSTR("text/html; charset=utf-8"), EpubPageHtml, sizeof(EpubPageHtml) - 1);
}

void LocalServer::handleFontManagerPage() const { server->send(200, "text/html", FontManagerPageHtml); }

void LocalServer::handleTagsPage() const { server->send(200, "text/html", TagsPageHtml); }

void LocalServer::handleInxFontPackJs() const {
  server->send_P(200, PSTR("text/javascript; charset=utf-8"), INX_FONT_PACK_JS, sizeof(INX_FONT_PACK_JS) - 1);
}

void LocalServer::handleJsZipMinJs() const {
  server->send_P(200, PSTR("text/javascript; charset=utf-8"), JSZIP_MIN_JS, sizeof(JSZIP_MIN_JS) - 1);
}

void LocalServer::handleEpubPageJs() const {
  server->send_P(200, PSTR("text/javascript; charset=utf-8"), EPUB_PAGE_JS, sizeof(EPUB_PAGE_JS) - 1);
}

void LocalServer::handleFilesPageJs() const {
  server->send_P(200, PSTR("text/javascript; charset=utf-8"), FILES_PAGE_JS, sizeof(FILES_PAGE_JS) - 1);
}

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

void LocalServer::handleBookTagsGet() const {
#ifdef INX_SIMULATOR_WEB_ONLY
  server->send(501, "application/json", "{\"ok\":false,\"error\":\"unavailable_in_simulator\"}");
#else
  std::vector<IndexedBookInfo> books;
  const bool hasIndex = loadIndexedBooksWithTags(books);
  std::vector<std::string> tags;
  BookTags::loadTagList(tags);

  std::sort(books.begin(), books.end(), [](const IndexedBookInfo& a, const IndexedBookInfo& b) {
    return lowerAscii(a.title) < lowerAscii(b.title);
  });

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("{\"indexed\":");
  server->sendContent(hasIndex ? "true" : "false");
  server->sendContent(",\"tags\":[");
  for (size_t i = 0; i < tags.size(); ++i) {
    if (i > 0) {
      server->sendContent(",");
    }
    server->sendContent("\"" + jsonEscape(tags[i].c_str()) + "\"");
  }
  server->sendContent("],\"books\":[");
  bool first = true;
  for (const auto& book : books) {
    if (!first) {
      server->sendContent(",");
    }
    first = false;
    String row = "{\"path\":\"" + jsonEscape(book.path) + "\",\"title\":\"" + jsonEscape(book.title) +
                 "\",\"folder\":\"" + jsonEscape(book.folder) + "\",\"tag\":\"" + jsonEscape(book.tag) + "\"}";
    server->sendContent(row);
    yield();
  }
  server->sendContent("]}");
  server->sendContent("");
#endif
}

void LocalServer::handleLibraryIndexRefresh() const {
#ifdef INX_SIMULATOR_WEB_ONLY
  server->send(501, "application/json", "{\"ok\":false,\"error\":\"unavailable_in_simulator\"}");
#else
  if (webLibraryIndexing) {
    server->send(200, "application/json", "{\"ok\":true,\"indexing\":true}");
    return;
  }

  webLibraryIndexing = true;
  webLibraryIndexCurrent = 0;
  webLibraryIndexTotal = 0;
  webLibraryIndexPath[0] = '\0';

  BaseType_t created = xTaskCreate(webLibraryIndexTask, "WebLibIndex", 4096, nullptr, 1, nullptr);
  if (created != pdPASS) {
    webLibraryIndexing = false;
    server->send(500, "application/json", "{\"ok\":false,\"error\":\"task\"}");
    return;
  }

  server->send(200, "application/json", "{\"ok\":true,\"indexing\":true}");
#endif
}

void LocalServer::handleLibraryIndexStatus() const {
#ifdef INX_SIMULATOR_WEB_ONLY
  server->send(200, "application/json", "{\"indexing\":false,\"current\":0,\"total\":0,\"path\":\"\"}");
#else
  String body = "{\"indexing\":";
  body += webLibraryIndexing ? "true" : "false";
  body += ",\"current\":";
  body += String(webLibraryIndexCurrent);
  body += ",\"total\":";
  body += String(webLibraryIndexTotal);
  body += ",\"path\":\"";
  body += jsonEscape(String(webLibraryIndexPath));
  body += "\"}";
  server->send(200, "application/json", body);
#endif
}

void LocalServer::handleBookTagsPost() const {
#ifdef INX_SIMULATOR_WEB_ONLY
  server->send(501, "application/json", "{\"ok\":false,\"error\":\"unavailable_in_simulator\"}");
#else
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server->arg("plain"));
  if (err) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }

  const char* action = doc["action"] | "";
  if (strcmp(action, "addTag") == 0) {
    const char* tag = doc["tag"] | "";
    if (!BookTags::addTag(tag)) {
      server->send(500, "text/plain", "Failed to save tag");
      return;
    }
    server->send(200, "application/json", "{\"ok\":true}");
    return;
  }

  if (strcmp(action, "renameTag") == 0) {
    const char* oldTag = doc["oldTag"] | "";
    const char* newTag = doc["newTag"] | "";
    if (!BookTags::renameTag(oldTag, newTag)) {
      server->send(500, "text/plain", "Failed to rename tag");
      return;
    }
    server->send(200, "application/json", "{\"ok\":true}");
    return;
  }

  if (strcmp(action, "deleteTag") == 0) {
    const char* tag = doc["tag"] | "";
    if (!BookTags::deleteTag(tag)) {
      server->send(500, "text/plain", "Failed to delete tag");
      return;
    }
    server->send(200, "application/json", "{\"ok\":true}");
    return;
  }

  const char* path = doc["path"] | "";
  const char* tag = doc["tag"] | "";
  if (!path[0]) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  if (!BookTags::set(path, tag)) {
    server->send(500, "text/plain", "Failed to save tag");
    return;
  }

  server->send(200, "application/json", "{\"ok\":true}");
#endif
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
static uint8_t* uploadBuffer = nullptr;
static size_t uploadBufferPos = 0;

static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

static void freeUploadBuffer() {
  std::free(uploadBuffer);
  uploadBuffer = nullptr;
  uploadBufferPos = 0;
}

static bool flushUploadBuffer() {
  if (uploadBufferPos > 0 && uploadFile && uploadBuffer) {
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
    yield();
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
    freeUploadBuffer();

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

    uploadBuffer = static_cast<uint8_t*>(std::malloc(UPLOAD_BUFFER_SIZE));
    if (!uploadBuffer) {
      uploadError = "Failed to allocate upload buffer";
      uploadFile.close();
      SdMan.remove(filePath.c_str());
      Serial.printf("[%lu] [WEB] [UPLOAD] FAILED to allocate %d byte buffer\n", millis(), UPLOAD_BUFFER_SIZE);
      return;
    }

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
      freeUploadBuffer();

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
    freeUploadBuffer();
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

void LocalServer::collectEpubRenames(const std::string& oldDirPath, const std::string& newDirPath,
                                     std::vector<std::pair<std::string, std::string>>& out) const {
  scanFiles(oldDirPath.c_str(), [&](const FileInfo info) {
    const std::string childOld = oldDirPath + "/" + info.name.c_str();
    const std::string childNew = newDirPath + "/" + info.name.c_str();
    if (info.isDirectory) {
      collectEpubRenames(childOld, childNew, out);
    } else if (info.isEpub) {
      out.emplace_back(childOld, childNew);
    }
  });
}

void LocalServer::migrateEpubBookState(const std::string& oldPath, const std::string& newPath) const {
#ifndef INX_SIMULATOR_WEB_ONLY
  // Bookmarks, annotations, reading progress, and book settings all live under a cache dir keyed by
  // hash(filepath) (see Epub::Epub) - move it alongside the file so a rename doesn't orphan that state.
  const std::string oldCachePath = "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(oldPath));
  const std::string newCachePath = "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(newPath));

  if (SdMan.exists(oldCachePath.c_str()) && !SdMan.exists(newCachePath.c_str())) {
    SdMan.rename(oldCachePath.c_str(), newCachePath.c_str());
  }

  BOOK_STATE.renamePath(oldPath, newPath);
  RECENT_BOOKS.renamePath(oldPath, newPath, newCachePath);
#else
  (void)oldPath;
  (void)newPath;
#endif
}

void LocalServer::handleRename() const {
  if (!server->hasArg("path") || !server->hasArg("name")) {
    server->send(400, "text/plain", "Missing path or name");
    return;
  }

  String itemPath = server->arg("path");
  String newName = server->arg("name");
  newName.trim();

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Cannot rename root directory");
    return;
  }
  if (!itemPath.startsWith("/")) {
    itemPath = "/" + itemPath;
  }
  if (itemPath.length() > 1 && itemPath.endsWith("/")) {
    itemPath = itemPath.substring(0, itemPath.length() - 1);
  }

  if (newName.isEmpty() || newName.indexOf('/') != -1 || newName.indexOf('\\') != -1 || newName == "." ||
      newName == "..") {
    server->send(400, "text/plain", "Invalid name");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);

  if (itemName.startsWith(".")) {
    Serial.printf("[%lu] [WEB] Rename rejected - hidden/system item: %s\n", millis(), itemPath.c_str());
    server->send(403, "text/plain", "Cannot rename system files");
    return;
  }

  for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
    if (itemName.equals(HIDDEN_ITEMS[i])) {
      Serial.printf("[%lu] [WEB] Rename rejected - protected item: %s\n", millis(), itemPath.c_str());
      server->send(403, "text/plain", "Cannot rename protected items");
      return;
    }
  }

  if (!SdMan.exists(itemPath.c_str())) {
    Serial.printf("[%lu] [WEB] Rename failed - item not found: %s\n", millis(), itemPath.c_str());
    server->send(404, "text/plain", "Item not found");
    return;
  }

  const int lastSlash = itemPath.lastIndexOf('/');
  const String parentPath = lastSlash > 0 ? itemPath.substring(0, lastSlash) : String("");
  const String newPath = parentPath + "/" + newName;

  if (newPath == itemPath) {
    server->send(200, "text/plain", "Renamed successfully");
    return;
  }

  // FAT/exFAT lookups are case-insensitive, so a pure case change (Foo.epub -> FOO.epub) would otherwise
  // collide with itself here; only block on a genuine name clash.
  if (strcasecmp(newName.c_str(), itemName.c_str()) != 0 && SdMan.exists(newPath.c_str())) {
    server->send(409, "text/plain", "An item with that name already exists");
    return;
  }

  FsFile item = SdMan.open(itemPath.c_str());
  const bool isDir = item && item.isDirectory();
  if (item) {
    item.close();
  }

  std::vector<std::pair<std::string, std::string>> epubRenames;
  if (isDir) {
    collectEpubRenames(itemPath.c_str(), newPath.c_str(), epubRenames);
  } else if (isEpubFile(itemName)) {
    epubRenames.emplace_back(itemPath.c_str(), newPath.c_str());
  }

  Serial.printf("[%lu] [WEB] Renaming %s -> %s\n", millis(), itemPath.c_str(), newPath.c_str());

  if (!SdMan.rename(itemPath.c_str(), newPath.c_str())) {
    Serial.printf("[%lu] [WEB] Failed to rename: %s\n", millis(), itemPath.c_str());
    server->send(500, "text/plain", "Failed to rename item");
    return;
  }

  for (const auto& renamePair : epubRenames) {
    migrateEpubBookState(renamePair.first, renamePair.second);
  }

  Serial.printf("[%lu] [WEB] Successfully renamed: %s -> %s\n", millis(), itemPath.c_str(), newPath.c_str());
  server->send(200, "text/plain", "Renamed successfully");
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
  const bool clockAvailable = clockSettingsAvailable();
  const uint8_t sleepScreen = (!clockAvailable && SETTINGS.sleepScreen == SystemSetting::DATETIME)
                                  ? SystemSetting::LIGHT
                                  : SETTINGS.sleepScreen;

  doc["clockAvailable"] = clockAvailable;
  doc["sleepScreen"] = sleepScreen;
  doc["sleepScreenCoverMode"] = SETTINGS.sleepScreenCoverMode;
  doc["sleepScreenCoverFilter"] = SETTINGS.sleepScreenCoverFilter;
  doc["sleepImageQuality"] = SETTINGS.sleepImageQuality;
  doc["sleepScreenCoverGrayscale"] = SETTINGS.sleepImageQuality;
  doc["sleepImageTwoBit"] = SETTINGS.sleepImageQuality != SystemSetting::SLEEP_IMAGE_LOW;
  doc["sleepCustomBmp"] = SETTINGS.sleepCustomBmp;
  if (clockAvailable) {
    doc["sleepClockStyle"] = SETTINGS.sleepClockStyle;
    doc["sleepClockTimeFormat"] = SETTINGS.sleepClockTimeFormat;
    doc["timeZoneQuarterOffset"] = SETTINGS.timeZoneQuarterOffset;
  }
  doc["hideBatteryPercentage"] = SETTINGS.hideBatteryPercentage;
  doc["recentLibraryMode"] = SETTINGS.recentLibraryMode;
  doc["libraryMode"] = SETTINGS.libraryMode;
  doc["recentVisibleCount"] = SETTINGS.recentVisibleCount;
  doc["librarySortEnabled"] = SETTINGS.librarySortEnabled;
  doc["librarySortMode"] = SETTINGS.librarySortMode;

  doc["fontFamily"] = SETTINGS.fontFamily;
  doc["fontSize"] = SETTINGS.fontSize;

  doc["lineHeight"] = SETTINGS.lineHeight;
  doc["textSpace"] = SETTINGS.textSpace;
  doc["screenMargin"] = SETTINGS.screenMargin;
  doc["paragraphAlignment"] = SETTINGS.paragraphAlignment;
  doc["paragraphCssIndentEnabled"] = SETTINGS.paragraphCssIndentEnabled;
  doc["extraParagraphSpacing"] = SETTINGS.extraParagraphSpacing;
  doc["orientation"] = SETTINGS.orientation;
  doc["hyphenationEnabled"] = SETTINGS.hyphenationEnabled;
  doc["bionicReadingEnabled"] = SETTINGS.bionicReadingEnabled;

  doc["readerDirectionMapping"] = SETTINGS.readerDirectionMapping;
  doc["readerMenuButton"] = SETTINGS.readerMenuButton;
  doc["longPressChapterSkip"] = SETTINGS.longPressChapterSkip;
  doc["readerShortPwrBtn"] = SETTINGS.readerShortPwrBtn;
  doc["shakePageTurn"] = SETTINGS.shakePageTurn;
  doc["shakePageTurnSensitivity"] = SETTINGS.shakePageTurnSensitivity;

  doc["textAntiAliasing"] = SETTINGS.textAntiAliasing;
  doc["refreshFrequency"] = SETTINGS.refreshFrequency;
  doc["readerImageGrayscale"] = SETTINGS.readerImageGrayscale;
  doc["readerSmartRefreshOnImages"] = SETTINGS.readerSmartRefreshOnImages;
  doc["statusBar"] = SETTINGS.statusBar;
  doc["statusBarLeft"] = SETTINGS.statusBarLeft;
  doc["statusBarMiddle"] = SETTINGS.statusBarMiddle;
  doc["statusBarRight"] = SETTINGS.statusBarRight;

  doc["frontButtonLayout"] = SETTINGS.frontButtonLayout;
  doc["shortPwrBtn"] = SETTINGS.shortPwrBtn;

  doc["sleepTimeout"] = SETTINGS.sleepTimeout;
  doc["useLibraryIndex"] = SETTINGS.useLibraryIndex;
  doc["bootSetting"] = SETTINGS.bootSetting;

  doc["refreshOnLoadRecent"] = SETTINGS.refreshOnLoadRecent;
  doc["refreshOnLoadLibrary"] = SETTINGS.refreshOnLoadLibrary;
  doc["refreshOnLoadSettings"] = SETTINGS.refreshOnLoadSettings;
  doc["refreshOnLoadSync"] = SETTINGS.refreshOnLoadSync;
  doc["refreshOnLoadStatistics"] = SETTINGS.refreshOnLoadStatistics;
  doc["pageAutoTurnSeconds"] = SETTINGS.pageAutoTurnSeconds;
  doc["bitmapRoundedCorners"] = SETTINGS.bitmapRoundedCorners;
  doc["opdsServerUrl"] = SETTINGS.opdsServerUrl;
  doc["opdsUsername"] = SETTINGS.opdsUsername;
  doc["opdsPasswordSet"] = strlen(SETTINGS.opdsPassword) > 0;

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
  const bool clockAvailable = clockSettingsAvailable();

  for (JsonPair kv : doc.as<JsonObject>()) {
    const char* key = kv.key().c_str();
    int value = kv.value().as<int>();

    if (strcmp(key, "sleepScreen") == 0) {
      uint8_t v = static_cast<uint8_t>(value);
      if (v >= SystemSetting::SLEEP_SCREEN_MODE_COUNT || (!clockAvailable && v == SystemSetting::DATETIME)) {
        v = SystemSetting::LIGHT;
      }
      SETTINGS.sleepScreen = v;
      changed = true;
    } else if (strcmp(key, "sleepScreenCoverMode") == 0) {
      SETTINGS.sleepScreenCoverMode = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "sleepScreenCoverFilter") == 0) {
      SETTINGS.sleepScreenCoverFilter = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "sleepScreenCoverGrayscale") == 0) {
      SETTINGS.sleepImageQuality = (value >= 0 && value < SystemSetting::SLEEP_IMAGE_QUALITY_COUNT)
                                       ? static_cast<uint8_t>(value)
                                       : SystemSetting::SLEEP_IMAGE_LOW;
      changed = true;
    } else if (strcmp(key, "sleepImageQuality") == 0) {
      SETTINGS.sleepImageQuality = (value >= 0 && value < SystemSetting::SLEEP_IMAGE_QUALITY_COUNT)
                                       ? static_cast<uint8_t>(value)
                                       : SystemSetting::SLEEP_IMAGE_LOW;
      changed = true;
    } else if (strcmp(key, "sleepImageTwoBit") == 0) {
      SETTINGS.sleepImageQuality = (uint8_t)value ? SystemSetting::SLEEP_IMAGE_MEDIUM : SystemSetting::SLEEP_IMAGE_LOW;
      changed = true;
    } else if (strcmp(key, "sleepCustomBmp") == 0) {
      if (kv.value().isNull()) {
        SETTINGS.setSleepCustomBmpFromInput(nullptr);
      } else {
        SETTINGS.setSleepCustomBmpFromInput(kv.value().as<const char*>());
      }
      changed = true;
    } else if (clockAvailable && strcmp(key, "sleepClockStyle") == 0) {
      uint8_t v = static_cast<uint8_t>(value);
      if (v >= SystemSetting::SLEEP_CLOCK_STYLE_COUNT) v = SystemSetting::CLOCK_CENTERED_DATE;
      SETTINGS.sleepClockStyle = v;
      changed = true;
    } else if (clockAvailable && strcmp(key, "sleepClockTimeFormat") == 0) {
      uint8_t v = static_cast<uint8_t>(value);
      if (v >= SystemSetting::CLOCK_TIME_FORMAT_COUNT) v = SystemSetting::CLOCK_24_HOUR;
      SETTINGS.sleepClockTimeFormat = v;
      changed = true;
    } else if (clockAvailable && strcmp(key, "timeZoneQuarterOffset") == 0) {
      int v = static_cast<int>(value);
      if (v < 0) v = 0;
      if (v > 104) v = 104;
      SETTINGS.timeZoneQuarterOffset = static_cast<uint8_t>(v);
      changed = true;
    } else if (strcmp(key, "hideBatteryPercentage") == 0) {
      SETTINGS.hideBatteryPercentage = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "recentLibraryMode") == 0) {
      SETTINGS.recentLibraryMode = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "libraryMode") == 0) {
      uint8_t v = static_cast<uint8_t>(value);
      if (v >= SystemSetting::LIBRARY_MODE_COUNT) v = SystemSetting::LIBRARY_LIST;
      SETTINGS.libraryMode = v;
      changed = true;
    } else if (strcmp(key, "recentVisibleCount") == 0) {
      int v = static_cast<int>(value);
      if (v < 1) v = 1;
      if (v > 8) v = 8;
      SETTINGS.recentVisibleCount = static_cast<uint8_t>(v);
      changed = true;
    } else if (strcmp(key, "librarySortEnabled") == 0) {
      SETTINGS.librarySortEnabled = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "librarySortMode") == 0) {
      int v = static_cast<int>(value);
      if (v < 0) v = 0;
      if (v > 6) v = 0;
      SETTINGS.librarySortMode = static_cast<uint8_t>(v);
      changed = true;
    } else if (strcmp(key, "fontFamily") == 0) {
      SETTINGS.fontFamily = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "fontSize") == 0) {
      SETTINGS.fontSize = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "lineHeight") == 0) {
      uint8_t v = (uint8_t)value;
      SETTINGS.lineHeight = (v < 10 || v > 200) ? 100 : v;
      changed = true;
    } else if (strcmp(key, "textSpace") == 0) {
      uint8_t v = (uint8_t)value;
      SETTINGS.textSpace = (v < 10 || v > 200) ? 100 : v;
      changed = true;
    } else if (strcmp(key, "screenMargin") == 0) {
      SETTINGS.screenMargin = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "paragraphAlignment") == 0) {
      SETTINGS.paragraphAlignment = (uint8_t)value;
      if (SETTINGS.paragraphAlignment >= SystemSetting::PARAGRAPH_ALIGNMENT_COUNT) {
        SETTINGS.paragraphAlignment = SystemSetting::JUSTIFIED;
      }
      changed = true;
    } else if (strcmp(key, "extraParagraphSpacing") == 0) {
      SETTINGS.extraParagraphSpacing = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "paragraphCssIndentEnabled") == 0) {
      SETTINGS.paragraphCssIndentEnabled = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "orientation") == 0) {
      SETTINGS.orientation = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "hyphenationEnabled") == 0) {
      SETTINGS.hyphenationEnabled = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "bionicReadingEnabled") == 0) {
      SETTINGS.bionicReadingEnabled = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "readerDirectionMapping") == 0) {
      SETTINGS.readerDirectionMapping = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "readerMenuButton") == 0) {
      uint8_t v = (uint8_t)value;
      if (v >= SystemSetting::READER_MENU_BUTTON_COUNT) {
        v = SystemSetting::MENU_UP;
      }
      SETTINGS.readerMenuButton = v;
      changed = true;
    } else if (strcmp(key, "longPressChapterSkip") == 0) {
      const int v = static_cast<int>(value);
      SETTINGS.longPressChapterSkip =
          (v < 0) ? 0
                  : (v > SystemSetting::LONG_PRESS_PAGE_SKIP_5 ? SystemSetting::LONG_PRESS_PAGE_SKIP_5 : (uint8_t)v);
      changed = true;
    } else if (strcmp(key, "readerShortPwrBtn") == 0) {
      SETTINGS.readerShortPwrBtn = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "shakePageTurn") == 0) {
      const int motionMode = static_cast<int>(value);
      SETTINGS.shakePageTurn = static_cast<uint8_t>(motionMode < 0 ? 0 : motionMode > 2 ? 2 : motionMode);
      changed = true;
    } else if (strcmp(key, "shakePageTurnSensitivity") == 0) {
      const int sensitivity = static_cast<int>(value);
      SETTINGS.shakePageTurnSensitivity =
          static_cast<uint8_t>(sensitivity < 0 ? 0 : sensitivity > 2 ? 2 : sensitivity);
      changed = true;
    } else if (strcmp(key, "textAntiAliasing") == 0) {
      SETTINGS.textAntiAliasing = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "refreshFrequency") == 0) {
      SETTINGS.refreshFrequency = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "readerImageGrayscale") == 0) {
      SETTINGS.readerImageGrayscale = (value >= 0 && value < SystemSetting::READER_IMAGE_QUALITY_COUNT)
                                          ? (uint8_t)value
                                          : SystemSetting::READER_IMAGE_LOW;
      changed = true;
    } else if (strcmp(key, "readerSmartRefreshOnImages") == 0) {
      SETTINGS.readerSmartRefreshOnImages = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "statusBar") == 0) {
      SETTINGS.statusBar = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "statusBarLeft") == 0) {
      SETTINGS.statusBarLeft = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "statusBarMiddle") == 0) {
      SETTINGS.statusBarMiddle = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "statusBarRight") == 0) {
      SETTINGS.statusBarRight = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "frontButtonLayout") == 0) {
      SETTINGS.frontButtonLayout = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "shortPwrBtn") == 0) {
      SETTINGS.shortPwrBtn = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "sleepTimeout") == 0) {
      SETTINGS.sleepTimeout = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "useLibraryIndex") == 0) {
      SETTINGS.useLibraryIndex = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "bootSetting") == 0) {
      SETTINGS.bootSetting = (uint8_t)value;
      changed = true;
    } else if (strcmp(key, "refreshOnLoadRecent") == 0) {
      SETTINGS.refreshOnLoadRecent = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "refreshOnLoadLibrary") == 0) {
      SETTINGS.refreshOnLoadLibrary = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "refreshOnLoadSettings") == 0) {
      SETTINGS.refreshOnLoadSettings = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "refreshOnLoadSync") == 0) {
      SETTINGS.refreshOnLoadSync = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "refreshOnLoadStatistics") == 0) {
      SETTINGS.refreshOnLoadStatistics = (uint8_t)value ? 1 : 0;
      changed = true;
    } else if (strcmp(key, "pageAutoTurnSeconds") == 0) {
      int v = static_cast<int>(value);
      if (v < 0) v = 0;
      if (v > 180) v = 180;
      v = (v / 10) * 10;
      SETTINGS.pageAutoTurnSeconds = static_cast<uint8_t>(v);
      changed = true;
    } else if (strcmp(key, "bitmapRoundedCorners") == 0) {
      int cornerStyle = static_cast<int>(value);
      if (cornerStyle < 0) cornerStyle = 0;
      if (cornerStyle > 2) cornerStyle = 2;
      SETTINGS.bitmapRoundedCorners = static_cast<uint8_t>(cornerStyle);
      changed = true;
    } else if (strcmp(key, "opdsServerUrl") == 0) {
      copySettingString(SETTINGS.opdsServerUrl, sizeof(SETTINGS.opdsServerUrl), kv.value().as<const char*>());
      changed = true;
    } else if (strcmp(key, "opdsUsername") == 0) {
      copySettingString(SETTINGS.opdsUsername, sizeof(SETTINGS.opdsUsername), kv.value().as<const char*>());
      changed = true;
    } else if (strcmp(key, "opdsPassword") == 0) {
      copySettingString(SETTINGS.opdsPassword, sizeof(SETTINGS.opdsPassword), kv.value().as<const char*>());
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
  String password = doc["password"].is<const char*>() ? (doc["password"] | "") : KOREADER_STORE.getPassword().c_str();
  String serverUrl = doc["serverUrl"] | "";
  int matchMethod = doc["matchMethod"] | 0;

  KOREADER_STORE.setCredentials(username.c_str(), password.c_str());
  KOREADER_STORE.setServerUrl(serverUrl.c_str());
  KOREADER_STORE.setMatchMethod((DocumentMatchMethod)matchMethod);
  KOREADER_STORE.saveToFile();

  server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void LocalServer::handleFontsRescan() const {
#ifdef INX_SIMULATOR_WEB_ONLY
  server->send(501, "application/json", "{\"ok\":false,\"error\":\"unavailable_in_simulator\"}");
#else
  if (!SdMan.ready()) {
    server->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_unavailable\"}");
    return;
  }
  const bool ok = FontManager::scanSDFonts("/fonts", true);
  if (ok) {
    server->send(200, "application/json", "{\"ok\":true}");
  } else {
    server->send(500, "application/json", "{\"ok\":false,\"error\":\"scan_failed\"}");
  }
#endif
}

#ifndef INX_SIMULATOR_WEB_ONLY
void LocalServer::handleOpdsGet() const {
  JsonDocument doc;
  const auto& servers = OPDS_STORE.getAllServers();
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& srv : servers) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = srv.name;
    obj["url"] = srv.url;
    obj["username"] = srv.username;
  }
  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void LocalServer::handleOpdsPost() const {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON");
    return;
  }

  JsonDocument doc;
  deserializeJson(doc, server->arg("plain"));
  String name = doc["name"];
  String url = doc["url"];
  String username = doc["username"] | "";
  String password = doc["password"] | "";

  if (name.length() == 0 || url.length() == 0) {
    server->send(400, "text/plain", "Name and URL are required");
    return;
  }

  if (OPDS_STORE.addServer(name.c_str(), url.c_str(), username.c_str(), password.c_str())) {
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server->send(500, "text/plain", "Failed to save");
  }
}

void LocalServer::handleOpdsDelete() const {
  String uri = server->uri();
  int lastSlash = uri.lastIndexOf('/');
  String name = uri.substring(lastSlash + 1);
  name.replace("%20", " ");

  if (OPDS_STORE.removeServer(name.c_str())) {
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server->send(404, "text/plain", "Not found");
  }
}
#endif
