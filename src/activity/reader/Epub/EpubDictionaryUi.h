#pragma once

#include <EpdFontFamily.h>
#include <Epub/PageWordIndex.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "dictionary/StarDictLookup.h"

class EpubActivity;

enum class DefinitionBlockKind : uint8_t { Paragraph, Heading, ListItem };

/** A run of text within a block that shares one font style (bold/italic/bold-italic/regular, from
 *  nested <b>/<strong>/<i>/<em> tags). May contain '\n' from <br>. */
struct DefinitionTextRun {
  DefinitionTextRun() = default;
  DefinitionTextRun(std::string t, EpdFontFamily::Style s) : text(std::move(t)), style(s) {}

  std::string text;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
};

/** One block-level chunk of a parsed StarDict "h" (HTML) definition - a paragraph, heading, or list
 *  item, laid out with its own font size/indent at render time; each run within it may still carry
 *  its own bold/italic style. */
struct DefinitionBlock {
  DefinitionBlockKind kind = DefinitionBlockKind::Paragraph;
  int headingLevel = 1;  // 1-6, only meaningful when kind == Heading
  std::vector<DefinitionTextRun> runs;
};

/** One atom to render within a wrapped line: a word (or word fragment, if a style change happened
 *  mid-word) in one style, or a hard line break with text empty. spaceBefore marks whether a space
 *  should be rendered/measured before it when it's not the first atom on a line. */
struct DefinitionTextAtom {
  DefinitionTextAtom() = default;
  DefinitionTextAtom(std::string t, EpdFontFamily::Style s, bool hb, bool sb)
      : text(std::move(t)), style(s), hardBreak(hb), spaceBefore(sb) {}

  std::string text;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  bool hardBreak = false;
  bool spaceBefore = false;
};

/** One already-wrapped, already-styled line ready to render in the definition panel. */
struct DefinitionStyledLine {
  DefinitionStyledLine() = default;
  DefinitionStyledLine(std::vector<DefinitionTextAtom> a, int f, int indent, int gap)
      : atoms(std::move(a)), fontId(f), indentPx(indent), extraGapBeforePx(gap) {}

  std::vector<DefinitionTextAtom> atoms;
  int fontId = 0;
  int indentPx = 0;
  int extraGapBeforePx = 0;
};

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
