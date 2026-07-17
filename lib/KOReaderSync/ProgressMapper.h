#pragma once

/**
 * @file ProgressMapper.h
 * @brief Public interface and types for ProgressMapper.
 */

#include <Epub.h>

#include <memory>
#include <string>

/**
 * Reader page position representation.
 */
struct PagePosition {
  int spineIndex;
  int pageNumber;
  int totalPages;
  uint16_t paragraphIndex = 0;
  bool hasParagraphIndex = false;
  uint16_t liIndex = 0;
  bool hasLiIndex = false;
  char xpathAnchorId[64] = {};
};

/**
 * KOReader position representation.
 */
struct KOReaderPosition {
  std::string xpath;
  float percentage;
};

/**
 * Maps between CrossPoint and KOReader position formats.
 *
 * The reader tracks position as (spineIndex, pageNumber).
 * KOReader uses XPath-like strings + percentage.
 *
 * Since CrossPoint discards HTML structure during parsing, we generate
 * synthetic XPath strings based on spine index, using percentage as the
 * primary sync mechanism.
 */
class ProgressMapper {
 public:
  /**
   * Convert reader page position to KOReader format.
   *
   * @param epub The EPUB book
   * @param pos Reader page position
   * @return KOReader position
   */
  static KOReaderPosition toKOReader(const std::shared_ptr<Epub>& epub, const PagePosition& pos);

  /**
   * Convert KOReader position to reader page format.
   *
   * Note: The returned pageNumber may be approximate since different
   * rendering settings produce different page counts.
   *
   * @param epub The EPUB book
   * @param koPos KOReader position
   * @param currentSpineIndex Index of the currently open spine item (for density estimation)
   * @param totalPagesInCurrentSpine Total pages in the current spine item (for density estimation)
   * @return Reader page position
   */
  static PagePosition toCrossPoint(const std::shared_ptr<Epub>& epub, const KOReaderPosition& koPos,
                                   int currentSpineIndex = -1, int totalPagesInCurrentSpine = 0);

 private:
  /**
   * Generate a fallback XPath by streaming the spine item's XHTML and resolving
   * a paragraph/text position from intra-spine progress.
   * Produces a full ancestry path such as
   * /body/DocFragment[3]/body/p[42]/text().17.
   */
  static std::string generateXPath(const std::shared_ptr<Epub>& epub, int spineIndex, float intraSpineProgress);
};
