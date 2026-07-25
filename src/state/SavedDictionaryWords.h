#pragma once

/**
 * @file SavedDictionaryWords.h
 * @brief Persistent list of dictionary words (with their looked-up definition) the user chose to
 *        save while reading.
 *
 * One small file per saved word under kDir, rather than a single growing blob - appending a save is
 * then just "write one new file" with no need to hold every previously-saved definition in RAM to
 * rewrite a shared file (a scholarly dictionary entry can be up to kMaxDefinitionLen bytes, so a
 * single shared file holding kMaxWords of them would be far too large to keep resident on the
 * ESP32-C3). load() only reads each file's word (a few bytes) to build the in-RAM list; the
 * definition itself is read from disk on demand via definitionAt(), same formatted HTML text that was
 * shown at save time - see EpubDictionaryUi::saveCurrentWord().
 */

#include <string>
#include <vector>

#include "dictionary/StarDictLookup.h"

class SavedDictionaryWordStore {
 public:
  static SavedDictionaryWordStore& getInstance() {
    static SavedDictionaryWordStore instance;
    return instance;
  }

  /** Scans the saved-words directory and builds the in-RAM word list (idempotent). */
  void load();
  /** Forces the list to be re-scanned from SD. */
  void reload();

  int count();
  std::string wordAt(int index);
  /** Reads the stored (already-HTML-formatted) definition for a saved word from disk. */
  std::string definitionAt(int index);

  /** Case-insensitive membership check. */
  bool contains(const std::string& word);

  /** Writes word+definition to disk immediately and adds it to the in-RAM list. Returns false if
   *  already saved (case-insensitive), at the capacity limit, or the write failed. */
  bool add(const std::string& word, const std::string& definition);

  /** Deletes a saved word's file and removes it from the in-RAM list (case-insensitive match).
   *  Returns false if no such word was saved. */
  bool remove(const std::string& word);

 private:
  SavedDictionaryWordStore() = default;

  struct SavedWordEntry {
    std::string word;
    std::string filename;  // sanitized, relative to kDir
  };

  std::vector<SavedWordEntry> entries_;
  bool loaded_ = false;

  static constexpr const char* kDir = "/.system/dictionary_words";
  static constexpr size_t kMaxWordLen = 64;
  static constexpr size_t kMaxDefinitionLen = StarDictLookup::kMaxDefinitionBytes;
  static constexpr size_t kMaxWords = 100;
};

#define SAVED_WORDS SavedDictionaryWordStore::getInstance()
