# OPDS Server Multi-Server Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add multi-OPDS-server support with web UI management and on-device server selection, modeled after the WiFi networks feature.

**Architecture:** New `OpdsServerStore` singleton for persistence (parallel to `WifiCredentialStore`), web API endpoints under `/api/opds`, web UI section in `SettingsPage.html`, new `OpdsServerListActivity` for on-device server picking, and `OpdsBookBrowserActivity` refactored to accept per-server credentials.

**Tech Stack:** C++2a (Arduino/ESP32-C3), ArduinoJson 7.4.2, SDCardManager via SdFat, PlatformIO

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `src/state/OpdsServerStore.h` | Create | Struct + singleton store declaration |
| `src/state/OpdsServerStore.cpp` | Create | Store implementation (save/load/add/remove) |
| `src/network/LocalServer.h` | Modify | Add OPDS API handler declarations |
| `src/network/LocalServer.cpp` | Modify | Add OPDS route registration + handlers |
| `src/network/html/SettingsPage.html` | Modify | Add "OPDS Servers" web UI section |
| `src/activity/browser/OpdsBookBrowserActivity.h` | Modify | Add server-params constructor |
| `src/activity/browser/OpdsBookBrowserActivity.cpp` | Modify | Read from params instead of SETTINGS |
| `src/activity/OpdsServerListActivity.h` | Create | On-device server picker header |
| `src/activity/OpdsServerListActivity.cpp` | Create | On-device server picker implementation |
| `src/activity/settings/CategorySettingsActivity.cpp` | Modify | Wire OPDS action to server list |
| `src/network/HttpDownloader.h` | Modify | Add credential-aware overloads |
| `src/network/HttpDownloader.cpp` | Modify | Accept per-request credentials |

---

### Task 1: Create OpdsServerEntry struct and OpdsServerStore

**Files:**
- Create: `src/state/OpdsServerStore.h`
- Create: `src/state/OpdsServerStore.cpp`

**Reference:** `src/state/NetworkCredential.h` and `src/state/NetworkCredential.cpp` — exact same pattern.

- [ ] **Step 1: Create `src/state/OpdsServerStore.h`**

```cpp
#pragma once

#include <string>
#include <vector>

struct OpdsServerEntry {
  std::string name;
  std::string url;
  std::string username;
  std::string password;
};

class OpdsServerStore {
 private:
  static OpdsServerStore instance;
  std::vector<OpdsServerEntry> servers;

  static constexpr size_t MAX_SERVERS = 8;

  OpdsServerStore() = default;
  void obfuscate(std::string& data) const;

 public:
  OpdsServerStore(const OpdsServerStore&) = delete;
  OpdsServerStore& operator=(const OpdsServerStore&) = delete;

  static OpdsServerStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();

  bool addServer(const std::string& name, const std::string& url,
                 const std::string& username, const std::string& password);
  bool removeServer(const std::string& name);
  const OpdsServerEntry* findServer(const std::string& name) const;

  const std::vector<OpdsServerEntry>& getAllServers() const { return servers; }
};

#define OPDS_STORE OpdsServerStore::getInstance()
```

- [ ] **Step 2: Create `src/state/OpdsServerStore.cpp`**

```cpp
#include "state/OpdsServerStore.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

OpdsServerStore OpdsServerStore::instance;

namespace {

constexpr uint8_t OPDS_FILE_VERSION = 1;

constexpr char OPDS_FILE[] = "/.system/opds_servers.bin";

constexpr uint8_t OBFUSCATION_KEY[] = {0x43, 0x72, 0x6F, 0x73, 0x73, 0x50, 0x6F, 0x69, 0x6E, 0x74};
constexpr size_t KEY_LENGTH = sizeof(OBFUSCATION_KEY);
}

void OpdsServerStore::obfuscate(std::string& data) const {
  for (size_t i = 0; i < data.size(); i++) {
    data[i] ^= OBFUSCATION_KEY[i % KEY_LENGTH];
  }
}

bool OpdsServerStore::saveToFile() const {
  SdMan.mkdir("/.system");

  FsFile file;
  if (!SdMan.openFileForWrite("OSS", OPDS_FILE, file)) {
    return false;
  }

  serialization::writePod(file, OPDS_FILE_VERSION);
  serialization::writePod(file, static_cast<uint8_t>(servers.size()));

  for (const auto& srv : servers) {
    serialization::writeString(file, srv.name);
    serialization::writeString(file, srv.url);
    serialization::writeString(file, srv.username);

    std::string obfuscatedPwd = srv.password;
    obfuscate(obfuscatedPwd);
    serialization::writeString(file, obfuscatedPwd);
  }

  file.close();
  Serial.printf("[%lu] [OSS] Saved %zu OPDS servers to file\n", millis(), servers.size());
  return true;
}

bool OpdsServerStore::loadFromFile() {
  FsFile file;
  if (!SdMan.openFileForRead("OSS", OPDS_FILE, file)) {
    servers.clear();
    return false;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version != OPDS_FILE_VERSION) {
    Serial.printf("[%lu] [OSS] Unknown file version: %u\n", millis(), version);
    file.close();
    return false;
  }

  uint8_t count;
  serialization::readPod(file, count);

  servers.clear();
  for (uint8_t i = 0; i < count && i < MAX_SERVERS; i++) {
    OpdsServerEntry srv;

    serialization::readString(file, srv.name);
    serialization::readString(file, srv.url);
    serialization::readString(file, srv.username);
    serialization::readString(file, srv.password);
    obfuscate(srv.password);

    servers.push_back(srv);
  }

  file.close();
  Serial.printf("[%lu] [OSS] Loaded %zu OPDS servers from file\n", millis(), servers.size());
  return true;
}

bool OpdsServerStore::addServer(const std::string& name, const std::string& url,
                                const std::string& username, const std::string& password) {
  auto existing = find_if(servers.begin(), servers.end(),
                          [&name](const OpdsServerEntry& s) { return s.name == name; });
  if (existing != servers.end()) {
    existing->url = url;
    existing->username = username;
    existing->password = password;
    Serial.printf("[%lu] [OSS] Updated server: %s\n", millis(), name.c_str());
    return saveToFile();
  }

  if (servers.size() >= MAX_SERVERS) {
    Serial.printf("[%lu] [OSS] Cannot add more servers, limit of %zu reached\n", millis(), MAX_SERVERS);
    return false;
  }

  servers.push_back({name, url, username, password});
  Serial.printf("[%lu] [OSS] Added server: %s\n", millis(), name.c_str());
  return saveToFile();
}

bool OpdsServerStore::removeServer(const std::string& name) {
  auto existing = find_if(servers.begin(), servers.end(),
                          [&name](const OpdsServerEntry& s) { return s.name == name; });
  if (existing != servers.end()) {
    servers.erase(existing);
    Serial.printf("[%lu] [OSS] Removed server: %s\n", millis(), name.c_str());
    return saveToFile();
  }
  return false;
}

const OpdsServerEntry* OpdsServerStore::findServer(const std::string& name) const {
  auto existing = find_if(servers.begin(), servers.end(),
                          [&name](const OpdsServerEntry& s) { return s.name == name; });
  if (existing != servers.end()) {
    return &*existing;
  }
  return nullptr;
}
```

- [ ] **Step 3: Verify the files compile (do a build check)**

Run: `pio run -t check` (or `pio run`)

---

### Task 2: Add OPDS API handlers to LocalServer

**Files:**
- Modify: `src/network/LocalServer.h`
- Modify: `src/network/LocalServer.cpp`

- [ ] **Step 1: Add handler declarations to `LocalServer.h`** (after line 102, after `handleKOReaderPost`)

Add:
```cpp
  void handleOpdsGet() const;
  void handleOpdsPost() const;
  void handleOpdsDelete() const;
```

- [ ] **Step 2: Add routes in `LocalServer.cpp`** (after line 318, after KOReader routes)

Add:
```cpp
  server->on("/api/opds", HTTP_GET, [this] { handleOpdsGet(); });
  server->on("/api/opds", HTTP_POST, [this] { handleOpdsPost(); });
  server->on("/api/opds/*", HTTP_DELETE, [this] { handleOpdsDelete(); });
```

- [ ] **Step 3: Add include at top of `LocalServer.cpp`** (after line 33, after NetworkCredential.h)

Add:
```cpp
#include "state/OpdsServerStore.h"
```

- [ ] **Step 4: Add handler implementations** (after `handleFontsRescan()`, before end of file)

```cpp
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
```

---

### Task 3: Add OPDS Servers section to web UI

**Files:**
- Modify: `src/network/html/SettingsPage.html`

- [ ] **Step 1: Add HTML section** (after the WiFi Networks section closing `</div>` at line 219, before the KOReader Sync section at line 220)

Insert:
```html
    <div class="settings-section">
      <div class="section-header"><h3>OPDS Servers</h3></div>
      <div class="wifi-list" id="opdsList"><div class="setting-item"><div><div class="setting-label">Loading...</div></div></div></div>
      <button class="btn-add" onclick="showAddOpdsForm()">+ Add OPDS Server</button>
      <div class="wifi-form" id="addOpdsForm">
        <input id="newOpdsName" placeholder="Server Name" autocomplete="off">
        <input id="newOpdsUrl" placeholder="Server URL (e.g. http://192.168.1.100/opds)" autocomplete="off">
        <input id="newOpdsUsername" placeholder="Username (optional)" autocomplete="off">
        <input id="newOpdsPassword" type="password" placeholder="Password (optional)">
        <div class="wifi-form-buttons">
          <button class="btn-primary" onclick="saveOpdsServer()">Save</button>
          <button class="btn-secondary" onclick="hideAddOpdsForm()">Cancel</button>
        </div>
      </div>
    </div>
```

- [ ] **Step 2: Add load call in `loadSettings()`** (after line 433, `await loadKOReaderSettings();`)

Add:
```js
    await loadOpdsServers();
```

- [ ] **Step 3: Add JavaScript functions** (after `removeWifiNetwork()` at line 532, before `saveKOReaderSettings()` at line 534)

Insert:
```js
async function loadOpdsServers() {
  try {
    const r = await fetch("/api/opds");
    const servers = await r.json();
    renderOpdsList(servers);
  } catch (e) {
    console.error(e);
    renderOpdsList([]);
  }
}

function renderOpdsList(servers) {
  const n = document.getElementById("opdsList");
  if (!n) return;
  if (servers.length) {
    n.innerHTML = "";
    servers.forEach(function (s) {
      const t = document.createElement("div");
      t.className = "wifi-item";
      t.innerHTML = '<div><span class="wifi-ssid">' + escapeHtml(s.name) + '</span><br><span style="font-size:12px;color:#888;">' + escapeHtml(s.url) + '</span></div><div class="wifi-actions"><button type="button" class="btn-remove" onclick="removeOpdsServer(\'' + escapeHtml(s.name).replace(/'/g, "\\'") + '\')">Remove</button></div>';
      n.appendChild(t);
    });
  } else {
    n.innerHTML = '<div class="setting-item"><div><div class="setting-label">No saved servers</div></div></div>';
  }
}

function showAddOpdsForm() {
  document.getElementById("addOpdsForm").classList.add("show");
}

function hideAddOpdsForm() {
  document.getElementById("addOpdsForm").classList.remove("show");
  document.getElementById("newOpdsName").value = "";
  document.getElementById("newOpdsUrl").value = "";
  document.getElementById("newOpdsUsername").value = "";
  document.getElementById("newOpdsPassword").value = "";
}

async function saveOpdsServer() {
  const name = document.getElementById("newOpdsName").value.trim();
  const url = document.getElementById("newOpdsUrl").value.trim();
  if (!name || !url) {
    showToast("Enter server name and URL", true);
    return;
  }
  try {
    const ok = (await fetch("/api/opds", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        name: name,
        url: url,
        username: document.getElementById("newOpdsUsername").value || "",
        password: document.getElementById("newOpdsPassword").value || ""
      })
    })).ok;
    if (ok) {
      showToast("Saved " + name);
      hideAddOpdsForm();
      await loadOpdsServers();
    } else showToast("Failed to save server", true);
  } catch (e) {
    showToast("Error saving server", true);
  }
}

async function removeOpdsServer(name) {
  if (!confirm('Remove "' + name + '"?')) return;
  try {
    const ok = (await fetch("/api/opds/" + encodeURIComponent(name), { method: "DELETE" })).ok;
    if (ok) {
      showToast("Removed " + name);
      await loadOpdsServers();
    } else showToast("Failed to remove server", true);
  } catch (e) {
    showToast("Error removing server", true);
  }
}
```

---

### Task 4: Add credential-aware overloads to HttpDownloader

**Files:**
- Modify: `src/network/HttpDownloader.h`
- Modify: `src/network/HttpDownloader.cpp`

HttpDownloader currently reads credentials from `SETTINGS.opdsUsername`/`SETTINGS.opdsPassword` internally.
We need overloads that accept per-request credentials so the multi-server OpdsBookBrowserActivity can use the right credentials per server.

- [ ] **Step 1: Add credential-aware overload declarations to `HttpDownloader.h`**

After line 36 (the `fetchUrl` taking `Stream&`), add:
```cpp
  static bool fetchUrl(const std::string& url, std::string& outContent,
                       const std::string& username, const std::string& password);

  static bool fetchUrl(const std::string& url, Stream& stream,
                       const std::string& username, const std::string& password);
```

After line 46 (the `downloadToFile` declaration), add:
```cpp
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      const std::string& username, const std::string& password,
                                      ProgressCallback progress = nullptr);
```

- [ ] **Step 2: Add a private static helper method declaration** (after `DOWNLOAD_CHUNK_SIZE`)

```cpp
  static void addBasicAuth(HTTPClient& http, const std::string& username, const std::string& password);
```

- [ ] **Step 3: Update `HttpDownloader.cpp` to add the helper and credential-aware overloads**

Add the helper implementation at the top of the file (after includes):
```cpp
void HttpDownloader::addBasicAuth(HTTPClient& http, const std::string& username, const std::string& password) {
  if (!username.empty() && !password.empty()) {
    std::string credentials = username + ":" + password;
    String encoded = base64::encode(credentials.c_str());
    http.addHeader("Authorization", "Basic " + encoded);
  }
}
```

Replace the existing `fetchUrl(url, stream)` implementation (lines 21-59) to use the helper:
```cpp
bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent) {
  std::string user = SETTINGS.opdsUsername;
  std::string pass = SETTINGS.opdsPassword;
  return fetchUrl(url, outContent, user, pass);
}
```

Add the credential-aware overload:
```cpp
bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent,
                              const std::string& username, const std::string& password) {
  std::unique_ptr<WiFiClient> client;
  if (UrlUtils::isHttpsUrl(url)) {
    auto* secureClient = new WiFiClientSecure();
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new WiFiClient());
  }
  HTTPClient http;

  Serial.printf("[%lu] [HTTP] Fetching: %s\n", millis(), url.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" INX_VERSION);

  addBasicAuth(http, username, password);

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[%lu] [HTTP] Fetch failed: %d\n", millis(), httpCode);
    http.end();
    return false;
  }

  http.writeToStream(&outContent);

  http.end();

  Serial.printf("[%lu] [HTTP] Fetch success\n", millis());
  return true;
}
```

Replace the existing `downloadToFile` (lines 70-178) to use the helper:
```cpp
HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress) {
  std::string user = SETTINGS.opdsUsername;
  std::string pass = SETTINGS.opdsPassword;
  return downloadToFile(url, destPath, user, pass, progress);
}
```

Add the credential-aware overload:
```cpp
HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             const std::string& username, const std::string& password,
                                                             ProgressCallback progress) {
  std::unique_ptr<WiFiClient> client;
  if (UrlUtils::isHttpsUrl(url)) {
    auto* secureClient = new WiFiClientSecure();
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new WiFiClient());
  }
  HTTPClient http;

  Serial.printf("[%lu] [HTTP] Downloading: %s\n", millis(), url.c_str());
  Serial.printf("[%lu] [HTTP] Destination: %s\n", millis(), destPath.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" INX_VERSION);

  addBasicAuth(http, username, password);

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[%lu] [HTTP] Download failed: %d\n", millis(), httpCode);
    http.end();
    return HTTP_ERROR;
  }

  const size_t contentLength = http.getSize();
  Serial.printf("[%lu] [HTTP] Content-Length: %zu\n", millis(), contentLength);

  if (SdMan.exists(destPath.c_str())) {
    SdMan.remove(destPath.c_str());
  }

  FsFile file;
  if (!SdMan.openFileForWrite("HTTP", destPath.c_str(), file)) {
    Serial.printf("[%lu] [HTTP] Failed to open file for writing\n", millis());
    http.end();
    return FILE_ERROR;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (!stream) {
    Serial.printf("[%lu] [HTTP] Failed to get stream\n", millis());
    file.close();
    SdMan.remove(destPath.c_str());
    http.end();
    return HTTP_ERROR;
  }

  uint8_t buffer[DOWNLOAD_CHUNK_SIZE];
  size_t downloaded = 0;
  const size_t total = contentLength > 0 ? contentLength : 0;

  while (http.connected() && (contentLength == 0 || downloaded < contentLength)) {
    const size_t available = stream->available();
    if (available == 0) {
      delay(1);
      continue;
    }

    const size_t toRead = available < DOWNLOAD_CHUNK_SIZE ? available : DOWNLOAD_CHUNK_SIZE;
    const size_t bytesRead = stream->readBytes(buffer, toRead);

    if (bytesRead == 0) {
      break;
    }

    const size_t written = file.write(buffer, bytesRead);
    if (written != bytesRead) {
      Serial.printf("[%lu] [HTTP] Write failed: wrote %zu of %zu bytes\n", millis(), written, bytesRead);
      file.close();
      SdMan.remove(destPath.c_str());
      http.end();
      return FILE_ERROR;
    }

    downloaded += bytesRead;

    if (progress && total > 0) {
      progress(downloaded, total);
    }
  }

  file.close();
  http.end();

  Serial.printf("[%lu] [HTTP] Downloaded %zu bytes\n", millis(), downloaded);

  if (contentLength > 0 && downloaded != contentLength) {
    Serial.printf("[%lu] [HTTP] Size mismatch: got %zu, expected %zu\n", millis(), downloaded, contentLength);
    SdMan.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  return OK;
}
```

Also remove the old inline credential code from `fetchUrl(url, stream)` and the old `downloadToFile`. The helper `addBasicAuth` replaces it.

Also add `#include <HTTPClient.h>` if not already present at the top.

- [ ] **Step 4: Add `HttpDownloader.h` include in `OpdsBookBrowserActivity.cpp`** if not already there

---

### Task 5: Refactor OpdsBookBrowserActivity to accept server params

**Files:**
- Modify: `src/activity/browser/OpdsBookBrowserActivity.h`
- Modify: `src/activity/browser/OpdsBookBrowserActivity.cpp`

- [ ] **Step 1: Add server params constructor + fields to header** (in `OpdsBookBrowserActivity.h`)

After the existing constructor (line 35-37), add:
```cpp
  explicit OpdsBookBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::function<void()>& onGoToRecent,
                                   const std::string& serverUrl,
                                   const std::string& serverUsername,
                                   const std::string& serverPassword)
      : ActivityWithSubactivity("OpdsBookBrowser", renderer, mappedInput),
        onGoToRecent(onGoToRecent),
        serverUrl(serverUrl),
        serverUsername(serverUsername),
        serverPassword(serverPassword) {}
```

After the `onGoToRecent` member (line 58), add:
```cpp
  std::string serverUrl;
  std::string serverUsername;
  std::string serverPassword;
```

- [ ] **Step 2: Update `OpdsBookBrowserActivity.cpp` to use server params**

In `fetchFeed()` (around line 261-304), change the entire method to use member params:
```cpp
void OpdsBookBrowserActivity::fetchFeed(const std::string& path) {
  const char* activeUrl = serverUrl.c_str();
  if (activeUrl[0] == '\0') {
    activeUrl = SETTINGS.opdsServerUrl;
  }
  if (strlen(activeUrl) == 0) {
    state = BrowserState::ERROR;
    errorMessage = "No server URL configured";
    updateRequired = true;
    return;
  }

  std::string fullUrl = UrlUtils::buildUrl(activeUrl, path);
  Serial.printf("[%lu] [OPDS] Fetching: %s\n", millis(), fullUrl.c_str());

  std::string user = serverUsername.empty() ? SETTINGS.opdsUsername : serverUsername;
  std::string pass = serverPassword.empty() ? SETTINGS.opdsPassword : serverPassword;

  OpdsParser parser;

  {
    OpdsParserStream stream{parser};
    if (!HttpDownloader::fetchUrl(fullUrl, stream, user, pass)) {
      state = BrowserState::ERROR;
      errorMessage = "Failed to fetch feed";
      updateRequired = true;
      return;
    }
  }

  if (!parser) {
    state = BrowserState::ERROR;
    errorMessage = "Failed to parse feed";
    updateRequired = true;
    return;
  }

  entries = std::move(parser).getEntries();
  Serial.printf("[%lu] [OPDS] Found %d entries\n", millis(), entries.size());
  selectorIndex = 0;

  if (entries.empty()) {
    state = BrowserState::ERROR;
    errorMessage = "No entries found";
    updateRequired = true;
    return;
  }

  state = BrowserState::BROWSING;
  updateRequired = true;
}
```

In `downloadBook()` (around line 340), change the server URL and download call:
```cpp
void OpdsBookBrowserActivity::downloadBook(const OpdsEntry& book) {
  state = BrowserState::DOWNLOADING;
  statusMessage = book.title;
  downloadProgress = 0;
  downloadTotal = 0;
  updateRequired = true;

  const char* activeUrl = serverUrl.c_str();
  if (activeUrl[0] == '\0') {
    activeUrl = SETTINGS.opdsServerUrl;
  }
  std::string downloadUrl = UrlUtils::buildUrl(activeUrl, book.href);

  std::string baseName = book.title;
  if (!book.author.empty()) {
    baseName += " - " + book.author;
  }
  std::string filename = "/" + StringUtils::sanitizeFilename(baseName) + ".epub";

  Serial.printf("[%lu] [OPDS] Downloading: %s -> %s\n", millis(), downloadUrl.c_str(), filename.c_str());

  std::string user = serverUsername.empty() ? SETTINGS.opdsUsername : serverUsername;
  std::string pass = serverPassword.empty() ? SETTINGS.opdsPassword : serverPassword;

  const auto result =
      HttpDownloader::downloadToFile(downloadUrl, filename, user, pass,
                                     [this](const size_t downloaded, const size_t total) {
        downloadProgress = downloaded;
        downloadTotal = total;
        updateRequired = true;
      });

  if (result == HttpDownloader::OK) {
    // ... rest unchanged
```

---

### Task 6: Create OpdsServerListActivity (on-device server picker)

**Files:**
- Create: `src/activity/OpdsServerListActivity.h`
- Create: `src/activity/OpdsServerListActivity.cpp`

This follows the exact same pattern as `CalibreSettingsActivity` but shows a list of saved OPDS servers from the store, and launches `OpdsBookBrowserActivity` when one is selected.

- [ ] **Step 1: Create `src/activity/OpdsServerListActivity.h`**

```cpp
#pragma once

#include <GfxRenderer.h>
#include <functional>

#include "../ActivityWithSubactivity.h"

class OpdsServerListActivity final : public ActivityWithSubactivity {
 public:
  explicit OpdsServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  const std::function<void()>& onBack)
      : ActivityWithSubactivity("OpdsServerList", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  int selectedIndex = 0;
  int serverCount = 0;
  const std::function<void()> onBack;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render();
  void handleSelection();
};
```

- [ ] **Step 2: Create `src/activity/OpdsServerListActivity.cpp`**

```cpp
#include "OpdsServerListActivity.h"

#include <GfxRenderer.h>

#include "browser/OpdsBookBrowserActivity.h"
#include "state/OpdsServerStore.h"

void OpdsServerListActivity::taskTrampoline(void* param) {
  auto* self = static_cast<OpdsServerListActivity*>(param);
  self->displayTaskLoop();
}

void OpdsServerListActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  OPDS_STORE.loadFromFile();
  serverCount = OPDS_STORE.getAllServers().size();
  updateRequired = true;

  xTaskCreate(&OpdsServerListActivity::taskTrampoline, "OpdsServerListTask",
              4096, this, 1, &displayTaskHandle);
}

void OpdsServerListActivity::onExit() {
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  if (renderingMutex) {
    xSemaphoreGive(renderingMutex);
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
}

void OpdsServerListActivity::loop() {
  ActivityWithSubactivity::loop();

  if (!renderingMutex) return;

  if (mappedInput.wasPressed(MenuNav::back())) {
    exitActivity();
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MenuNav::itemNext()) || mappedInput.wasPressed(MenuNav::tabNext())) {
    if (serverCount > 0) {
      selectedIndex = (selectedIndex + 1) % serverCount;
      updateRequired = true;
    }
  }

  if (mappedInput.wasPressed(MenuNav::itemPrev()) || mappedInput.wasPressed(MenuNav::tabPrev())) {
    if (serverCount > 0) {
      selectedIndex = (selectedIndex - 1 + serverCount) % serverCount;
      updateRequired = true;
    }
  }

  if (mappedInput.wasPressed(MenuNav::confirm()) || mappedInput.wasPressed(MenuNav::itemActivate())) {
    handleSelection();
  }

  if (updateRequired && !subActivity) {
    updateRequired = false;
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    render();
    xSemaphoreGive(renderingMutex);
  }
}

void OpdsServerListActivity::handleSelection() {
  if (serverCount <= 0) return;

  const auto& servers = OPDS_STORE.getAllServers();
  if (selectedIndex >= (int)servers.size()) return;

  const auto& srv = servers[selectedIndex];

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  exitActivity();
  enterNewActivity(new OpdsBookBrowserActivity(
      renderer, mappedInput,
      [this] {
        exitActivity();
        updateRequired = true;
      },
      srv.url, srv.username, srv.password));
  xSemaphoreGive(renderingMutex);
}

void OpdsServerListActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void OpdsServerListActivity::render() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 15, "OPDS Server", true, EpdFontFamily::BOLD);

  const auto& servers = OPDS_STORE.getAllServers();
  serverCount = servers.size();

  if (serverCount == 0) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 80,
                           "No servers configured", false);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 100,
                           "Add servers via the web interface", false);
  } else {
    if (selectedIndex >= 0 && selectedIndex < serverCount) {
      renderer.rectangle.fill(0, 70 + selectedIndex * 30 - 2, pageWidth - 1, 30,
                              static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    for (int i = 0; i < serverCount; i++) {
      const int y = 70 + i * 30;
      const bool isSelected = (i == selectedIndex);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, y,
                           servers[i].name.c_str(), !isSelected);
    }
  }

  const auto labels = mappedInput.mapLabels(serverCount > 0 ? "« Back" : "« Back",
                                            serverCount > 0 ? "Browse" : "",
                                            "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID,
                          labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
```

---

### Task 7: Wire OPDS action to the new server list activity

**Files:**
- Modify: `src/activity/settings/CategorySettingsActivity.cpp`

- [ ] **Step 1: Add include at top of `CategorySettingsActivity.cpp`**

Find the existing includes block and add:
```cpp
#include "../OpdsServerListActivity.h"
```

- [ ] **Step 2: Change the OPDS Browser action handler** (in the `ACTION` handling block, find the `"OPDS Browser"` match)

Change:
```cpp
if (strcmp(setting.name, "OPDS Browser") == 0) {
    exitActivity();
    enterNewActivity(new CalibreSettingsActivity(renderer, mappedInput, [this] {
        exitActivity();
        updateRequired = true;
    }));
}
```
To:
```cpp
if (strcmp(setting.name, "OPDS Browser") == 0) {
    exitActivity();
    enterNewActivity(new OpdsServerListActivity(renderer, mappedInput, [this] {
        exitActivity();
        updateRequired = true;
    }));
}
```

---

### Task 8: Build and verify

- [ ] **Step 1: Generate the HTML header**

Run: `python scripts/build_html.py`

Expected: `src/network/html/SettingsPageHtml.generated.h` is created/updated.

- [ ] **Step 2: Build the firmware**

Run: `pio run`

Expected: Compiles without errors. New files `OpdsServerStore.cpp` and `OpdsServerListActivity.cpp` are auto-discovered by PlatformIO.

- [ ] **Step 3: Commit all changes**

```bash
git add src/state/OpdsServerStore.h src/state/OpdsServerStore.cpp
git add src/network/LocalServer.h src/network/LocalServer.cpp
git add src/network/html/SettingsPage.html
git add src/activity/OpdsServerListActivity.h src/activity/OpdsServerListActivity.cpp
git add src/activity/browser/OpdsBookBrowserActivity.h src/activity/browser/OpdsBookBrowserActivity.cpp
git add src/network/HttpDownloader.h src/network/HttpDownloader.cpp
git add src/activity/settings/CategorySettingsActivity.cpp
git add docs/superpowers/plans/2026-06-30-opds-multi-server.md
git commit -m "feat: add multi-OPDS-server support with web UI management and on-device server picker"
```
