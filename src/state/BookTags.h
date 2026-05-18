#pragma once

#include <ArduinoJson.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

class BookTags {
 public:
  struct Entry {
    std::string path;
    std::string tag;
  };

  static constexpr const char* TAGS_PATH = "/.metadata/library/book_tags.json";
  static constexpr const char* TAG_LIST_KEY = "__tags";

  static std::string normalizeTag(const char* raw) {
    std::string tag = raw ? raw : "";
    while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.front()))) {
      tag.erase(tag.begin());
    }
    while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.back()))) {
      tag.pop_back();
    }
    if (tag.size() > 40) {
      tag.resize(40);
    }
    return tag;
  }

  static bool load(std::vector<Entry>& entries) {
    entries.clear();
    FsFile file = SdMan.open(TAGS_PATH, O_READ);
    if (!file) {
      return true;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err || !doc.is<JsonObject>()) {
      return false;
    }

    for (JsonPair kv : doc.as<JsonObject>()) {
      const char* path = kv.key().c_str();
      if (!path || strcmp(path, TAG_LIST_KEY) == 0 || !kv.value().is<const char*>()) {
        continue;
      }
      const char* tag = kv.value().as<const char*>();
      if (!path || !path[0]) {
        continue;
      }
      std::string cleanTag = normalizeTag(tag);
      if (cleanTag.empty()) {
        continue;
      }
      entries.push_back({path, cleanTag});
    }
    return true;
  }

  static bool loadTagList(std::vector<std::string>& tags) {
    tags.clear();
    FsFile file = SdMan.open(TAGS_PATH, O_READ);
    if (!file) {
      return true;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err || !doc.is<JsonObject>()) {
      return false;
    }

    JsonObject root = doc.as<JsonObject>();
    if (root[TAG_LIST_KEY].is<JsonArray>()) {
      for (JsonVariant value : root[TAG_LIST_KEY].as<JsonArray>()) {
        addUniqueTag(tags, normalizeTag(value.as<const char*>()));
      }
    }

    for (JsonPair kv : root) {
      if (!kv.value().is<const char*>()) {
        continue;
      }
      addUniqueTag(tags, normalizeTag(kv.value().as<const char*>()));
    }
    sortTags(tags);
    return true;
  }

  static std::string find(const std::vector<Entry>& entries, const std::string& path) {
    for (const auto& entry : entries) {
      if (entry.path == path) {
        return entry.tag;
      }
    }
    return "";
  }

  static bool set(const std::string& path, const std::string& rawTag) {
    if (path.empty()) {
      return false;
    }
    if (!SdMan.exists("/.metadata")) SdMan.mkdir("/.metadata");
    if (!SdMan.exists("/.metadata/library")) SdMan.mkdir("/.metadata/library");

    std::vector<Entry> entries;
    load(entries);
    std::vector<std::string> tags;
    loadTagList(tags);

    const std::string tag = normalizeTag(rawTag.c_str());
    addUniqueTag(tags, tag);
    bool found = false;
    for (auto& entry : entries) {
      if (entry.path == path) {
        found = true;
        if (tag.empty()) {
          entry.tag.clear();
        } else {
          entry.tag = tag;
        }
        break;
      }
    }
    if (!found && !tag.empty()) {
      entries.push_back({path, tag});
    }

    return writeAll(entries, tags);
  }

  static bool addTag(const std::string& rawTag) {
    if (!SdMan.exists("/.metadata")) SdMan.mkdir("/.metadata");
    if (!SdMan.exists("/.metadata/library")) SdMan.mkdir("/.metadata/library");

    std::vector<Entry> entries;
    load(entries);
    std::vector<std::string> tags;
    loadTagList(tags);
    addUniqueTag(tags, normalizeTag(rawTag.c_str()));
    return writeAll(entries, tags);
  }

 private:
  static void addUniqueTag(std::vector<std::string>& tags, const std::string& tag) {
    if (tag.empty()) {
      return;
    }
    for (const auto& existing : tags) {
      if (existing == tag) {
        return;
      }
    }
    tags.push_back(tag);
  }

  static void sortTags(std::vector<std::string>& tags) {
    std::sort(tags.begin(), tags.end(), [](std::string a, std::string b) {
      std::transform(a.begin(), a.end(), a.begin(), ::tolower);
      std::transform(b.begin(), b.end(), b.begin(), ::tolower);
      return a < b;
    });
  }

  static bool writeAll(const std::vector<Entry>& entries, std::vector<std::string> tags) {
    FsFile file = SdMan.open(TAGS_PATH, O_WRITE | O_CREAT | O_TRUNC);
    if (!file) {
      return false;
    }

    sortTags(tags);
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    JsonArray tagArray = root[TAG_LIST_KEY].to<JsonArray>();
    for (const auto& tag : tags) {
      if (!tag.empty()) {
        tagArray.add(tag.c_str());
      }
    }
    for (const auto& entry : entries) {
      if (!entry.path.empty() && !entry.tag.empty()) {
        root[entry.path.c_str()] = entry.tag.c_str();
      }
    }
    serializeJson(doc, file);
    file.close();
    return true;
  }
};
