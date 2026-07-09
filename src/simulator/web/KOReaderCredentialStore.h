#pragma once

#ifdef SIMULATOR

#include <cstdint>
#include <string>

enum class DocumentMatchMethod : uint8_t {
  FILENAME = 0,
  BINARY = 1,
};

class KOReaderCredentialStore {
 private:
  std::string username;
  std::string password;
  std::string serverUrl;
  DocumentMatchMethod matchMethod = DocumentMatchMethod::FILENAME;

 public:
  static KOReaderCredentialStore& getInstance() {
    static KOReaderCredentialStore instance;
    return instance;
  }

  bool saveToFile() const { return true; }
  bool loadFromFile() { return true; }

  void setCredentials(const std::string& user, const std::string& pass) {
    username = user;
    password = pass;
  }
  const std::string& getUsername() const { return username; }
  const std::string& getPassword() const { return password; }
  std::string getMd5Password() const { return password; }
  bool hasCredentials() const { return !username.empty() && !password.empty(); }
  void clearCredentials() {
    username.clear();
    password.clear();
  }

  void setServerUrl(const std::string& url) { serverUrl = url; }
  const std::string& getServerUrl() const { return serverUrl; }
  std::string getBaseUrl() const { return serverUrl; }
  void setMatchMethod(DocumentMatchMethod method) { matchMethod = method; }
  DocumentMatchMethod getMatchMethod() const { return matchMethod; }
};

#define KOREADER_STORE KOReaderCredentialStore::getInstance()

#else
#error "src/simulator/KOReaderCredentialStore.h is only for simulator builds"
#endif
