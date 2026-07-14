/**
 * @file OpdsServerStore.cpp
 * @brief Definitions for OpdsServerStore.
 */

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
}  // namespace

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

bool OpdsServerStore::loadOrMigrate(const OpdsServerEntry& legacyServer) {
  if (SdMan.exists(OPDS_FILE)) {
    return loadFromFile();
  }

  servers.clear();
  if (legacyServer.url.empty()) {
    return true;
  }

  servers.push_back(legacyServer);
  Serial.printf("[%lu] [OSS] Migrating legacy OPDS server\n", millis());
  return saveToFile();
}

bool OpdsServerStore::addServer(const std::string& name, const std::string& url, const std::string& username,
                                const std::string& password) {
  auto existing = find_if(servers.begin(), servers.end(), [&name](const OpdsServerEntry& s) { return s.name == name; });
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
  auto existing = find_if(servers.begin(), servers.end(), [&name](const OpdsServerEntry& s) { return s.name == name; });
  if (existing != servers.end()) {
    servers.erase(existing);
    Serial.printf("[%lu] [OSS] Removed server: %s\n", millis(), name.c_str());
    return saveToFile();
  }
  return false;
}

const OpdsServerEntry* OpdsServerStore::findServer(const std::string& name) const {
  auto existing = find_if(servers.begin(), servers.end(), [&name](const OpdsServerEntry& s) { return s.name == name; });
  if (existing != servers.end()) {
    return &*existing;
  }
  return nullptr;
}
