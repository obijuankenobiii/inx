#pragma once

#include <Epub/PageWordIndex.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "dictionary/DictionaryDefinitionLayout.h"

class EpubActivity;

/**
 * Footnote lookup UI: quick-action entry, Left/Right navigation between footnote markers on the
 * current page, and a body popup - same interaction shape as EpubDictionaryUi (see that file), except
 * words_/focus_ only ever holds footnote-marker words (filtered from the full page word index), and
 * there's no Up/Down line navigation - footnote markers are sparse, so "next/previous marker" via
 * Left/Right is all that's needed.
 */
class EpubFootnoteUi {
 public:
  EpubFootnoteUi();

  bool isActive() const { return mode_; }

  void enter(EpubActivity& act);
  void exit(EpubActivity& act);
  void handleInput(EpubActivity& act);
  void repaint(EpubActivity& act);
  void drawUiOverlay(EpubActivity& act);

#ifdef SIMULATOR
  // Diagnostic-only direct entry point for env:selftest - lets the headless repro driver exercise the
  // exact body-resolution path without faking button input through HalGPIO/MappedInputManager.
  void debugResolveFootnoteBodyForSelftest(EpubActivity& act) { resolveFootnoteBody(act); }
  size_t debugWordCountForSelftest() const { return words_.size(); }
#endif

 private:
  void prepareWordGeometry(EpubActivity& act);
  void captureFramebuffer(EpubActivity& act);
  void moveFocusWord(int delta);
  bool tryNavigationHoldRepeat(EpubActivity& act);
  bool isDuplicateNavEdge(int dir, unsigned long now);
  void drawFocusHighlight(EpubActivity& act);
  void drawBodyPanel(EpubActivity& act);
  /** Resolves the focused marker's footnote body (same-document or cross-file - both go through
   * Epub::readItemContentsToStream + extractElementInnerHtmlById) and lays it out for the panel. */
  void resolveFootnoteBody(EpubActivity& act);
  /** Extracts one footnote's body text from an EPUB manifest item. Writes the item to a temp SD file
   * (bounded ~1KB chunks, via Epub::readItemContentsToStream - same pattern as
   * Epub::parseTocNcxFile/parseTocNavFile) rather than holding it in RAM, then does a bounded, chunked
   * on-SD search for id="fragmentId" using small fixed-size stack buffers - so peak memory never
   * scales with the source item's size, however large a book's notes/endnotes chapter turns out to be.
   * Returns "" if the item couldn't be loaded or the id wasn't found. */
  std::string loadFootnoteBodyText(EpubActivity& act, const std::string& internalPath,
                                   const std::string& fragmentId);
  /** Actually releases bodyBlocks_/bodyLines_'s heap capacity - see EpubDictionaryUi's
   *  releaseDefinitionMemory() for why .clear() alone isn't enough. */
  void releaseBodyMemory();

  bool mode_ = false;
  std::vector<PageWordHit> words_;  // filtered to footnote-marker words only
  size_t focus_ = 0;

  bool showingBody_ = false;
  std::vector<DefinitionBlock> bodyBlocks_;
  std::vector<DefinitionStyledLine> bodyLines_;
  size_t bodyScrollLine_ = 0;
  bool bodyScrollable_ = false;

  static constexpr size_t kCaptureChunkBytes = 8000;
  std::vector<std::unique_ptr<uint8_t[]>> captureChunks_{};
  std::unique_ptr<uint8_t[]> captureMonolithic_{};
  bool captureUsesMonolithic_ = false;
  size_t captureBytes_ = 0;
  bool captureValid_ = false;

  unsigned long lastNavEdgeMs_ = 0;
  int lastNavEdgeDir_ = -1;
  int navRepeatDir_ = -1;
  unsigned long navRepeatNextMs_ = 0;
};
