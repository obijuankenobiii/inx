#pragma once

/**
 * @file ReaderPreset.h
 * @brief Named reader settings presets and their persistent store.
 *
 * A preset is a name plus a full BookSettings snapshot. Index 0 is a virtual "Default" preset that
 * maps to the global SystemSetting reader fields (so un-customized books keep using the globals).
 * User presets (index >= 1) are persisted to /.system/reader_presets.bin.
 *
 * Applying a preset to a book is a one-time SNAPSHOT copy (see applyToBook).
 */

#include <string>
#include <vector>

#include "state/BookSetting.h"

struct ReaderPreset {
  std::string name;
  BookSettings settings;
};

class ReaderPresetStore {
 public:
  static ReaderPresetStore& getInstance() {
    static ReaderPresetStore instance;
    return instance;
  }

  /** Loads user presets from SD (idempotent). */
  void load();
  /** Persists user presets to SD. */
  bool save();

  /** Total presets including the virtual Default at index 0. */
  int count();
  bool isDefault(int index) const { return index == 0; }

  /** Display name for a preset index (Default => "Default"). */
  std::string nameOf(int index);
  /** Settings snapshot for a preset index (Default reads the live global reader settings). */
  BookSettings settingsOf(int index);

  /** Appends a new user preset; returns its store index (>=1), or -1 on failure. */
  int add(const std::string& name, const BookSettings& settings);
  /** Replaces a preset's name+settings. For Default (index 0) this writes back to the globals. */
  bool update(int index, const std::string& name, const BookSettings& settings);
  /** Renames a user preset (rejects Default). */
  bool rename(int index, const std::string& name);
  /** Deletes a user preset (rejects Default). */
  bool remove(int index);

  /**
   * Snapshot-applies a preset onto a book's settings. Default => revert book to globals
   * (useCustomSettings=false); a named preset => copy its values (useCustomSettings=true).
   */
  void applyToBook(int index, BookSettings& book);

 private:
  ReaderPresetStore() = default;

  std::vector<ReaderPreset> userPresets_;  ///< store index i (>=1) maps to userPresets_[i-1]
  bool loaded_ = false;

  static constexpr const char* kDir = "/.system";
  static constexpr const char* kPath = "/.system/reader_presets.bin";
  static constexpr uint32_t kMagic = 0x52505253;  // "RPRS"
  static constexpr uint8_t kVersion = 2;
};

#define READER_PRESETS ReaderPresetStore::getInstance()
