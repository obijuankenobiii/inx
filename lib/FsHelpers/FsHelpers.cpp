#include "FsHelpers.h"
#include <vector>

std::string FsHelpers::resolveRelativePath(const std::string& currentFile, const std::string& relativePath) {
  if (relativePath.empty()) return "";
  
  // If the path is absolute (starts with /), just normalise it
  if (relativePath[0] == '/') return normalisePath(relativePath);

  // 1. Get the directory containing the current file
  size_t lastSlash = currentFile.find_last_of('/');
  std::string baseDir = (lastSlash == std::string::npos) ? "" : currentFile.substr(0, lastSlash + 1);
  
  // 2. Combine them and let normalisePath handle the ".." and "."
  return normalisePath(baseDir + relativePath);
}

std::string FsHelpers::normalisePath(const std::string& path) {
  std::vector<std::string> components;
  std::string component;

  for (const auto c : path) {
    if (c == '/') {
      if (!component.empty()) {
        if (component == "..") {
          if (!components.empty()) components.pop_back();
        } else if (component != ".") { // Ignore "."
          components.push_back(component);
        }
        component.clear();
      }
    } else {
      component += c;
    }
  }

  if (!component.empty()) {
    if (component == "..") {
      if (!components.empty()) components.pop_back();
    } else if (component != ".") {
      components.push_back(component);
    }
  }

  std::string result;
  for (const auto& c : components) {
    if (!result.empty()) result += "/";
    result += c;
  }

  return result;
}