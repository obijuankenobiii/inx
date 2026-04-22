#pragma once

/**
 * @file SDCardManager.h
 * @brief Public interface and types for SDCardManager.
 */

#include <WString.h>
#include <vector>
#include <string>
#include <SdFat.h>

class SDCardManager {
 public:
  SDCardManager();
  bool begin();
  bool ready() const;
  std::vector<String> listFiles(const char* path = "/", int maxFiles = 200);
  
  String readFile(const char* path);
  
  
  
  bool readFileToStream(const char* path, Print& out, size_t chunkSize = 256);
  
  size_t readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes = 0);
  
  
  bool writeFile(const char* path, const String& content);
  
  bool ensureDirectoryExists(const char* path);

  FsFile open(const char* path, const oflag_t oflag = O_RDONLY) { return sd.open(path, oflag); }
  bool mkdir(const char* path, const bool pFlag = true) { return sd.mkdir(path, pFlag); }
  bool exists(const char* path) { return sd.exists(path); }
  bool remove(const char* path) { return sd.remove(path); }
  bool rmdir(const char* path) { return sd.rmdir(path); }
  bool rename(const char* path, const char* newPath) { return sd.rename(path, newPath); }

  bool openFileForRead(const char* moduleName, const char* path, FsFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, FsFile& file);
  bool openFileForRead(const char* moduleName, const String& path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const String& path, FsFile& file);
  bool removeDir(const char* path);

 static SDCardManager& getInstance() { return instance; }

 private:
  static SDCardManager instance;

  bool initialized = false;
  SdFat sd;
};

#define SdMan SDCardManager::getInstance()
