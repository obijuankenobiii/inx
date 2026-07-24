#include "EpubDictionaryUi.h"

#include <Epub/Page.h>
#include <Epub/PageWordIndex.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>

#include "EpubActivity.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

constexpr unsigned long kChordHoldMs = 600;
constexpr int kHighlightLatticeStepPx = 2;
constexpr unsigned long kNavEdgeDebounceMs = 130;
constexpr unsigned long kNavRepeatInitialMs = 700;
constexpr unsigned long kNavRepeatIntervalMs = 95;

// Shared between performLookup() (to lay out definitionLines_ once, at the width it'll actually be
// rendered at) and drawDefinitionPanel() (to size/draw the panel itself).
constexpr int kDefinitionPanelMargin = 16;
constexpr int kDefinitionPanelPad = 20;
// Hard cap on how much raw definition text gets parsed/laid out. A big scholarly dictionary entry can
// be 10+KB of HTML, which produces thousands of small string/vector allocations in the parser and
// layout below - that was intermittently crashing (heap exhaustion) on the ESP32-C3. This is already
// far more than fits on the panel even with scrolling, so truncating costs nothing in practice.
constexpr size_t kMaxDefinitionRawBytes = 4000;

std::string stripSurroundingPunctuation(const std::string& s) {
  size_t start = 0;
  size_t end = s.size();
  auto keep = [](unsigned char c) { return std::isalnum(c) || c == '\'' || c == '-'; };
  while (start < end && !keep(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  while (end > start && !keep(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

std::string decodeHtmlEntities(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '&') {
      const size_t semi = s.find(';', i);
      if (semi != std::string::npos && semi - i <= 10) {
        const std::string entity = s.substr(i + 1, semi - i - 1);
        if (entity == "amp") {
          out += '&';
          i = semi + 1;
          continue;
        }
        if (entity == "lt") {
          out += '<';
          i = semi + 1;
          continue;
        }
        if (entity == "gt") {
          out += '>';
          i = semi + 1;
          continue;
        }
        if (entity == "quot") {
          out += '"';
          i = semi + 1;
          continue;
        }
        if (entity == "apos" || entity == "#39") {
          out += '\'';
          i = semi + 1;
          continue;
        }
        if (entity == "nbsp") {
          out += ' ';
          i = semi + 1;
          continue;
        }
      }
    }
    out += s[i];
    ++i;
  }
  return out;
}

/** Appends c to the current run's text, collapsing runs of whitespace (including raw source
 *  newlines/tabs, which are just HTML source formatting, not real line breaks) down to a single
 *  space, and never starting a block with leading whitespace. Looks at the last character across ALL
 *  of the block's runs (not just the current one) so collapsing still works across a style change,
 *  e.g. "hello <b> world</b>" shouldn't keep the space right after <b>. Deliberate '\n' breaks (from
 *  <br>) are appended directly by the caller instead of going through this. */
void appendCollapsedChar(DefinitionBlock& block, char c) {
  if (c == '\n' || c == '\r' || c == '\t') {
    c = ' ';
  }
  char lastChar = '\0';
  for (auto it = block.runs.rbegin(); it != block.runs.rend(); ++it) {
    if (!it->text.empty()) {
      lastChar = it->text.back();
      break;
    }
  }
  if (c == ' ' && (lastChar == '\0' || lastChar == ' ' || lastChar == '\n')) {
    return;
  }
  block.runs.back().text += c;
}

/** Parses a StarDict "h" (HTML) definition into block-level chunks (paragraph/heading/list item),
 *  each holding one or more style runs, so the caller can render each block with its own font
 *  size/indent and each run with its own bold/italic style. <b>/<strong> and <i>/<em> (nestable, so
 *  e.g. <b><i>...</i></b> becomes bold-italic) set style; heading blocks are bold by default even
 *  without an explicit <b>. <br> becomes a forced line break within the current block. */
std::vector<DefinitionBlock> parseHtmlToBlocks(const std::string& html) {
  std::vector<DefinitionBlock> blocks;
  DefinitionBlock current;
  current.runs.push_back(DefinitionTextRun{});
  int boldDepth = 0;
  int italicDepth = 0;

  auto currentStyle = [&]() -> EpdFontFamily::Style {
    const bool bold = boldDepth > 0 || current.kind == DefinitionBlockKind::Heading;
    const bool italic = italicDepth > 0;
    if (bold && italic) {
      return EpdFontFamily::BOLD_ITALIC;
    }
    if (bold) {
      return EpdFontFamily::BOLD;
    }
    if (italic) {
      return EpdFontFamily::ITALIC;
    }
    return EpdFontFamily::REGULAR;
  };

  auto ensureRunStyle = [&]() {
    if (current.runs.back().style != currentStyle()) {
      current.runs.push_back(DefinitionTextRun{"", currentStyle()});
    }
  };

  auto flush = [&]() {
    // Drop trailing empty/whitespace-only runs (find_last_not_of returns npos for both cases), then
    // trim trailing whitespace off whatever real run is left at the end.
    while (!current.runs.empty() && current.runs.back().text.find_last_not_of(" \n") == std::string::npos) {
      current.runs.pop_back();
    }
    if (!current.runs.empty()) {
      std::string& t = current.runs.back().text;
      while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) {
        t.pop_back();
      }
    }
    const bool hasContent =
        std::any_of(current.runs.begin(), current.runs.end(), [](const DefinitionTextRun& r) { return !r.text.empty(); });
    if (hasContent) {
      blocks.push_back(current);
    }
    current = DefinitionBlock{};
    current.runs.push_back(DefinitionTextRun{});
    boldDepth = 0;
    italicDepth = 0;
  };

  size_t i = 0;
  while (i < html.size()) {
    if (html[i] == '<') {
      const size_t close = html.find('>', i);
      if (close == std::string::npos) {
        break;  // unterminated tag - stop rather than emit garbage
      }
      std::string tag = html.substr(i + 1, close - i - 1);
      i = close + 1;
      const bool closing = !tag.empty() && tag[0] == '/';
      if (closing) {
        tag.erase(0, 1);
      }
      const size_t space = tag.find_first_of(" \t");
      if (space != std::string::npos) {
        tag = tag.substr(0, space);
      }
      if (!tag.empty() && tag.back() == '/') {
        tag.pop_back();
      }
      std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) { return std::tolower(c); });

      if (tag == "br") {
        ensureRunStyle();
        if (!current.runs.back().text.empty() && current.runs.back().text.back() != '\n') {
          current.runs.back().text += '\n';
        }
        continue;
      }
      if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
        flush();
        if (!closing) {
          current.kind = DefinitionBlockKind::Heading;
          current.headingLevel = tag[1] - '0';
        }
        continue;
      }
      if (tag == "li") {
        flush();
        if (!closing) {
          current.kind = DefinitionBlockKind::ListItem;
        }
        continue;
      }
      if (tag == "p" || tag == "div" || tag == "ul" || tag == "ol") {
        flush();
        continue;
      }
      if (tag == "b" || tag == "strong") {
        boldDepth = closing ? std::max(0, boldDepth - 1) : boldDepth + 1;
        continue;
      }
      if (tag == "i" || tag == "em") {
        italicDepth = closing ? std::max(0, italicDepth - 1) : italicDepth + 1;
        continue;
      }
      // Any other tag (u, span, font, tt, sub, sup, etc.) - strip, keep inline text content.
      continue;
    }
    ensureRunStyle();
    appendCollapsedChar(current, html[i]);
    ++i;
  }
  flush();

  for (DefinitionBlock& block : blocks) {
    for (DefinitionTextRun& run : block.runs) {
      run.text = decodeHtmlEntities(run.text);
    }
  }
  return blocks;
}

/** Splits a block's style runs into atoms (words, carrying their run's style) plus hard-break atoms
 *  for embedded '\n's, tracking whether each atom had a space before it (false only when two runs
 *  are glued together with no space between them, e.g. a style change mid-word). */
std::vector<DefinitionTextAtom> tokenizeBlock(const DefinitionBlock& block) {
  std::vector<DefinitionTextAtom> atoms;
  bool pendingSpace = false;
  bool isFirstAtom = true;
  for (const DefinitionTextRun& run : block.runs) {
    size_t i = 0;
    while (i < run.text.size()) {
      if (run.text[i] == '\n') {
        atoms.push_back(DefinitionTextAtom{"", EpdFontFamily::REGULAR, true, false});
        ++i;
        pendingSpace = false;
        isFirstAtom = true;
        continue;
      }
      if (run.text[i] == ' ') {
        pendingSpace = true;
        ++i;
        continue;
      }
      const size_t start = i;
      while (i < run.text.size() && run.text[i] != ' ' && run.text[i] != '\n') {
        ++i;
      }
      DefinitionTextAtom atom;
      atom.text = run.text.substr(start, i - start);
      atom.style = run.style;
      atom.spaceBefore = !isFirstAtom && pendingSpace;
      atoms.push_back(std::move(atom));
      pendingSpace = false;
      isFirstAtom = false;
    }
  }
  return atoms;
}

/** Greedy word-wrap of a block's atoms into lines no wider than maxWidth, breaking only where an
 *  atom has spaceBefore (or at a hard break) so mid-word style changes never split across lines. */
std::vector<DefinitionStyledLine> wrapAtomsToWidth(const GfxRenderer& renderer,
                                                   const std::vector<DefinitionTextAtom>& atoms, const int fontId,
                                                   const int indentPx, const int maxWidth) {
  std::vector<DefinitionStyledLine> lines;
  DefinitionStyledLine current{{}, fontId, indentPx, 0};
  int currentWidth = 0;
  const int spaceW = renderer.text.getSpaceWidth(fontId);

  auto flushLine = [&]() {
    if (!current.atoms.empty()) {
      lines.push_back(std::move(current));
    }
    current = DefinitionStyledLine{{}, fontId, indentPx, 0};
    currentWidth = 0;
  };

  for (const DefinitionTextAtom& atom : atoms) {
    if (atom.hardBreak) {
      flushLine();
      continue;
    }
    const int atomW = renderer.text.getWidth(fontId, atom.text.c_str(), atom.style);
    const int extra = (atom.spaceBefore && !current.atoms.empty()) ? spaceW : 0;
    if (!current.atoms.empty() && currentWidth + extra + atomW > maxWidth) {
      flushLine();
      DefinitionTextAtom first = atom;
      first.spaceBefore = false;
      currentWidth = atomW;
      current.atoms.push_back(std::move(first));
    } else {
      currentWidth += extra + atomW;
      current.atoms.push_back(atom);
    }
  }
  flushLine();
  return lines;
}

int fontIdForBlock(const DefinitionBlock& block) {
  if (block.kind != DefinitionBlockKind::Heading) {
    return ATKINSON_HYPERLEGIBLE_10_FONT_ID;
  }
  if (block.headingLevel <= 1) {
    return ATKINSON_HYPERLEGIBLE_16_FONT_ID;
  }
  if (block.headingLevel == 2) {
    return ATKINSON_HYPERLEGIBLE_14_FONT_ID;
  }
  return ATKINSON_HYPERLEGIBLE_12_FONT_ID;
}

/** Flattens parsed HTML blocks into wrapped, styled lines with per-block font/indent/bullet, each
 *  narrowed to maxWidth (minus that block's indent) at its own font. */
std::vector<DefinitionStyledLine> layoutDefinitionBlocks(const GfxRenderer& renderer,
                                                         const std::vector<DefinitionBlock>& blocks,
                                                         const int maxWidth) {
  constexpr int kListIndentPx = 14;
  constexpr int kBlockGapPx = 4;

  std::vector<DefinitionStyledLine> styledLines;
  for (size_t bi = 0; bi < blocks.size(); ++bi) {
    const DefinitionBlock& block = blocks[bi];
    const int fontId = fontIdForBlock(block);
    const int indent = block.kind == DefinitionBlockKind::ListItem ? kListIndentPx : 0;

    auto atoms = tokenizeBlock(block);
    if (block.kind == DefinitionBlockKind::ListItem && !atoms.empty()) {
      atoms.insert(atoms.begin(), DefinitionTextAtom{"\xE2\x80\xA2", EpdFontFamily::REGULAR, false, false});
      atoms[1].spaceBefore = true;
    }

    auto wrapped = wrapAtomsToWidth(renderer, atoms, fontId, indent, maxWidth - indent);
    for (size_t li = 0; li < wrapped.size(); ++li) {
      wrapped[li].extraGapBeforePx = (li == 0 && bi > 0) ? kBlockGapPx : 0;
      styledLines.push_back(std::move(wrapped[li]));
    }
  }
  return styledLines;
}

}  // namespace

EpubDictionaryUi::EpubDictionaryUi() = default;

void EpubDictionaryUi::tryChordEnter(EpubActivity& act) {
  if (!act.epub || !act.section || mode_) {
    return;
  }
  // DOWN+LEFT (annotations already owns DOWN+RIGHT) so the two live overlays never collide.
  const bool down = act.mappedInput.rawHalIsPressed(HalGPIO::BTN_DOWN);
  const bool left = act.mappedInput.rawHalIsPressed(HalGPIO::BTN_LEFT);
  if (down && left) {
    if (chordStartMs_ == 0) {
      chordStartMs_ = millis();
    }
    if (!chordConsumed_ && millis() - chordStartMs_ >= kChordHoldMs) {
      enter(act);
      chordConsumed_ = true;
    }
  } else {
    chordStartMs_ = 0;
    chordConsumed_ = false;
  }
}

bool EpubDictionaryUi::isDuplicateNavEdge(const int dir, const unsigned long now) {
  if (lastNavEdgeDir_ == dir && (now - lastNavEdgeMs_) < kNavEdgeDebounceMs) {
    return true;
  }
  lastNavEdgeMs_ = now;
  lastNavEdgeDir_ = dir;
  return false;
}

void EpubDictionaryUi::prepareWordGeometry(EpubActivity& act) {
  if (!act.section || !act.epub) {
    return;
  }
  const ViewportInfo info = act.calculateViewport();
  const int fontId = act.bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);
  const int mt = info.totalMarginTop;
  const int ml = info.totalMarginLeft;

  auto page = act.section->loadPageFromSectionFile();
  if (!page) {
    words_.clear();
    lineFirst_.clear();
    return;
  }
  constexpr bool omitStoredWordStrings = false;
  buildPageWordIndex(*page, act.renderer, fontId, headerFontId, ml, mt, words_, &lineFirst_, omitStoredWordStrings);
}

void EpubDictionaryUi::captureFramebuffer(EpubActivity& act) {
  for (auto& ch : captureChunks_) {
    ch.reset();
  }
  captureMonolithic_.reset();
  captureUsesMonolithic_ = false;
  captureBytes_ = 0;
  captureValid_ = false;

  act.renderer.resetTransientReaderState();

  uint8_t* fb = act.renderer.getFrameBuffer();
  const size_t n = act.renderer.getBufferSize();
  if (!fb || n == 0) {
    return;
  }

  const size_t chunkCount = (n + kCaptureChunkBytes - 1) / kCaptureChunkBytes;
  captureChunks_.resize(chunkCount);

  bool chunkedOk = true;
  for (size_t i = 0; i < chunkCount; ++i) {
    const size_t offset = i * kCaptureChunkBytes;
    const size_t chunkBytes = std::min(kCaptureChunkBytes, n - offset);
    uint8_t* const buf = new (std::nothrow) uint8_t[chunkBytes];
    if (!buf) {
      chunkedOk = false;
      for (size_t j = 0; j < i; ++j) {
        captureChunks_[j].reset();
      }
      break;
    }
    memcpy(buf, fb + offset, chunkBytes);
    captureChunks_[i].reset(buf);
  }

  if (chunkedOk) {
    captureBytes_ = n;
    captureValid_ = true;
    return;
  }

  captureMonolithic_.reset(new (std::nothrow) uint8_t[n]);
  if (!captureMonolithic_) {
    return;
  }
  memcpy(captureMonolithic_.get(), fb, n);
  captureUsesMonolithic_ = true;
  captureBytes_ = n;
  captureValid_ = true;
}

void EpubDictionaryUi::enter(EpubActivity& act) {
  if (!act.section || !act.epub) {
    return;
  }
  mode_ = true;
  showingDefinition_ = false;
  lookedUpWord_.clear();
  currentDefinition_.clear();
  focus_ = 0;
  lastNavEdgeDir_ = -1;
  navRepeatDir_ = -1;

  prepareWordGeometry(act);
  if (words_.empty()) {
    act.readerPopup("No text to look up");
    exit(act);
    return;
  }
  captureFramebuffer(act);
  if (!captureValid_) {
    act.readerPopup("Could not capture page");
    exit(act);
    return;
  }
  act.updateRequired = true;
}

void EpubDictionaryUi::exit(EpubActivity& act) {
  mode_ = false;
  showingDefinition_ = false;
  lookedUpWord_.clear();
  currentDefinition_.clear();
  definitionBlocks_.clear();
  definitionLines_.clear();
  definitionScrollLine_ = 0;
  definitionScrollable_ = false;
  words_.clear();
  lineFirst_.clear();
  lastNavEdgeDir_ = -1;
  navRepeatDir_ = -1;
  for (auto& ch : captureChunks_) {
    ch.reset();
  }
  captureMonolithic_.reset();
  captureUsesMonolithic_ = false;
  captureBytes_ = 0;
  captureValid_ = false;
  act.updateRequired = true;
}

void EpubDictionaryUi::ensureDictionaryOpen() {
  if (dictOpenAttempted_) {
    return;
  }
  dictOpenAttempted_ = true;
  if (SETTINGS.dictionaryFolder[0] == '\0') {
    Serial.printf("[%lu] [DICT] ensureDictionaryOpen: SETTINGS.dictionaryFolder is empty\n", millis());
    return;
  }
  const std::string folder = std::string("/dictionaries/") + SETTINGS.dictionaryFolder;
  const bool opened = dict_.open(folder);
  Serial.printf("[%lu] [DICT] ensureDictionaryOpen: open('%s') -> %d\n", millis(), folder.c_str(), opened ? 1 : 0);
}

void EpubDictionaryUi::performLookup(EpubActivity& act) {
  if (words_.empty() || focus_ >= words_.size()) {
    return;
  }
  lookedUpWord_ = stripSurroundingPunctuation(words_[focus_].text);
  currentDefinition_.clear();
  definitionScrollLine_ = 0;

  if (lookedUpWord_.empty()) {
    currentDefinition_ = "Nothing to look up.";
  } else if (SETTINGS.dictionaryFolder[0] == '\0') {
    currentDefinition_ = "No dictionary selected. Pick one in Settings > Reader > Choose dictionary.";
  } else {
    // Show the status popup FIRST, before any blocking work - opening the dictionary (first lookup
    // only) and the SD scan itself can together take several seconds on a large dictionary, and both
    // must happen after the popup is on screen for it to actually look instant. readerPopup() forces
    // an immediate flush, same pattern as its other callers (e.g. "Deleting book...").
    act.readerPopup("Looking up...");
    ensureDictionaryOpen();
    if (!dict_.isOpen()) {
      currentDefinition_ = "Could not open the selected dictionary.";
    } else if (!dict_.lookup(lookedUpWord_, currentDefinition_)) {
      currentDefinition_ = "No definition found.";
    }
  }
  if (currentDefinition_.size() > kMaxDefinitionRawBytes) {
    currentDefinition_.resize(kMaxDefinitionRawBytes);
    // Back off from a cut that landed mid-UTF-8-codepoint (dictionaries are full of accented
    // letters, IPA symbols, en/em dashes) so we never hand a malformed byte sequence to the parser.
    while (!currentDefinition_.empty()) {
      const auto last = static_cast<unsigned char>(currentDefinition_.back());
      if ((last & 0xC0) == 0x80) {
        currentDefinition_.pop_back();  // continuation byte - still mid-sequence
        continue;
      }
      if (last >= 0xC0) {
        currentDefinition_.pop_back();  // orphaned lead byte - its continuation got cut off
      }
      break;
    }
    currentDefinition_ += " \xE2\x80\xA6";
  }
  // Plain fallback messages above have no tags, so this is a no-op for them - it only does real work
  // for an actual HTML definition. Keeps drawDefinitionPanel() dealing with a single block list always.
  definitionBlocks_ = parseHtmlToBlocks(currentDefinition_);
  // Laid out once here (not per-frame in drawDefinitionPanel) - see kMaxDefinitionRawBytes comment.
  const int textWidth =
      (act.renderer.getScreenWidth() - kDefinitionPanelMargin * 2) - kDefinitionPanelPad * 2;
  definitionLines_ = layoutDefinitionBlocks(act.renderer, definitionBlocks_, textWidth);
  showingDefinition_ = true;
  act.updateRequired = true;
}

bool EpubDictionaryUi::tryNavigationHoldRepeat(EpubActivity& act) {
  using Btn = MappedInputManager::Button;
  const MappedInputManager& m = act.mappedInput;
  const unsigned long now = millis();

  if (m.wasPressed(Btn::Left)) {
    if (isDuplicateNavEdge(0, now)) {
      return true;
    }
    moveFocusWord(-1);
    navRepeatDir_ = 0;
    navRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    return true;
  }
  if (m.wasPressed(Btn::Right)) {
    if (isDuplicateNavEdge(1, now)) {
      return true;
    }
    moveFocusWord(1);
    navRepeatDir_ = 1;
    navRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    return true;
  }
  if (m.wasPressed(Btn::Up)) {
    if (isDuplicateNavEdge(2, now)) {
      return true;
    }
    moveFocusLine(-1);
    navRepeatDir_ = 2;
    navRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    return true;
  }
  if (m.wasPressed(Btn::Down)) {
    if (isDuplicateNavEdge(3, now)) {
      return true;
    }
    moveFocusLine(1);
    navRepeatDir_ = 3;
    navRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    return true;
  }
  const bool leftHeld = m.isPressed(Btn::Left);
  const bool rightHeld = m.isPressed(Btn::Right);
  const bool upHeld = m.isPressed(Btn::Up);
  const bool downHeld = m.isPressed(Btn::Down);
  if (!leftHeld && !rightHeld && !upHeld && !downHeld) {
    navRepeatDir_ = -1;
    return false;
  }
  if (navRepeatDir_ < 0 || now < navRepeatNextMs_) {
    return false;
  }
  if (navRepeatDir_ == 0 && leftHeld) {
    moveFocusWord(-1);
  } else if (navRepeatDir_ == 1 && rightHeld) {
    moveFocusWord(1);
  } else if (navRepeatDir_ == 2 && upHeld) {
    moveFocusLine(-1);
  } else if (navRepeatDir_ == 3 && downHeld) {
    moveFocusLine(1);
  } else {
    navRepeatDir_ = -1;
    return false;
  }
  navRepeatNextMs_ = now + kNavRepeatIntervalMs;
  act.updateRequired = true;
  return true;
}

void EpubDictionaryUi::moveFocusWord(const int delta) {
  if (words_.empty()) {
    return;
  }
  if (delta < 0) {
    if (focus_ > 0) {
      focus_--;
    }
    return;
  }
  if (focus_ + 1 < words_.size()) {
    focus_++;
  }
}

void EpubDictionaryUi::moveFocusLine(const int delta) {
  if (lineFirst_.empty() || words_.empty()) {
    return;
  }
  size_t lineIdx = 0;
  for (size_t i = 0; i < lineFirst_.size(); ++i) {
    const size_t start = lineFirst_[i];
    const size_t end = (i + 1 < lineFirst_.size()) ? lineFirst_[i + 1] : words_.size();
    if (focus_ >= start && focus_ < end) {
      lineIdx = i;
      break;
    }
  }
  if (delta < 0) {
    if (lineIdx == 0) {
      return;
    }
    lineIdx--;
    const size_t end = lineFirst_[lineIdx + 1];
    focus_ = end - 1;
  } else {
    if (lineIdx + 1 >= lineFirst_.size()) {
      return;
    }
    lineIdx++;
    focus_ = lineFirst_[lineIdx];
  }
}

void EpubDictionaryUi::handleInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;

  if (m.wasReleased(MappedInputManager::Button::Back)) {
    if (showingDefinition_) {
      showingDefinition_ = false;
      act.updateRequired = true;
    } else {
      exit(act);
      act.startPageTimer();
    }
    return;
  }
  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    if (showingDefinition_) {
      showingDefinition_ = false;
    } else {
      performLookup(act);
    }
    act.updateRequired = true;
    return;
  }
  if (showingDefinition_) {
    // Word navigation is frozen while a definition is on screen; Up/Down instead scroll long
    // definitions that don't fully fit (drawDefinitionPanel clamps the range each frame).
    constexpr size_t kScrollLinesPerPress = 3;
    if (m.wasPressed(MappedInputManager::Button::Up)) {
      definitionScrollLine_ = (definitionScrollLine_ > kScrollLinesPerPress) ? definitionScrollLine_ - kScrollLinesPerPress : 0;
      act.updateRequired = true;
    } else if (m.wasPressed(MappedInputManager::Button::Down)) {
      definitionScrollLine_ += kScrollLinesPerPress;
      act.updateRequired = true;
    }
    return;
  }
  if (tryNavigationHoldRepeat(act)) {
    return;
  }
}

void EpubDictionaryUi::repaint(EpubActivity& act) {
  if (!mode_) {
    return;
  }
  const size_t n = act.renderer.getBufferSize();
  if (!captureValid_ || captureBytes_ != n) {
    act.renderScreen(true);
    return;
  }
  uint8_t* fb = act.renderer.getFrameBuffer();
  if (!fb) {
    act.renderScreen(true);
    return;
  }
  act.renderer.setRenderMode(GfxRenderer::BW);
  if (captureUsesMonolithic_) {
    if (!captureMonolithic_) {
      act.renderScreen(true);
      return;
    }
    memcpy(fb, captureMonolithic_.get(), n);
  } else {
    const size_t chunkCount = (n + kCaptureChunkBytes - 1) / kCaptureChunkBytes;
    if (captureChunks_.size() != chunkCount) {
      act.renderScreen(true);
      return;
    }
    for (size_t i = 0; i < chunkCount; ++i) {
      const size_t offset = i * kCaptureChunkBytes;
      const size_t chunkBytes = std::min(kCaptureChunkBytes, n - offset);
      if (!captureChunks_[i]) {
        act.renderScreen(true);
        return;
      }
      memcpy(fb + offset, captureChunks_[i].get(), chunkBytes);
    }
  }
  drawUiOverlay(act);
}

void EpubDictionaryUi::drawFocusHighlight(EpubActivity& act) {
  if (words_.empty() || focus_ >= words_.size()) {
    return;
  }
  const PageWordHit& w = words_[focus_];
  act.renderer.ui.fillSparseInkLatticeInRect(w.screenX, std::max(0, w.screenY), std::max(1, w.screenW),
                                             std::max(3, w.screenH), kHighlightLatticeStepPx);
}

void EpubDictionaryUi::drawDefinitionPanel(EpubActivity& act) {
  const int screenW = act.renderer.getScreenWidth();
  const int screenH = act.renderer.getScreenHeight();
  constexpr int margin = kDefinitionPanelMargin;
  constexpr int pad = kDefinitionPanelPad;
  const int panelX = margin;
  const int panelW = screenW - margin * 2;
  const int panelBottom = screenH - margin - 40;  // leave room for the button-hint row below
  const int defaultPanelTop = screenH * 2 / 5;    // panel height used for short definitions
  const int minPanelTop = margin;                 // panel can grow up to near the top of the screen

  const int titleFontId = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
  const int titleH = act.renderer.text.getLineHeight(titleFontId);
  // definitionLines_ is computed once per lookup (performLookup()), not recomputed here every frame.
  const auto& styledLines = definitionLines_;

  int contentH = 0;
  for (const DefinitionStyledLine& sl : styledLines) {
    contentH += act.renderer.text.getLineHeight(sl.fontId) + sl.extraGapBeforePx;
  }

  // Grow the panel to fit the content (up to minPanelTop), instead of always using the default size
  // and truncating - only falls back to scrolling if the content doesn't fit even at max height.
  constexpr int kTitleGapPx = 8;  // gap above and below the separator line under the title
  const int neededPanelH = pad * 2 + titleH + kTitleGapPx * 2 + contentH;
  const int defaultPanelH = panelBottom - defaultPanelTop;
  const int maxPanelH = panelBottom - minPanelTop;
  const int panelH = std::min(maxPanelH, std::max(defaultPanelH, neededPanelH));
  const int panelTop = panelBottom - panelH;

  // Same sharp-corner white-fill + black-border panel style as the menu/settings drawers
  // (MenuDrawer/SettingsDrawer background), not a rounded popup box.
  act.renderer.rectangle.fill(panelX, panelTop, panelW, panelH, false);
  act.renderer.rectangle.render(panelX, panelTop, panelW, panelH, true);

  int y = panelTop + pad + titleH;
  act.renderer.text.render(titleFontId, panelX + pad, y - titleH, lookedUpWord_.c_str(), true, EpdFontFamily::BOLD);
  y += kTitleGapPx;
  act.renderer.line.render(panelX + pad, y, panelX + panelW - pad, y, true, LineRender::Style::Dotted);
  y += kTitleGapPx;

  const int contentBottom = panelTop + panelH - pad;
  const int availableH = contentBottom - y;

  // Clamp scroll so the last screenful is always fully populated - walk backward from the end,
  // accumulating line heights, to find the furthest offset that still fills the available height.
  int maxScrollLine = 0;
  {
    int hFromEnd = 0;
    int idx = static_cast<int>(styledLines.size()) - 1;
    while (idx >= 0) {
      const int lh = act.renderer.text.getLineHeight(styledLines[idx].fontId) + styledLines[idx].extraGapBeforePx;
      if (hFromEnd + lh > availableH) {
        break;
      }
      hFromEnd += lh;
      --idx;
    }
    maxScrollLine = idx + 1;
  }
  definitionScrollable_ = maxScrollLine > 0;
  definitionScrollLine_ = std::min(definitionScrollLine_, static_cast<size_t>(maxScrollLine));

  for (size_t i = definitionScrollLine_; i < styledLines.size(); ++i) {
    const DefinitionStyledLine& sl = styledLines[i];
    const int lineH = act.renderer.text.getLineHeight(sl.fontId);
    const int gap = (i == definitionScrollLine_) ? 0 : sl.extraGapBeforePx;
    if (y + gap + lineH > contentBottom) {
      break;
    }
    y += gap;
    int x = panelX + pad + sl.indentPx;
    const int spaceW = act.renderer.text.getSpaceWidth(sl.fontId);
    for (size_t ai = 0; ai < sl.atoms.size(); ++ai) {
      const DefinitionTextAtom& atom = sl.atoms[ai];
      if (ai > 0 && atom.spaceBefore) {
        x += spaceW;
      }
      act.renderer.text.render(sl.fontId, x, y, atom.text.c_str(), true, atom.style);
      x += act.renderer.text.getWidth(sl.fontId, atom.text.c_str(), atom.style);
    }
    y += lineH;
  }
}

void EpubDictionaryUi::drawUiOverlay(EpubActivity& act) {
  if (!mode_) {
    return;
  }
  const GfxRenderer::Orientation o = act.renderer.getOrientation();
  if (showingDefinition_) {
    drawDefinitionPanel(act);
  } else {
    drawFocusHighlight(act);
  }
  act.renderer.setOrientation(GfxRenderer::Portrait);
  const char* mid = showingDefinition_ ? "Close" : "Look up";
  const auto labels = act.mappedInput.mapLabels("Exit", mid, "Prev", "Next");
  act.renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  const bool showUpDown = !showingDefinition_ || definitionScrollable_;
  act.renderer.ui.sideButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "", showUpDown ? "Up" : "", showUpDown ? "Down" : "");
  act.renderer.setOrientation(o);
  act.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
