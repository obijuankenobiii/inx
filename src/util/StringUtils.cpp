/**
 * @file StringUtils.cpp
 * @brief Definitions for StringUtils.
 */

#include "StringUtils.h"

#include <cstring>

namespace StringUtils {

std::string sanitizeFilename(const std::string& name, size_t maxLength) {
  std::string result;
  result.reserve(name.size());

  for (char c : name) {
    
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
      result += '_';
    } else if (c >= 32 && c < 127) {
      
      result += c;
    }
    
  }

  
  size_t start = result.find_first_not_of(" .");
  if (start == std::string::npos) {
    return "book";  
  }
  size_t end = result.find_last_not_of(" .");
  result = result.substr(start, end - start + 1);

  
  if (result.length() > maxLength) {
    result.resize(maxLength);
  }

  return result.empty() ? "book" : result;
}

bool checkFileExtension(const std::string& fileName, const char* extension) {
  if (fileName.length() < strlen(extension)) {
    return false;
  }

  const std::string fileExt = fileName.substr(fileName.length() - strlen(extension));
  for (size_t i = 0; i < fileExt.length(); i++) {
    if (tolower(fileExt[i]) != tolower(extension[i])) {
      return false;
    }
  }
  return true;
}

bool checkFileExtension(const String& fileName, const char* extension) {
  if (fileName.length() < strlen(extension)) {
    return false;
  }

  String localFile(fileName);
  String localExtension(extension);
  localFile.toLowerCase();
  localExtension.toLowerCase();
  return localFile.endsWith(localExtension);
}

}  
