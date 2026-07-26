/**
 * @file SavedDictionaryWords.cpp
 * @brief Definitions for SavedDictionaryWordStore.
 */

#include "SavedDictionaryWords.h"

#include <SDCardManager.h>

#include <algorithm>
#include <cctype>

namespace {
std::string toLowerCopy(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

/** Filenames are derived from the word, not stored verbatim - keeps every saved word on safe FAT
 *  filename ground (letters/digits only) regardless of accents, apostrophes, or punctuation. */
std::string sanitizeFilename(const std::string& word) {
  std::string out;
  out.reserve(word.size());
  for (const unsigned char c : word) {
    if (std::isalnum(c)) {
      out += static_cast<char>(std::tolower(c));
    } else {
      out += '_';
    }
  }
  if (out.empty()) {
    out = "_";
  }
  if (out.size() > 40) {
    out.resize(40);
  }
  return out + ".bin";
}
}  // namespace

void SavedDictionaryWordStore::load() {
  if (loaded_) {
    return;
  }
  loaded_ = true;
  entries_.clear();

  if (!SdMan.exists(kDir)) {
    return;
  }
  const std::vector<String> files = SdMan.listFiles(kDir);
  for (const String& fname : files) {
    const std::string path = std::string(kDir) + "/" + fname.c_str();
    FsFile f;
    if (!SdMan.openFileForRead("SDW", path, f)) {
      continue;
    }
    uint8_t wordLen = 0;
    if (f.read(&wordLen, sizeof(wordLen)) != sizeof(wordLen)) {
      f.close();
      continue;
    }
    char buf[kMaxWordLen + 1] = {0};
    const uint8_t readLen = std::min<uint8_t>(wordLen, kMaxWordLen);
    if (wordLen > 0 && f.read(buf, wordLen) != wordLen) {
      f.close();
      continue;
    }
    buf[readLen] = '\0';
    f.close();

    SavedWordEntry entry;
    entry.word = buf;
    entry.filename = fname.c_str();
    entries_.push_back(std::move(entry));
  }
}

void SavedDictionaryWordStore::reload() {
  loaded_ = false;
  load();
}

int SavedDictionaryWordStore::count() {
  load();
  return static_cast<int>(entries_.size());
}

std::string SavedDictionaryWordStore::wordAt(int index) {
  load();
  if (index < 0 || index >= static_cast<int>(entries_.size())) {
    return "";
  }
  return entries_[index].word;
}

std::string SavedDictionaryWordStore::definitionAt(int index) {
  load();
  if (index < 0 || index >= static_cast<int>(entries_.size())) {
    return "";
  }
  const std::string path = std::string(kDir) + "/" + entries_[index].filename;
  FsFile f;
  if (!SdMan.openFileForRead("SDW", path, f)) {
    return "";
  }
  uint8_t wordLen = 0;
  if (f.read(&wordLen, sizeof(wordLen)) != sizeof(wordLen) || f.seekCur(wordLen) == false) {
    f.close();
    return "";
  }
  uint16_t defLen = 0;
  if (f.read(&defLen, sizeof(defLen)) != sizeof(defLen)) {
    f.close();
    return "";
  }
  std::string definition;
  definition.resize(std::min<size_t>(defLen, kMaxDefinitionLen));
  const int readN = definition.empty() ? 0 : f.read(&definition[0], definition.size());
  f.close();
  if (readN != static_cast<int>(definition.size())) {
    return "";
  }
  return definition;
}

bool SavedDictionaryWordStore::contains(const std::string& word) {
  load();
  const std::string lower = toLowerCopy(word);
  return std::any_of(entries_.begin(), entries_.end(),
                     [&lower](const SavedWordEntry& e) { return toLowerCopy(e.word) == lower; });
}

bool SavedDictionaryWordStore::add(const std::string& word, const std::string& definition) {
  load();
  if (word.empty() || word.size() > kMaxWordLen) {
    return false;
  }
  if (entries_.size() >= kMaxWords) {
    return false;
  }
  if (contains(word)) {
    return false;
  }

  SdMan.mkdir(kDir);
  const std::string filename = sanitizeFilename(word);
  const std::string path = std::string(kDir) + "/" + filename;

  FsFile f;
  if (!SdMan.openFileForWrite("SDW", path, f)) {
    return false;
  }
  const uint8_t wordLen = static_cast<uint8_t>(std::min(word.size(), kMaxWordLen));
  const uint16_t defLen = static_cast<uint16_t>(std::min(definition.size(), kMaxDefinitionLen));
  f.write(&wordLen, sizeof(wordLen));
  f.write(reinterpret_cast<const uint8_t*>(word.data()), wordLen);
  f.write(&defLen, sizeof(defLen));
  if (defLen > 0) {
    f.write(reinterpret_cast<const uint8_t*>(definition.data()), defLen);
  }
  f.close();

  SavedWordEntry entry;
  entry.word = word;
  entry.filename = filename;
  entries_.push_back(std::move(entry));
  return true;
}

bool SavedDictionaryWordStore::remove(const std::string& word) {
  load();
  const std::string lower = toLowerCopy(word);
  const auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&lower](const SavedWordEntry& e) { return toLowerCopy(e.word) == lower; });
  if (it == entries_.end()) {
    return false;
  }
  const std::string path = std::string(kDir) + "/" + it->filename;
  SdMan.remove(path.c_str());
  entries_.erase(it);
  return true;
}
