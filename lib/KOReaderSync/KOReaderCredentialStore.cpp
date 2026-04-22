/**
 * @file KOReaderCredentialStore.cpp
 * @brief Definitions for KOReaderCredentialStore.
 */

#include "KOReaderCredentialStore.h"

#include <HardwareSerial.h>
#include <MD5Builder.h>
#include <SDCardManager.h>
#include <Serialization.h>


KOReaderCredentialStore KOReaderCredentialStore::instance;

namespace {

constexpr uint8_t KOREADER_FILE_VERSION = 1;


constexpr char KOREADER_FILE[] = "/.system/koreader.bin";


constexpr char DEFAULT_SERVER_URL[] = "https://sync.koreader.rocks:443";



constexpr uint8_t OBFUSCATION_KEY[] = {0x4B, 0x4F, 0x52, 0x65, 0x61, 0x64, 0x65, 0x72};
constexpr size_t KEY_LENGTH = sizeof(OBFUSCATION_KEY);
}  

void KOReaderCredentialStore::obfuscate(std::string& data) const {
  for (size_t i = 0; i < data.size(); i++) {
    data[i] ^= OBFUSCATION_KEY[i % KEY_LENGTH];
  }
}

bool KOReaderCredentialStore::saveToFile() const {
  
  SdMan.mkdir("/.system");

  FsFile file;
  if (!SdMan.openFileForWrite("KRS", KOREADER_FILE, file)) {
    return false;
  }

  
  serialization::writePod(file, KOREADER_FILE_VERSION);

  
  serialization::writeString(file, username);
  Serial.printf("[%lu] [KRS] Saving username: %s\n", millis(), username.c_str());

  
  std::string obfuscatedPwd = password;
  obfuscate(obfuscatedPwd);
  serialization::writeString(file, obfuscatedPwd);

  
  serialization::writeString(file, serverUrl);

  
  serialization::writePod(file, static_cast<uint8_t>(matchMethod));

  file.close();
  Serial.printf("[%lu] [KRS] Saved KOReader credentials to file\n", millis());
  return true;
}

bool KOReaderCredentialStore::loadFromFile() {
  FsFile file;
  if (!SdMan.openFileForRead("KRS", KOREADER_FILE, file)) {
    saveToFile();
    return false;
  }

  
  uint8_t version;
  serialization::readPod(file, version);
  if (version != KOREADER_FILE_VERSION) {
    file.close();
    return false;
  }

  
  if (file.available()) {
    serialization::readString(file, username);
  } else {
    username.clear();
  }

  
  if (file.available()) {
    serialization::readString(file, password);
    obfuscate(password);  
  } else {
    password.clear();
  }

  
  if (file.available()) {
    serialization::readString(file, serverUrl);
  } else {
    serverUrl.clear();
  }

  
  if (file.available()) {
    uint8_t method;
    serialization::readPod(file, method);
    matchMethod = static_cast<DocumentMatchMethod>(method);
  } else {
    matchMethod = DocumentMatchMethod::FILENAME;
  }

  file.close();
  return true;
}

void KOReaderCredentialStore::setCredentials(const std::string& user, const std::string& pass) {
  username = user;
  password = pass;
  Serial.printf("[%lu] [KRS] Set credentials for user: %s\n", millis(), user.c_str());
}

std::string KOReaderCredentialStore::getMd5Password() const {
  if (password.empty()) {
    return "";
  }

  
  MD5Builder md5;
  md5.begin();
  md5.add(password.c_str());
  md5.calculate();

  return md5.toString().c_str();
}

bool KOReaderCredentialStore::hasCredentials() const { return !username.empty() && !password.empty(); }

void KOReaderCredentialStore::clearCredentials() {
  username.clear();
  password.clear();
  saveToFile();
  Serial.printf("[%lu] [KRS] Cleared KOReader credentials\n", millis());
}

void KOReaderCredentialStore::setServerUrl(const std::string& url) {
  serverUrl = url;
  Serial.printf("[%lu] [KRS] Set server URL: %s\n", millis(), url.empty() ? "(default)" : url.c_str());
}

std::string KOReaderCredentialStore::getBaseUrl() const {
  if (serverUrl.empty()) {
    return DEFAULT_SERVER_URL;
  }

  
  if (serverUrl.find("://") == std::string::npos) {
    return "http://" + serverUrl;
  }

  return serverUrl;
}

void KOReaderCredentialStore::setMatchMethod(DocumentMatchMethod method) {
  matchMethod = method;
  Serial.printf("[%lu] [KRS] Set match method: %s\n", millis(),
                method == DocumentMatchMethod::FILENAME ? "Filename" : "Binary");
}
