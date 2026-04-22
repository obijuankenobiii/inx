#pragma once

/**
 * @file LanguageRegistry.h
 * @brief Public interface and types for LanguageRegistry.
 */

#include <cstddef>
#include <string>

#include "LanguageHyphenator.h"

struct LanguageEntry {
  const char* cliName;
  const char* primaryTag;
  const LanguageHyphenator* hyphenator;
};

struct LanguageEntryView {
  const LanguageEntry* data;
  size_t size;

  const LanguageEntry* begin() const { return data; }
  const LanguageEntry* end() const { return data + size; }
};


const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& primaryTag);


LanguageEntryView getLanguageEntries();
