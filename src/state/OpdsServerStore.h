#pragma once

/**
 * @file OpdsServerStore.h
 * @brief Public interface and types for OpdsServerStore.
 */

#include <string>
#include <vector>

struct OpdsServerEntry {
  std::string name;
  std::string url;
  std::string username;
  std::string password;
};

/**
 * Singleton class for storing OPDS server entries on the SD card.
 * Servers are stored in /sd/.system/opds_servers.bin with basic
 * XOR obfuscation of passwords to prevent casual reading (not cryptographically secure).
 */
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
  bool loadOrMigrate(const OpdsServerEntry& legacyServer);

  bool addServer(const std::string& name, const std::string& url, const std::string& username,
                 const std::string& password);
  bool removeServer(const std::string& name);
  const OpdsServerEntry* findServer(const std::string& name) const;

  const std::vector<OpdsServerEntry>& getAllServers() const { return servers; }
};

#define OPDS_STORE OpdsServerStore::getInstance()
