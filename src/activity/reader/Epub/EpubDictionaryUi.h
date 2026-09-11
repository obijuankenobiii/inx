#pragma once

#include <Epub/PageWordIndex.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "dictionary/DictionaryDefinitionLayout.h"
#include "dictionary/DictionaryRegistry.h"
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
  void ensureDictionaryOpen(EpubActivity& act);
  void openFolder(const std::string& folderName);
  bool lookupInFolder(EpubActivity& act, const std::string& folderName, const std::string& queryWord,
                      std::string& outDefinition, bool* outTruncated, bool allowStem = true);
  bool tryUsefulLookup(EpubActivity& act, const std::string& folderName, const std::string& queryWord, bool* outTruncated);
  void cycleDictionary(EpubActivity& act, int delta);
  void saveCurrentWord(EpubActivity& act);
  std::string resolvePreferredFolder(EpubActivity& act);
  void setLangLabelFromFolder(const std::string& folderName);
  void layoutCurrentDefinition(EpubActivity& act, bool truncated);
  /** Actually releases currentDefinition_/definitionBlocks_/definitionLines_'s heap capacity (not
   *  just .clear(), which keeps it reserved for reuse) - a big dictionary entry's parsed/laid-out
   *  form can run into the tens of KB, and .clear() alone would leave that reserved for as long as
   *  the book stays open even after the user backs out of viewing it. */
  void releaseDefinitionMemory();

  bool mode_ = false;
  std::vector<PageWordHit> words_;
  std::vector<size_t> lineFirst_;
  size_t focus_ = 0;

  StarDictLookup dict_;
  std::string preferredFolder_;
  std::string sessionFolder_;
  std::string activeLangLabel_;
  bool usedFallbackDict_ = false;
  bool showingDefinition_ = false;
  std::string lookedUpWord_;
  /** Dictionary headword that actually matched (lemma after stemming). Empty when it equals lookedUpWord_. */
  std::string matchedHeadword_;
  bool wordAlreadySaved_ = false;
  std::string currentDefinition_;
  std::vector<DefinitionBlock> definitionBlocks_;
  // Wrapped/styled once per lookup in performLookup() (not per-frame in drawDefinitionPanel) - a
  // large HTML definition (a big scholarly dictionary entry can be 10+KB of markup) produces
  // thousands of small string/vector allocations, which was crashing on repeated re-layout (e.g. one
  // extra full re-parse+re-layout per scroll press) under heap pressure on the ESP32-C3.
  std::vector<DefinitionStyledLine> definitionLines_;
  size_t definitionScrollLine_ = 0;
  bool definitionScrollable_ = false;

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
