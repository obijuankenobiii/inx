#pragma once

#include "EpubAnnotations.h"

#include <Epub/PageWordIndex.h>

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class EpubActivity;

/**
 * Highlight UI: chord entry, D-pad navigation, framebuffer capture/repaint, overlays, and persistence via
 * EpubAnnotations.
 */
class EpubAnnotationUi {
 public:
  EpubAnnotationUi() = default;

  bool isActive() const { return mode_; }

  EpubAnnotations& annotations() { return annotations_; }
  const EpubAnnotations& annotations() const { return annotations_; }

  std::vector<PageWordHit>& words() { return words_; }
  const std::vector<PageWordHit>& words() const { return words_; }

  std::vector<size_t>& lineFirst() { return lineFirst_; }
  const std::vector<size_t>& lineFirst() const { return lineFirst_; }

  std::vector<std::pair<size_t, size_t>>& storedRanges() { return storedRanges_; }
  const std::vector<std::pair<size_t, size_t>>& storedRanges() const { return storedRanges_; }

  int wordIndexCacheSpine() const { return wordIndexCacheSpine_; }
  int wordIndexCachePage() const { return wordIndexCachePage_; }
  int wordIndexCacheFontId() const { return wordIndexCacheFontId_; }
  int wordIndexCacheHeaderFontId() const { return wordIndexCacheHeaderFontId_; }
  int wordIndexCacheMarginL() const { return wordIndexCacheMarginL_; }
  int wordIndexCacheMarginT() const { return wordIndexCacheMarginT_; }

  void setWordIndexCache(int spine, int page, int fontId, int headerFontId, int marginL, int marginT);

  void clearWordIndexCache();

  void clearSessionAndCapture();

  void tryChordEnter(EpubActivity& act);
  void enter(EpubActivity& act);
  void exit(EpubActivity& act);
  void handleInput(EpubActivity& act);
  void repaint(EpubActivity& act);

  void ensureDiskListLoaded(EpubActivity& act);
  void updateStoredRangesForPage(EpubActivity& act);

  void drawStoredOverlay(EpubActivity& act);
  void drawUiOverlay(EpubActivity& act);

  std::string extractRangeText(size_t anchorFlat, size_t focusFlat) const;
  void saveToStorage(EpubActivity& act);

  void prepareWordGeometry(EpubActivity& act);

  /** After layout/cache updates, keep focus/anchor within word list. */
  void clampSelectionToValidWords();

 private:
  void drawLatticeHighlightRect(EpubActivity& act, int x, int y, int width, int height);
  void drawLatticeHighlightForWordIndexRange(EpubActivity& act, size_t lo, size_t hi);
  void drawHighlights(EpubActivity& act);

  void moveFocusWord(int delta);
  void moveFocusLine(int delta);
  bool tryNavigationHoldRepeat(EpubActivity& act);

  void captureFramebuffer(EpubActivity& act);

  /** @param dir 0=L,1=R,2=U,3=D */
  bool isDuplicateNavEdge(int dir, unsigned long now);

  bool hasSaveableContent() const;
  void resetSelectionToStart(EpubActivity& act);
  void clearAllStoredHighlightsOnCurrentPage(EpubActivity& act);
  /** Sort by lo; merge overlapping or adjacent word spans. */
  static void normalizeSpans(std::vector<std::pair<size_t, size_t>>& spans);

  bool mode_ = false;
  std::vector<PageWordHit> words_;
  std::vector<size_t> lineFirst_;
  size_t anchor_ = 0;
  size_t focus_ = 0;

  /** 8000 x 6 = 48k; chunked first (same as GfxRenderer::storeBwBuffer); monolithic fallback if heap shape differs. */
  static constexpr size_t kCaptureChunkBytes = 8000;
  static constexpr size_t kCaptureChunkCount = 6;
  std::array<std::unique_ptr<uint8_t[]>, kCaptureChunkCount> captureChunks_{};
  std::unique_ptr<uint8_t[]> captureMonolithic_{};
  bool captureUsesMonolithic_ = false;
  size_t captureBytes_ = 0;
  bool captureValid_ = false;

  unsigned long chordStartMs_ = 0;
  bool chordConsumed_ = false;
  bool selectingStarted_ = false;
  /** Completed ranges while browsing between Start/Stop cycles (same page). */
  std::vector<std::pair<size_t, size_t>> pendingSpans_;
  /** Suppress duplicate wasPressed edges (ADC bounce) for the same direction. */
  unsigned long annLastNavEdgeMs_ = 0;
  int annLastNavEdgeDir_ = -1;  // 0 L, 1 R, 2 U, 3 D; -1 = none
  /** D-pad hold-repeat: -1 = none; else same encoding as annLastNavEdgeDir_. */
  int annNavRepeatDir_ = -1;
  unsigned long annNavRepeatNextMs_ = 0;

  EpubAnnotations annotations_;
  std::vector<std::pair<size_t, size_t>> storedRanges_;

  int wordIndexCacheSpine_ = -1;
  int wordIndexCachePage_ = -1;
  int wordIndexCacheFontId_ = -1;
  int wordIndexCacheHeaderFontId_ = -1;
  int wordIndexCacheMarginL_ = INT_MIN;
  int wordIndexCacheMarginT_ = INT_MIN;
};
