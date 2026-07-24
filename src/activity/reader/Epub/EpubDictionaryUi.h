#pragma once

#include <Epub/PageWordIndex.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "dictionary/StarDictLookup.h"

class EpubActivity;

/**
 * Dictionary lookup UI: chord entry, D-pad word navigation, framebuffer capture/repaint, and an
 * on-SD StarDict lookup - same interaction shape as EpubAnnotationUi (see that file), but without
 * range selection/persistence: focus always highlights a single word, and Confirm looks it up.
 */
class EpubDictionaryUi {
 public:
  EpubDictionaryUi();

  bool isActive() const { return mode_; }

  void tryChordEnter(EpubActivity& act);
  void enter(EpubActivity& act);
  void exit(EpubActivity& act);
  void handleInput(EpubActivity& act);
  void repaint(EpubActivity& act);
  void drawUiOverlay(EpubActivity& act);

 private:
  void prepareWordGeometry(EpubActivity& act);
  void captureFramebuffer(EpubActivity& act);
  void moveFocusWord(int delta);
  void moveFocusLine(int delta);
  bool tryNavigationHoldRepeat(EpubActivity& act);
  bool isDuplicateNavEdge(int dir, unsigned long now);
  void drawFocusHighlight(EpubActivity& act);
  void drawDefinitionPanel(EpubActivity& act);
  void performLookup(EpubActivity& act);
  void ensureDictionaryOpen();

  bool mode_ = false;
  std::vector<PageWordHit> words_;
  std::vector<size_t> lineFirst_;
  size_t focus_ = 0;

  StarDictLookup dict_;
  bool dictOpenAttempted_ = false;
  bool showingDefinition_ = false;
  std::string lookedUpWord_;
  std::string currentDefinition_;

  static constexpr size_t kCaptureChunkBytes = 8000;
  std::vector<std::unique_ptr<uint8_t[]>> captureChunks_{};
  std::unique_ptr<uint8_t[]> captureMonolithic_{};
  bool captureUsesMonolithic_ = false;
  size_t captureBytes_ = 0;
  bool captureValid_ = false;

  unsigned long chordStartMs_ = 0;
  bool chordConsumed_ = false;

  unsigned long lastNavEdgeMs_ = 0;
  int lastNavEdgeDir_ = -1;
  int navRepeatDir_ = -1;
  unsigned long navRepeatNextMs_ = 0;
};
