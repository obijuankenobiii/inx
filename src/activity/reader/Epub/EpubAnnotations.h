#pragma once

#include <Epub/PageWordIndex.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/** One highlight row (ANN3 on disk). */
struct EpubAnnotationRecord {
  uint32_t timestamp = 0;
  std::string text;
  uint16_t startSpine = 0xFFFF;
  uint16_t startPage = 0;
  uint16_t endSpine = 0xFFFF;
  uint16_t endPage = 0xFFFF;
  uint16_t pageWordLo = 0xFFFF;
  uint16_t pageWordHi = 0xFFFF;
  uint16_t startPageWordLo = 0xFFFF;
  uint16_t startPageWordHi = 0xFFFF;
};

/**
 * Per-page highlight storage: one ANN3 file per (spine, page). Load/save for the visible page is O(1)
 * (bounded small file); saving a multi-page span touches O(span pages) shard files only.
 */
class EpubAnnotations {
 public:
  static constexpr uint16_t kWildcard = 0xFFFF;
  static constexpr int kMaxPerPage = 100;
  static constexpr const char* kSubdir = "ann";

  void clearSession();

  /** Load shard for (spine, page) if not already cached. */
  void ensurePageLoaded(const std::string& cachePath, int spine, int page);

  /** Deletes the ANN3 shard for this page and clears the in-memory cache when it matches. */
  void clearPageShard(const std::string& cachePath, int spine, int page);

  /** Whether the on-disk shard exists for this page (ground truth for saved highlights). */
  bool pageShardExists(const std::string& cachePath, int spine, int page) const;

  const std::vector<EpubAnnotationRecord>& records() const { return records_; }

  /** Enumerate touched pages, append record to each shard. Returns false if no write succeeded. */
  bool appendHighlight(const std::string& cachePath, int spineItemsCount, const EpubAnnotationRecord& rec,
                       int fallbackSpine, int fallbackPage);

  static bool recordTouchesPage(const EpubAnnotationRecord& r, int currentSpine, int currentPage);

  static void mergeStoredRangesForPage(const std::vector<EpubAnnotationRecord>& diskRecs, int currentSpine,
                                       int currentPage, const std::vector<PageWordHit>& annWords,
                                       std::vector<std::pair<size_t, size_t>>& outMerged);

 private:
  static bool tryAppendPreciseHighlightRanges(const EpubAnnotationRecord& r, int cs, int cp,
                                              const std::vector<PageWordHit>& annWords,
                                              std::vector<std::pair<size_t, size_t>>& raw);

  std::vector<EpubAnnotationRecord> records_;
  int cacheSpine_ = -1;
  int cachePage_ = -1;
};
