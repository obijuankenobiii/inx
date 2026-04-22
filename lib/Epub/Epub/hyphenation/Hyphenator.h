#pragma once

/**
 * @file Hyphenator.h
 * @brief Public interface and types for Hyphenator.
 */

#include <cstddef>
#include <string>
#include <vector>

class LanguageHyphenator;

class Hyphenator {
 public:
  struct BreakInfo {
    size_t byteOffset;
    bool requiresInsertedHyphen;
  };
  
  
  static std::vector<BreakInfo> breakOffsets(const std::string& word, bool includeFallback);

  
  static void setPreferredLanguage(const std::string& lang);

 private:
  static const LanguageHyphenator* cachedHyphenator_;
};