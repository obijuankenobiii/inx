#pragma once

/**
 * @file StarDictLookup.h
 * @brief Minimal StarDict (.ifo/.idx/.dict) reader for on-SD word lookup.
 *
 * Only uncompressed .dict files are supported (no .dict.dz). Only the common
 * "sametypesequence" .ifo layout is supported, where each .idx entry's dict-file bytes are the
 * raw definition with no per-entry type prefix - this covers the vast majority of distributed
 * StarDict dictionaries. .syn synonym files are not consulted; lookups are exact/case-insensitive
 * word match only.
 */

#include <SdFat.h>

#include <cstdint>
#include <string>
#include <vector>

class StarDictLookup {
 public:
  StarDictLookup() = default;
  ~StarDictLookup() { close(); }

  StarDictLookup(const StarDictLookup&) = delete;
  StarDictLookup& operator=(const StarDictLookup&) = delete;

  /** Locates the .ifo/.idx/.dict files inside folderPath, parses the .ifo header, and builds an in-RAM
   *  checkpoint index over .idx for fast lookup. Returns false if the set can't be opened/parsed. */
  bool open(const std::string& folderPath);
  void close();
  bool isOpen() const { return isOpen_; }

  const std::string& bookname() const { return bookname_; }

  /** Looks up queryWord (exact match, then case-insensitive fallback). Returns true and fills
   *  outDefinition on success. */
  bool lookup(const std::string& queryWord, std::string& outDefinition);

 private:
  // Field named entryText, not "word" - Arduino.h #defines a function-like macro `word(...)`
  // that silently breaks member-initializer syntax like `word(std::move(w))`.
  struct Checkpoint {
    Checkpoint(uint32_t offset, std::string w) : idxOffset(offset), entryText(std::move(w)) {}
    uint32_t idxOffset = 0;
    std::string entryText;
  };

  bool parseIfo(const std::string& ifoPath);
  bool buildCheckpoints();
  /** Reads one variable-length .idx entry at idxOffset. Returns false on read failure/EOF.
   *  nextOffset is the file offset immediately after this entry (for linear-scan advancement).
   *  The dict-offset field is 4 or 8 bytes on disk depending on the .ifo's idxoffsetbits (see
   *  use64BitOffsets_ below), hence outDictOffset being 64-bit here. */
  bool readIdxEntryAt(uint32_t idxOffset, std::string& outEntryText, uint64_t& outDictOffset, uint32_t& outDictSize,
                      uint32_t& outNextOffset);
  /** Case-insensitive-first comparison matching StarDict's default collation closely enough for
   *  practical lookup; returns <0, 0, >0. */
  static int compareWord(const std::string& a, const std::string& b);
  /** Checkpoint-based binary search + bounded scan, assuming .idx is sorted in plain byte order
   *  (the documented StarDict convention). Returns true/fills offsets on an exact match. */
  bool lookupViaCheckpoints(const std::string& candidate, uint64_t& outDictOffset, uint32_t& outDictSize);
  /** Full sequential scan of .idx, case-insensitive. Slower but correct regardless of the actual
   *  on-disk sort order - many third-party-generated StarDict files don't follow the spec's byte-
   *  order sort (e.g. case-insensitive sort instead), which silently breaks the checkpoint binary
   *  search above. Used only as a fallback when the fast path finds nothing. */
  bool lookupViaLinearScan(const std::string& candidate, uint64_t& outDictOffset, uint32_t& outDictSize);

  bool isOpen_ = false;
  FsFile idxFile_;
  FsFile dictFile_;
  std::string bookname_;
  std::string sameTypeSequence_;
  uint32_t wordCount_ = 0;
  uint32_t idxFileSize_ = 0;
  // Whether .idx dict-offset fields are 8 bytes (idxoffsetbits=64 in .ifo) instead of the default 4.
  // Large dictionaries (multi-GB .dict files) need this; getting it wrong misaligns every entry
  // after the first, since the offset/size fields are fixed-width but the preceding word isn't.
  bool use64BitOffsets_ = false;
  std::vector<Checkpoint> checkpoints_;

  static constexpr uint32_t kCheckpointStride = 256;
};
