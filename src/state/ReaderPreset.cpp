/**
 * @file ReaderPreset.cpp
 * @brief Definitions for ReaderPresetStore.
 */

#include "ReaderPreset.h"

#include <SDCardManager.h>

#include <algorithm>

namespace {
constexpr size_t kMaxNameLen = 40;
constexpr size_t kMaxUserPresets = 32;
}  // namespace

void ReaderPresetStore::load() {
  if (loaded_) {
    return;
  }
  loaded_ = true;
  userPresets_.clear();

  FsFile f;
  if (!SdMan.openFileForRead("RPS", kPath, f)) {
    return;
  }

  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t presetCount = 0;
  if (f.read(&magic, sizeof(magic)) != sizeof(magic) || magic != kMagic ||
      f.read(&version, sizeof(version)) != sizeof(version) || version != kVersion ||
      f.read(&presetCount, sizeof(presetCount)) != sizeof(presetCount)) {
    f.close();
    return;
  }

  for (uint8_t i = 0; i < presetCount; i++) {
    uint8_t nameLen = 0;
    if (f.read(&nameLen, sizeof(nameLen)) != sizeof(nameLen)) {
      break;
    }
    char nameBuf[kMaxNameLen + 1] = {0};
    const uint8_t readLen = std::min<uint8_t>(nameLen, kMaxNameLen);
    if (nameLen > 0 && f.read(nameBuf, nameLen) != nameLen) {
      break;
    }
    nameBuf[readLen] = '\0';

    uint8_t record[64] = {0};
    const size_t toRead = BookSettings::kSerializedSize;
    if (f.read(record, toRead) != static_cast<int>(toRead)) {
      break;
    }
    ReaderPreset preset;
    preset.name = std::string(nameBuf);
    size_t offset = 0;
    preset.settings.deserialize(record, toRead, offset);
    preset.settings.useCustomSettings = true;
    userPresets_.push_back(std::move(preset));
  }

  f.close();
}

bool ReaderPresetStore::save() {
  SdMan.mkdir(kDir);

  FsFile f;
  if (!SdMan.openFileForWrite("RPS", kPath, f)) {
    return false;
  }

  uint32_t magic = kMagic;
  uint8_t version = kVersion;
  uint8_t presetCount = static_cast<uint8_t>(std::min(userPresets_.size(), kMaxUserPresets));
  f.write(&magic, sizeof(magic));
  f.write(&version, sizeof(version));
  f.write(&presetCount, sizeof(presetCount));

  for (uint8_t i = 0; i < presetCount; i++) {
    const ReaderPreset& preset = userPresets_[i];
    uint8_t nameLen = static_cast<uint8_t>(std::min(preset.name.size(), kMaxNameLen));
    f.write(&nameLen, sizeof(nameLen));
    if (nameLen > 0) {
      f.write(reinterpret_cast<const uint8_t*>(preset.name.data()), nameLen);
    }
    uint8_t record[64] = {0};
    size_t offset = 0;
    preset.settings.serialize(record, offset);
    f.write(record, offset);
  }

  f.close();
  return true;
}

int ReaderPresetStore::count() {
  load();
  return 1 + static_cast<int>(userPresets_.size());
}

std::string ReaderPresetStore::nameOf(int index) {
  load();
  if (index <= 0) {
    return "Default";
  }
  if (index - 1 < static_cast<int>(userPresets_.size())) {
    return userPresets_[index - 1].name;
  }
  return "";
}

BookSettings ReaderPresetStore::settingsOf(int index) {
  load();
  BookSettings out;
  if (index <= 0) {
    out.loadFromGlobalSettings();
    out.useCustomSettings = false;
    return out;
  }
  if (index - 1 < static_cast<int>(userPresets_.size())) {
    out = userPresets_[index - 1].settings;
    out.useCustomSettings = true;
  }
  return out;
}

int ReaderPresetStore::add(const std::string& name, const BookSettings& settings) {
  load();
  if (userPresets_.size() >= kMaxUserPresets) {
    return -1;
  }
  ReaderPreset preset;
  preset.name = name.empty() ? "Preset" : name.substr(0, kMaxNameLen);
  preset.settings = settings;
  preset.settings.useCustomSettings = true;
  userPresets_.push_back(std::move(preset));
  save();
  return static_cast<int>(userPresets_.size());  // store index of the new preset
}

bool ReaderPresetStore::update(int index, const std::string& name, const BookSettings& settings) {
  load();
  if (index <= 0) {
    // Default: write the settings back to the global reader fields.
    settings.applyToGlobalSettings();
    SETTINGS.saveToFile();
    return true;
  }
  if (index - 1 >= static_cast<int>(userPresets_.size())) {
    return false;
  }
  userPresets_[index - 1].name = name.empty() ? userPresets_[index - 1].name : name.substr(0, kMaxNameLen);
  userPresets_[index - 1].settings = settings;
  userPresets_[index - 1].settings.useCustomSettings = true;
  return save();
}

bool ReaderPresetStore::rename(int index, const std::string& name) {
  load();
  if (index <= 0 || index - 1 >= static_cast<int>(userPresets_.size()) || name.empty()) {
    return false;
  }
  userPresets_[index - 1].name = name.substr(0, kMaxNameLen);
  return save();
}

bool ReaderPresetStore::remove(int index) {
  load();
  if (index <= 0 || index - 1 >= static_cast<int>(userPresets_.size())) {
    return false;
  }
  userPresets_.erase(userPresets_.begin() + (index - 1));
  return save();
}

void ReaderPresetStore::applyToBook(int index, BookSettings& book) {
  load();
  if (index <= 0) {
    book.loadFromGlobalSettings();
    book.useCustomSettings = false;
    return;
  }
  if (index - 1 < static_cast<int>(userPresets_.size())) {
    book = userPresets_[index - 1].settings;
    book.useCustomSettings = true;
  }
}
