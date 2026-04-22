/**
 * @file NetworkCredential.cpp
 * @brief Definitions for NetworkCredential.
 */

#include "state/NetworkCredential.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>


WifiCredentialStore WifiCredentialStore::instance;

namespace {

constexpr uint8_t WIFI_FILE_VERSION = 1;


constexpr char WIFI_FILE[] = "/.system/wifi.bin";



constexpr uint8_t OBFUSCATION_KEY[] = {0x43, 0x72, 0x6F, 0x73, 0x73, 0x50, 0x6F, 0x69, 0x6E, 0x74};
constexpr size_t KEY_LENGTH = sizeof(OBFUSCATION_KEY);
}  

void WifiCredentialStore::obfuscate(std::string& data) const {
  Serial.printf("[%lu] [WCS] Obfuscating/deobfuscating %zu bytes\n", millis(), data.size());
  for (size_t i = 0; i < data.size(); i++) {
    data[i] ^= OBFUSCATION_KEY[i % KEY_LENGTH];
  }
}

bool WifiCredentialStore::saveToFile() const {
  
  SdMan.mkdir("/.system");

  FsFile file;
  if (!SdMan.openFileForWrite("WCS", WIFI_FILE, file)) {
    return false;
  }

  
  serialization::writePod(file, WIFI_FILE_VERSION);
  serialization::writePod(file, static_cast<uint8_t>(credentials.size()));

  
  for (const auto& cred : credentials) {
    
    serialization::writeString(file, cred.ssid);
    Serial.printf("[%lu] [WCS] Saving SSID: %s, password length: %zu\n", millis(), cred.ssid.c_str(),
                  cred.password.size());

    
    std::string obfuscatedPwd = cred.password;
    obfuscate(obfuscatedPwd);
    serialization::writeString(file, obfuscatedPwd);
  }

  file.close();
  Serial.printf("[%lu] [WCS] Saved %zu WiFi credentials to file\n", millis(), credentials.size());
  return true;
}

bool WifiCredentialStore::loadFromFile() {
  FsFile file;
  if (!SdMan.openFileForRead("WCS", WIFI_FILE, file)) {
    return false;
  }

  
  uint8_t version;
  serialization::readPod(file, version);
  if (version != WIFI_FILE_VERSION) {
    Serial.printf("[%lu] [WCS] Unknown file version: %u\n", millis(), version);
    file.close();
    return false;
  }

  
  uint8_t count;
  serialization::readPod(file, count);

  
  credentials.clear();
  for (uint8_t i = 0; i < count && i < MAX_NETWORKS; i++) {
    WifiCredential cred;

    
    serialization::readString(file, cred.ssid);

    
    serialization::readString(file, cred.password);
    Serial.printf("[%lu] [WCS] Loaded SSID: %s, obfuscated password length: %zu\n", millis(), cred.ssid.c_str(),
                  cred.password.size());
    obfuscate(cred.password);  
    Serial.printf("[%lu] [WCS] After deobfuscation, password length: %zu\n", millis(), cred.password.size());

    credentials.push_back(cred);
  }

  file.close();
  Serial.printf("[%lu] [WCS] Loaded %zu WiFi credentials from file\n", millis(), credentials.size());
  return true;
}

bool WifiCredentialStore::addCredential(const std::string& ssid, const std::string& password) {
  
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != credentials.end()) {
    cred->password = password;
    Serial.printf("[%lu] [WCS] Updated credentials for: %s\n", millis(), ssid.c_str());
    return saveToFile();
  }

  
  if (credentials.size() >= MAX_NETWORKS) {
    Serial.printf("[%lu] [WCS] Cannot add more networks, limit of %zu reached\n", millis(), MAX_NETWORKS);
    return false;
  }

  
  credentials.push_back({ssid, password});
  Serial.printf("[%lu] [WCS] Added credentials for: %s\n", millis(), ssid.c_str());
  return saveToFile();
}

bool WifiCredentialStore::removeCredential(const std::string& ssid) {
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != credentials.end()) {
    credentials.erase(cred);
    Serial.printf("[%lu] [WCS] Removed credentials for: %s\n", millis(), ssid.c_str());
    return saveToFile();
  }
  return false;  
}

const WifiCredential* WifiCredentialStore::findCredential(const std::string& ssid) const {
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });

  if (cred != credentials.end()) {
    return &*cred;
  }

  return nullptr;
}

bool WifiCredentialStore::hasSavedCredential(const std::string& ssid) const { return findCredential(ssid) != nullptr; }

void WifiCredentialStore::clearAll() {
  credentials.clear();
  saveToFile();
  Serial.printf("[%lu] [WCS] Cleared all WiFi credentials\n", millis());
}
