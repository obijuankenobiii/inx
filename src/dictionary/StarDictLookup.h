#pragma once

/**
 * @file StarDictLookup.h
 * @brief Minimal StarDict (.ifo/.idx/.dict) reader for on-SD word lookup.
 *
 * Only uncompressed .dict files are supported (no .dict.dz). Only the common
 * "sametypesequence" .ifo layout is supported, where each .idx entry's dict-file bytes are the
 * raw definition with no per-entry type prefix - this covers the vast majority of distributed
 * StarDict dictionaries. Optional uncompressed .syn files are not required. Lookups try the typed
 * word, then elision (l'/d'/…), then nearby .idx headwords that share a stem (inflected forms
 * sort next to their lemma), then CrossPoint's small English stemmer.
 *
 * Lookup index (same idea as CrossPoint 1.5's .qidx):
 * - A sidecar of uint32 .idx byte offsets, one sample every 256 entries.
 * - No headwords are kept in RAM. Binary search seeks the .idx at a sample, reads one word,
 *   then linear-scans at most 256 entries.
 * - First open of a dictionary builds the sidecar (one streaming pass). Later opens just load
 *   the offset table (~a few KB).
 * - If the sampled search misses (unsorted or unusual .idx order), a buffered linear scan of
 *   the whole .idx is the last resort so valid entries are not dropped.
 */

#include <SdFat.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class StarDictLookup {
 public:
  StarDictLookup() = default;
  ~StarDictLookup() { close(); }

  StarDictLookup(const StarDictLookup&) = delete;
  StarDictLookup& operator=(const StarDictLookup&) = delete;

  /** Locates the .ifo/.idx/.dict files inside folderPath, parses the .ifo header, and loads or
   *  builds the sampled-offset sidecar. Returns false if the set can't be opened/parsed. */
  bool open(const std::string& folderPath);
  void close();
  bool isOpen() const { return isOpen_; }

  const std::string& bookname() const { return bookname_; }
  const std::string& lang() const { return lang_; }
  /** Absolute path of the folder that was last successfully opened (empty if closed). */
  const std::string& folderPath() const { return folderPath_; }

  /** Sidecar next to the .ifo/.idx/.dict files: sampled .idx byte offsets, no headwords. */
  static constexpr const char* kSampleIndexFileName = ".inx-qidx";

  /** True when folder has no usable sidecar — first open will scan the .idx (show "Indexing…"). */
  static bool needsIndexBuild(const std::string& folderPath);

  /** Strips leading/trailing punctuation, including UTF-8 curly quotes/dashes (U+2000–U+206F). */
  static std::string stripSurroundingPunctuation(const std::string& word);

  /** Hard cap on how many raw definition bytes are read from the .dict file (and thus allocated) per
   *  lookup. Some entries in large scholarly dictionaries run 50KB+ of HTML, which can fail to
   *  allocate on the ESP32-C3's fragmented heap and abort() the whole firmware. Definitions this long
   *  never fit the reader's definition panel anyway, so capping the read avoids ever attempting the
   *  huge allocation in the first place. */
  static constexpr uint32_t kMaxDefinitionBytes = 2400;

  /** Looks up queryWord (exact match, then case-insensitive / nearby-headword / English-stem fallback).
   *  Returns true and fills outDefinition (capped to kMaxDefinitionBytes raw bytes) on success. If
   *  outTruncated is non-null, set to whether the on-disk definition was larger than the cap. */
  bool lookup(const std::string& queryWord, std::string& outDefinition, bool* outTruncated = nullptr);

  /** Same as lookup(), but only the typed word, case, orthography fold, and elision — no inflection. */
  bool lookupExact(const std::string& queryWord, std::string& outDefinition, bool* outTruncated = nullptr);

  /** Headword that last successful lookup actually matched (lemma after stemming). Empty if none. */
  const std::string& lastHitHeadword() const { return lastHitHeadword_; }

  /** Elision-stripped and English-stem forms of @p queryWord, not including the word itself. */
  static std::vector<std::string> alternateForms(const std::string& queryWord);

 private:
  struct DefCacheEntry {
    DefCacheEntry(std::string key, std::string def, std::string hit, bool trunc)
        : keyLower(std::move(key)), definition(std::move(def)), hitHeadword(std::move(hit)), truncated(trunc) {}
    std::string keyLower;
    std::string definition;
    std::string hitHeadword;
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
  bool loadSampleIndex(const std::string& qidxPath);
  bool buildSampleIndex(const std::string& qidxPath);
  bool readIdxEntry(IdxCursor& cur, std::string& outEntryText, uint64_t& outDictOffset, uint32_t& outDictSize);

  int compareForSearch(const std::string& a, const std::string& aLower, const std::string& b,
                       const std::string& bLower) const;

  bool lookupViaSamples(const std::string& candidate, const std::string& candidateLower,
                        std::vector<std::pair<uint64_t, uint32_t>>& outHits, std::string& outMatchedWord,
                        bool allowFuzzy);

  void collectSameWordHits(IdxCursor& cur, const std::string& wordLower,
                           std::vector<std::pair<uint64_t, uint32_t>>& outHits);

  /** One sequential pass over .idx matching any of the lowercase candidates (first hit wins in
   *  candidate-list order when multiple match). Buffered last resort when sampled search misses. */
  bool lookupViaLinearScan(const std::vector<std::string>& candidatesLower, std::string& outHitLower,
                           std::vector<std::pair<uint64_t, uint32_t>>& outHits);

  bool lookupInternal(const std::string& queryWord, std::string& outDefinition, bool* outTruncated, bool allowStem);

  bool readDefinition(uint64_t dictOffset, uint32_t dictSize, std::string& outDefinition, bool* outTruncated);

  /** Reads consecutive same-headword .idx hits (StarDict stores one POS per entry). Never appends a
   *  truncated sibling — only a too-large first entry is capped. */
  bool readConcatenatedDefinitions(const std::vector<std::pair<uint64_t, uint32_t>>& hits, std::string& outDefinition,
                                   bool* outTruncated);

  bool cacheGet(const std::string& keyLower, std::string& outDefinition, bool* outTruncated);
  void cachePut(const std::string& keyLower, const std::string& definition, const std::string& hitHeadword,
                bool truncated);

  bool isOpen_ = false;
  FsFile idxFile_;
  FsFile dictFile_;
  std::string folderPath_;
  std::string bookname_;
  std::string lang_;
  std::string sameTypeSequence_;
  uint32_t wordCount_ = 0;
  uint32_t idxFileSize_ = 0;
  bool use64BitOffsets_ = false;
  bool caseInsensitiveSort_ = false;
  std::vector<uint32_t> sampleOffsets_;

  static constexpr uint32_t kSampleInterval = 256;
  static constexpr size_t kDefCacheSlots = 12;
  std::vector<DefCacheEntry> defCache_;
  std::string lastHitHeadword_;
};
