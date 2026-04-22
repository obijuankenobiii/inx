/**
 * @file KOReaderDocumentId.cpp
 * @brief Definitions for KOReaderDocumentId.
 */

#include "KOReaderDocumentId.h"

#include <HardwareSerial.h>
#include <MD5Builder.h>
#include <SDCardManager.h>

namespace {

std::string getFilename(const std::string& path) {
  const size_t pos = path.rfind('/');
  if (pos == std::string::npos) {
    return path;
  }
  return path.substr(pos + 1);
}
}  

std::string KOReaderDocumentId::calculateFromFilename(const std::string& filePath) {
  const std::string filename = getFilename(filePath);
  if (filename.empty()) {
    return "";
  }

  MD5Builder md5;
  md5.begin();
  md5.add(filename.c_str());
  md5.calculate();

  std::string result = md5.toString().c_str();
  Serial.printf("[%lu] [KODoc] Filename hash: %s (from '%s')\n", millis(), result.c_str(), filename.c_str());
  return result;
}

size_t KOReaderDocumentId::getOffset(int i) {
  
  
  
  if (i < 0) {
    return 0;
  }
  return CHUNK_SIZE << (2 * i);
}

std::string KOReaderDocumentId::calculate(const std::string& filePath) {
  FsFile file;
  if (!SdMan.openFileForRead("KODoc", filePath, file)) {
    Serial.printf("[%lu] [KODoc] Failed to open file: %s\n", millis(), filePath.c_str());
    return "";
  }

  const size_t fileSize = file.fileSize();
  Serial.printf("[%lu] [KODoc] Calculating hash for file: %s (size: %zu)\n", millis(), filePath.c_str(), fileSize);

  
  MD5Builder md5;
  md5.begin();

  
  uint8_t buffer[CHUNK_SIZE];
  size_t totalBytesRead = 0;

  
  for (int i = -1; i < OFFSET_COUNT - 1; i++) {
    const size_t offset = getOffset(i);

    
    if (offset >= fileSize) {
      continue;
    }

    
    if (!file.seekSet(offset)) {
      Serial.printf("[%lu] [KODoc] Failed to seek to offset %zu\n", millis(), offset);
      continue;
    }

    
    const size_t bytesToRead = std::min(CHUNK_SIZE, fileSize - offset);
    const size_t bytesRead = file.read(buffer, bytesToRead);

    if (bytesRead > 0) {
      md5.add(buffer, bytesRead);
      totalBytesRead += bytesRead;
    }
  }

  file.close();

  
  md5.calculate();
  std::string result = md5.toString().c_str();

  Serial.printf("[%lu] [KODoc] Hash calculated: %s (from %zu bytes)\n", millis(), result.c_str(), totalBytesRead);

  return result;
}
