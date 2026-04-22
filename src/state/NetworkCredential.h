#pragma once

/**
 * @file NetworkCredential.h
 * @brief Public interface and types for NetworkCredential.
 */

#include <string>
#include <vector>

struct WifiCredential {
  std::string ssid;
  std::string password;  
};

/**
 * Singleton class for storing WiFi credentials on the SD card.
 * Credentials are stored in /sd/.system/wifi.bin with basic
 * XOR obfuscation to prevent casual reading (not cryptographically secure).
 */
class WifiCredentialStore {
 private:
  static WifiCredentialStore instance;
  std::vector<WifiCredential> credentials;

  static constexpr size_t MAX_NETWORKS = 8;

  
  WifiCredentialStore() = default;

  
  void obfuscate(std::string& data) const;

 public:
  
  WifiCredentialStore(const WifiCredentialStore&) = delete;
  WifiCredentialStore& operator=(const WifiCredentialStore&) = delete;

  
  static WifiCredentialStore& getInstance() { return instance; }

  
  bool saveToFile() const;
  bool loadFromFile();

  
  bool addCredential(const std::string& ssid, const std::string& password);
  bool removeCredential(const std::string& ssid);
  const WifiCredential* findCredential(const std::string& ssid) const;

  
  const std::vector<WifiCredential>& getCredentials() const { return credentials; }

  
  bool hasSavedCredential(const std::string& ssid) const;

  
  void clearAll();
};


#define WIFI_STORE WifiCredentialStore::getInstance()
