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
 *
 * Performance notes (ESP32-C3 + SPI SD):
 * - .idx is scanned with a multi-KB read buffer (not one-byte SD reads).
 * - A checkpoint index is built on open for O(log n) bracketed search.
 * - Sort order is detected (byte-order vs case-insensitive) so the fast path works for both
 *   common StarDict layouts instead of falling back to a full linear scan.
 * - Recent definitions are cached in RAM so re-looking-up the same word is effectively free.
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
  /** Absolute path of the folder that was last successfully opened (empty if closed). */
  const std::string& folderPath() const { return folderPath_; }

  /** Hard cap on how many raw definition bytes are read from the .dict file (and thus allocated) per
   *  lookup. Some entries in large scholarly dictionaries run 50KB+ of HTML, which can fail to
   *  allocate on the ESP32-C3's fragmented heap and abort() the whole firmware. Definitions this long
   *  never fit the reader's definition panel anyway, so capping the read avoids ever attempting the
   *  huge allocation in the first place. */
  static constexpr uint32_t kMaxDefinitionBytes = 4000;

  /** Looks up queryWord (exact match, then case-insensitive fallback). Returns true and fills
   *  outDefinition (capped to kMaxDefinitionBytes raw bytes) on success. If outTruncated is non-null,
   *  set to whether the on-disk definition was larger than the cap. */
  bool lookup(const std::string& queryWord, std::string& outDefinition, bool* outTruncated = nullptr);

 private:
  // Field named entryText, not "word" - Arduino.h #defines a function-like macro `word(...)`
  // that silently breaks member-initializer syntax like `word(std::move(w))`.
  struct Checkpoint {
    Checkpoint(uint32_t offset, std::string w, std::string lower)
        : idxOffset(offset), entryText(std::move(w)), entryLower(std::move(lower)) {}
    uint32_t idxOffset = 0;
    std::string entryText;
    std::string entryLower;
  };

  struct DefCacheEntry {
    DefCacheEntry(std::string key, std::string def, bool trunc)
        : keyLower(std::move(key)), definition(std::move(def)), truncated(trunc) {}
    std::string keyLower;
    std::string definition;
    bool truncated = false;
  };

  /** Sequential .idx reader with a multi-KB window so scans don't issue one SD transaction per byte. */
  class IdxCursor {
   public:
    IdxCursor(FsFile& file, uint32_t fileSize);

    bool seek(uint32_t absOffset);
    uint32_t position() const { return pos_; }
    bool readCString(std::string& out);
    bool readBE32(uint32_t& out);
    bool readBE64(uint64_t& out);

   private:
    bool fillFrom(uint32_t absOffset);
    bool ensure(uint32_t needBytes);
    bool readRaw(uint8_t* dest, uint32_t n);

    FsFile& file_;
    uint32_t fileSize_;
    static constexpr uint32_t kBufSize = 4096;
    uint8_t buf_[kBufSize]{};
    uint32_t bufBase_ = 0;
    uint32_t bufLen_ = 0;
    uint32_t pos_ = 0;
  };

  bool parseIfo(const std::string& ifoPath);
  bool buildCheckpoints();
  /** Reads one variable-length .idx entry starting at cursor's current position. Advances the cursor
   *  past the entry. dict-offset is 4 or 8 bytes depending on use64BitOffsets_. */
  bool readIdxEntry(IdxCursor& cur, std::string& outEntryText, uint64_t& outDictOffset, uint32_t& outDictSize);

  /** Comparison matching the detected on-disk sort order. Returns <0, 0, >0. */
  int compareForSearch(const std::string& a, const std::string& aLower, const std::string& b,
                       const std::string& bLower) const;

  /** Checkpoint-based binary search + bounded buffered scan. Returns true/fills offsets on match. */
  bool lookupViaCheckpoints(const std::string& candidate, const std::string& candidateLower, uint64_t& outDictOffset,
                            uint32_t& outDictSize);

  /** One sequential pass over .idx matching any of the lowercase candidates (first hit wins in
   *  candidate-list order when multiple match - we track best priority). Buffered; last resort. */
  bool lookupViaLinearScan(const std::vector<std::string>& candidatesLower, std::string& outHitLower,
                           uint64_t& outDictOffset, uint32_t& outDictSize);

  bool readDefinition(uint64_t dictOffset, uint32_t dictSize, std::string& outDefinition, bool* outTruncated);

  bool cacheGet(const std::string& keyLower, std::string& outDefinition, bool* outTruncated);
  void cachePut(const std::string& keyLower, const std::string& definition, bool truncated);

  bool isOpen_ = false;
  FsFile idxFile_;
  FsFile dictFile_;
  std::string folderPath_;
  std::string bookname_;
  std::string sameTypeSequence_;
  uint32_t wordCount_ = 0;
  uint32_t idxFileSize_ = 0;
  // Whether .idx dict-offset fields are 8 bytes (idxoffsetbits=64 in .ifo) instead of the default 4.
  bool use64BitOffsets_ = false;
  /** When true, .idx is ordered by case-insensitive word compare (common third-party layout). When
   *  false, plain strcmp / byte order (documented StarDict convention). */
  bool caseInsensitiveSort_ = false;
  std::vector<Checkpoint> checkpoints_;

  static constexpr uint32_t kCheckpointStride = 64;
  static constexpr size_t kDefCacheSlots = 12;
  std::vector<DefCacheEntry> defCache_;
};
