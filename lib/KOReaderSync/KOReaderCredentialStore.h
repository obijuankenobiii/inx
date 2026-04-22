#pragma once

/**
 * @file KOReaderCredentialStore.h
 * @brief Public interface and types for KOReaderCredentialStore.
 */

#include <cstdint>
#include <string>


enum class DocumentMatchMethod : uint8_t {
  FILENAME = 0,  
  BINARY = 1,    
};

/**
 * Singleton class for storing KOReader sync credentials on the SD card.
 * Credentials are stored in /sd/.system/koreader.bin with basic
 * XOR obfuscation to prevent casual reading (not cryptographically secure).
 */
class KOReaderCredentialStore {
 private:
  static KOReaderCredentialStore instance;
  std::string username;
  std::string password;
  std::string serverUrl;                                            
  DocumentMatchMethod matchMethod = DocumentMatchMethod::FILENAME;  

  
  KOReaderCredentialStore() = default;

  
  void obfuscate(std::string& data) const;

 public:
  
  KOReaderCredentialStore(const KOReaderCredentialStore&) = delete;
  KOReaderCredentialStore& operator=(const KOReaderCredentialStore&) = delete;

  
  static KOReaderCredentialStore& getInstance() { return instance; }

  
  bool saveToFile() const;
  bool loadFromFile();

  
  void setCredentials(const std::string& user, const std::string& pass);
  const std::string& getUsername() const { return username; }
  const std::string& getPassword() const { return password; }

  
  std::string getMd5Password() const;

  
  bool hasCredentials() const;

  
  void clearCredentials();

  
  void setServerUrl(const std::string& url);
  const std::string& getServerUrl() const { return serverUrl; }

  
  std::string getBaseUrl() const;

  
  void setMatchMethod(DocumentMatchMethod method);
  DocumentMatchMethod getMatchMethod() const { return matchMethod; }
};


#define KOREADER_STORE KOReaderCredentialStore::getInstance()
