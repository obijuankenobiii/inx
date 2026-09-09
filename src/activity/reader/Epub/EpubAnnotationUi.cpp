#include "EpubAnnotationUi.h"

#include <Epub/Page.h>
#include <Epub/PageWordIndex.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <ctime>
#include <new>

#include "EpubActivity.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

#include <Epub/Section.h>

namespace {

constexpr unsigned long kChordHoldMs = 600;
constexpr int kHighlightLatticeStepPx = 2;
/** ADC/button bounce can deliver two wasPressed edges ~ms apart; loop has no delay — suppress 2nd edge. */
constexpr unsigned long kNavEdgeDebounceMs = 130;
constexpr unsigned long kNavRepeatInitialMs = 700;
constexpr unsigned long kNavRepeatIntervalMs = 95;

int cmpLoc(const int s1, const int p1, const size_t w1, const int s2, const int p2, const size_t w2) {
  if (s1 != s2) {
    return s1 < s2 ? -1 : 1;
  }
  if (p1 != p2) {
    return p1 < p2 ? -1 : 1;
  }
  if (w1 != w2) {
    return w1 < w2 ? -1 : 1;
  }
  return 0;
}

HighlightSpan normalizedSpan(HighlightSpan span) {
  if (cmpLoc(span.startSpine, span.startPage, span.startWord, span.endSpine, span.endPage, span.endWord) > 0) {
    std::swap(span.startSpine, span.endSpine);
    std::swap(span.startPage, span.endPage);
    std::swap(span.startWord, span.endWord);
  }
  return span;
}

std::string joinWords(const std::vector<PageWordHit>& words, const size_t lo, const size_t hi) {
  std::string out;
  for (size_t i = lo; i <= hi && i < words.size(); ++i) {
    if (!out.empty()) {
      out += ' ';
    }
    out += words[i].text;
  }
  return out;
}

}  // namespace

EpubAnnotationUi::EpubAnnotationUi() = default;

bool EpubAnnotationUi::fillWordsForPage(EpubActivity& act, const int spine, const int page,
                                        std::vector<PageWordHit>& out) const {
  out.clear();
  if (!act.epub) {
    return false;
  }
  auto pageObj = Section::loadCachedPage(act.epub->getCachePath(), spine, page);
  if (!pageObj) {
    return false;
  }
  const ViewportInfo info = act.calculateViewport();
  const int fontId = act.bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);
  buildPageWordIndex(*pageObj, act.renderer, fontId, headerFontId, info.totalMarginLeft, info.totalMarginTop, out,
                     nullptr, false);
  return !out.empty();
}

HighlightSpan EpubAnnotationUi::liveSelectionSpan(const EpubActivity& act) const {
  HighlightSpan span;
  span.startSpine = selAnchorSpine_ >= 0 ? selAnchorSpine_ : act.currentSpineIndex;
  span.startPage = selAnchorPage_ >= 0 ? selAnchorPage_ : (act.section ? act.section->currentPage : 0);
  span.startWord = anchor_;
  span.endSpine = act.currentSpineIndex;
  span.endPage = act.section ? act.section->currentPage : 0;
  span.endWord = focus_;
  return normalizedSpan(span);
}

std::string EpubAnnotationUi::extractSpanText(EpubActivity& act, const HighlightSpan& raw) const {
  const HighlightSpan span = normalizedSpan(raw);
  if (span.startSpine == span.endSpine && span.startPage == span.endPage) {
    if (act.section && act.currentSpineIndex == span.startSpine && act.section->currentPage == span.startPage &&
        !words_.empty()) {
      return extractRangeText(span.startWord, span.endWord);
    }
    std::vector<PageWordHit> pageWords;
    if (!fillWordsForPage(act, span.startSpine, span.startPage, pageWords) || pageWords.empty()) {
      return {};
    }
    const size_t last = pageWords.size() - 1;
    return joinWords(pageWords, std::min(span.startWord, last), std::min(span.endWord, last));
  }

  std::string out;
  int s = span.startSpine;
  int p = span.startPage;
  const int maxSpine = act.epub ? act.epub->getSpineItemsCount() : span.endSpine + 1;
  for (int guard = 0; guard < 400; ++guard) {
    std::vector<PageWordHit> pageWords;
    if (fillWordsForPage(act, s, p, pageWords) && !pageWords.empty()) {
      const size_t last = pageWords.size() - 1;
      size_t lo = 0;
      size_t hi = last;
      if (s == span.startSpine && p == span.startPage) {
        lo = std::min(span.startWord, last);
      }
      if (s == span.endSpine && p == span.endPage) {
        hi = std::min(span.endWord, last);
      }
      if (lo <= hi) {
        const std::string slice = joinWords(pageWords, lo, hi);
        if (!slice.empty()) {
          if (!out.empty()) {
            out += ' ';
          }
          out += slice;
        }
      }
    }
    if (s == span.endSpine && p == span.endPage) {
      break;
    }
    if (!act.epub) {
      break;
    }
    if (Section::loadCachedPage(act.epub->getCachePath(), s, p + 1)) {
      ++p;
      continue;
    }
    ++s;
    p = 0;
    if (s > span.endSpine || s >= maxSpine) {
      break;
    }
  }
  return out;
}

void EpubAnnotationUi::setWordIndexCache(const int spine, const int page, const int fontId, const int headerFontId,
                                         const int marginL, const int marginT) {
  wordIndexCacheSpine_ = spine;
  wordIndexCachePage_ = page;
  wordIndexCacheFontId_ = fontId;
  wordIndexCacheHeaderFontId_ = headerFontId;
  wordIndexCacheMarginL_ = marginL;
  wordIndexCacheMarginT_ = marginT;
}

void EpubAnnotationUi::clearWordIndexCache() {
  wordIndexCacheSpine_ = -1;
  wordIndexCachePage_ = -1;
  wordIndexCacheFontId_ = -1;
  wordIndexCacheHeaderFontId_ = -1;
  wordIndexCacheMarginL_ = INT_MIN;
  wordIndexCacheMarginT_ = INT_MIN;
}

void EpubAnnotationUi::clearSessionAndCapture() {
  annotations_.clearSession();
  std::vector<HighlightSpan>().swap(pendingSpans_);
  for (auto& ch : captureChunks_) {
    ch.reset();
  }
  std::vector<std::unique_ptr<uint8_t[]>>().swap(captureChunks_);
  captureMonolithic_.reset();
  captureUsesMonolithic_ = false;
  captureBytes_ = 0;
  captureValid_ = false;
  clearWordIndexCache();
}

void EpubAnnotationUi::tryChordEnter(EpubActivity& act) {
  if (!act.epub || !act.section || mode_) {
    return;
  }
  const bool down = act.mappedInput.rawHalIsPressed(HalGPIO::BTN_DOWN);
  const bool right = act.mappedInput.rawHalIsPressed(HalGPIO::BTN_RIGHT);
  if (down && right) {
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

bool EpubAnnotationUi::isDuplicateNavEdge(const int dir, const unsigned long now) {
  if (annLastNavEdgeDir_ == dir && (now - annLastNavEdgeMs_) < kNavEdgeDebounceMs) {
    return true;
  }
  annLastNavEdgeMs_ = now;
  annLastNavEdgeDir_ = dir;
  return false;
}

bool EpubAnnotationUi::hasSaveableContent() const {
  if (!pendingSpans_.empty()) {
    return true;
  }
  return selectingStarted_ && !words_.empty();
}

void EpubAnnotationUi::resetSelectionToStart(EpubActivity& act) {
  pendingSpans_.clear();
  selectingStarted_ = false;
  selAnchorSpine_ = -1;
  selAnchorPage_ = -1;
  focus_ = 0;
  anchor_ = 0;
  act.updateRequired = true;
}

void EpubAnnotationUi::clearAllStoredHighlightsOnCurrentPage(EpubActivity& act) {
  if (!act.epub || !act.section) {
    return;
  }
  annotations_.clearPageShard(act.epub->getCachePath(), act.currentSpineIndex, act.section->currentPage);
  storedRanges_.clear();
  pendingSpans_.clear();
  selectingStarted_ = false;
  selAnchorSpine_ = -1;
  selAnchorPage_ = -1;
  focus_ = 0;
  anchor_ = 0;
  // Force full word-index rebuild so merge/geometry cannot reuse state tied to the deleted highlights.
  clearWordIndexCache();
  // Full redraw clears lattice from the framebuffer; then re-capture for annotation repaint path.
  // Suppress drawUiOverlay() during this specific render - otherwise it redraws a cursor box at
  // focus_ (word 0) before the capture, baking a stale highlight permanently into the "clean"
  // snapshot that every later repaint() restores from, regardless of where focus_ moves next.
  suppressOverlayDraw_ = true;
  act.renderScreen(true);
  suppressOverlayDraw_ = false;
  captureFramebuffer(act);
  act.updateRequired = true;
}

void EpubAnnotationUi::normalizeSpans(std::vector<HighlightSpan>& spans) {
  if (spans.empty()) {
    return;
  }
  for (HighlightSpan& span : spans) {
    span = normalizedSpan(span);
  }
  std::sort(spans.begin(), spans.end(), [](const HighlightSpan& a, const HighlightSpan& b) {
    const int c = cmpLoc(a.startSpine, a.startPage, a.startWord, b.startSpine, b.startPage, b.startWord);
    if (c != 0) {
      return c < 0;
    }
    return cmpLoc(a.endSpine, a.endPage, a.endWord, b.endSpine, b.endPage, b.endWord) < 0;
  });
  size_t write = 0;
  HighlightSpan cur = spans[0];
  for (size_t i = 1; i < spans.size(); ++i) {
    const bool samePage = cur.startSpine == cur.endSpine && cur.startPage == cur.endPage &&
                          spans[i].startSpine == spans[i].endSpine && spans[i].startPage == spans[i].endPage &&
                          cur.startSpine == spans[i].startSpine && cur.startPage == spans[i].startPage;
    if (samePage && spans[i].startWord <= cur.endWord + 1) {
      cur.endWord = std::max(cur.endWord, spans[i].endWord);
    } else {
      spans[write++] = cur;
      cur = spans[i];
    }
  }
  spans[write++] = cur;
  spans.resize(write);
}

void EpubAnnotationUi::enter(EpubActivity& act) {
  if (!act.section || !act.epub) {
    return;
  }
  // The Down+Right entry chord (and a plain long-press Down) leave the button held while
  // handleInput() is about to stop running for the whole overlay session - reset its per-button
  // state now so it doesn't misfire a stale long-press the instant this overlay exits.
  act.btnBindings_.reset();
  mode_ = true;
  selectingStarted_ = false;
  pendingSpans_.clear();
  annLastNavEdgeDir_ = -1;
  annNavRepeatDir_ = -1;
  selAnchorSpine_ = -1;
  selAnchorPage_ = -1;
  anchor_ = 0;
  focus_ = 0;
  // Build word index first, then capture the framebuffer. Allocating the 48k capture before the word index
  // (many strings + PageWordHit) spikes heap usage and can abort() on OOM on ESP32.
  prepareWordGeometry(act);
  if (words_.empty()) {
    act.readerPopup("No text to highlight");
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

void EpubAnnotationUi::exit(EpubActivity& act) {
  mode_ = false;
  selectingStarted_ = false;
  selAnchorSpine_ = -1;
  selAnchorPage_ = -1;
  // swap, not .clear() - .clear() empties the contents but keeps the heap capacity reserved for
  // reuse; a page with many highlights/words can grow these well past what's needed once the UI
  // closes (same fix as EpubDictionaryUi's releaseDefinitionMemory()).
  std::vector<HighlightSpan>().swap(pendingSpans_);
  std::vector<std::pair<size_t, size_t>>().swap(storedRanges_);
  std::vector<PageWordHit>().swap(words_);
  std::vector<size_t>().swap(lineFirst_);
  clearWordIndexCache();
  annLastNavEdgeDir_ = -1;
  annNavRepeatDir_ = -1;
  for (auto& ch : captureChunks_) {
    ch.reset();
  }
  std::vector<std::unique_ptr<uint8_t[]>>().swap(captureChunks_);
  captureMonolithic_.reset();
  captureUsesMonolithic_ = false;
  captureBytes_ = 0;
  captureValid_ = false;
  act.updateRequired = true;
}

bool EpubAnnotationUi::tryNavigationHoldRepeat(EpubActivity& act) {
  using Btn = MappedInputManager::Button;
  const MappedInputManager& m = act.mappedInput;
  const unsigned long now = millis();

  // One edge = one move. Holding the same direction starts repeat only after a long enough delay
  // that a normal click cannot jump two words/lines.
  if (m.wasPressed(Btn::Left)) {
    if (isDuplicateNavEdge(0, now)) {
      return true;
    }
    const bool atStart = words_.empty() || focus_ == 0;
    moveFocusWord(-1);
    if (atStart) {
      pageTurnFromHighlight(act, false);
    }
    annNavRepeatDir_ = 0;
    annNavRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    return true;
  }
  if (m.wasPressed(Btn::Right)) {
    if (isDuplicateNavEdge(1, now)) {
      return true;
    }
    const bool atEnd = !words_.empty() && focus_ == words_.size() - 1;
    moveFocusWord(1);
    if (atEnd) {
      pageTurnFromHighlight(act, true);
    }
    annNavRepeatDir_ = 1;
    annNavRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    return true;
  }
  if (m.wasPressed(Btn::Up)) {
    if (isDuplicateNavEdge(2, now)) {
      return true;
    }
    moveFocusLine(-1);
    annNavRepeatDir_ = 2;
    annNavRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    return true;
  }
  if (m.wasPressed(Btn::Down)) {
    if (isDuplicateNavEdge(3, now)) {
      return true;
    }
    moveFocusLine(1);
    annNavRepeatDir_ = 3;
    annNavRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    return true;
  }
  const bool leftHeld = m.isPressed(Btn::Left);
  const bool rightHeld = m.isPressed(Btn::Right);
  const bool upHeld = m.isPressed(Btn::Up);
  const bool downHeld = m.isPressed(Btn::Down);
  if (!leftHeld && !rightHeld && !upHeld && !downHeld) {
    annNavRepeatDir_ = -1;
    return false;
  }
  if (annNavRepeatDir_ < 0 || now < annNavRepeatNextMs_) {
    return false;
  }
  if (annNavRepeatDir_ == 0 && leftHeld) {
    moveFocusWord(-1);
  } else if (annNavRepeatDir_ == 1 && rightHeld) {
    moveFocusWord(1);
  } else if (annNavRepeatDir_ == 2 && upHeld) {
    // Auto-repeat covers 2 lines/tick (vs. 1 for the initial press) - holding Up/Down would
    // otherwise take forever to cross a full page at kNavRepeatIntervalMs.
    moveFocusLine(-1);
    moveFocusLine(-1);
  } else if (annNavRepeatDir_ == 3 && downHeld) {
    moveFocusLine(1);
    moveFocusLine(1);
  } else {
    annNavRepeatDir_ = -1;
    return false;
  }
  annNavRepeatNextMs_ = now + kNavRepeatIntervalMs;
  act.updateRequired = true;
  return true;
}

std::string EpubAnnotationUi::extractRangeText(const size_t anchorFlat, const size_t focusFlat) const {
  if (words_.empty()) {
    return {};
  }
  const size_t lo = std::min(anchorFlat, focusFlat);
  const size_t hi = std::max(anchorFlat, focusFlat);
  std::string out;
  for (size_t i = lo; i <= hi && i < words_.size(); ++i) {
    if (!out.empty()) {
      out += ' ';
    }
    out += words_[i].text;
  }
  return out;
}

void EpubAnnotationUi::drawLatticeHighlightRect(EpubActivity& act, const int x, const int y, const int width,
                                                const int height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  act.renderer.ui.fillSparseInkLatticeInRect(x, std::max(0, y), width, height, kHighlightLatticeStepPx);
}

void EpubAnnotationUi::drawLatticeHighlightForWordIndexRange(EpubActivity& act, const size_t lo, const size_t hi) {
  if (words_.empty() || lo > hi || hi >= words_.size()) {
    return;
  }
  size_t a = lo;
  while (a <= hi) {
    const int lineY = words_[a].screenY;
    size_t b = a + 1;
    int minX = words_[a].screenX;
    int maxR = words_[a].screenX + words_[a].screenW;
    const int fid0 = words_[a].fontId > 0 ? words_[a].fontId : act.bookSettings.getReaderFontId();
    int rowH = std::max(3, words_[a].screenH > 0 ? words_[a].screenH : act.renderer.text.getLineHeight(fid0));
    while (b <= hi && words_[b].screenY == lineY) {
      minX = std::min(minX, words_[b].screenX);
      maxR = std::max(maxR, words_[b].screenX + words_[b].screenW);
      const int fid = words_[b].fontId > 0 ? words_[b].fontId : act.bookSettings.getReaderFontId();
      const int lh = std::max(3, words_[b].screenH > 0 ? words_[b].screenH : act.renderer.text.getLineHeight(fid));
      rowH = std::max(rowH, lh);
      ++b;
    }
    drawLatticeHighlightRect(act, minX, lineY, std::max(1, maxR - minX), rowH);
    a = b;
  }
}

void EpubAnnotationUi::ensureDiskListLoaded(EpubActivity& act) {
  if (!act.epub || !act.section) {
    return;
  }
  annotations_.ensurePageLoaded(act.epub->getCachePath(), act.currentSpineIndex, act.section->currentPage);
}

void EpubAnnotationUi::updateStoredRangesForPage(const EpubActivity& act) {
  if (!act.section) {
    storedRanges_.clear();
    return;
  }
  EpubAnnotations::mergeStoredRangesForPage(annotations_.records(), act.currentSpineIndex, act.section->currentPage,
                                            words_, storedRanges_);
}

void EpubAnnotationUi::clampSelectionToValidWords() {
  if (words_.empty()) {
    return;
  }
  const size_t last = words_.size() - 1;
  focus_ = std::min(focus_, last);
  if (selectingStarted_ && selAnchorSpine_ == wordIndexCacheSpine_ && selAnchorPage_ == wordIndexCachePage_) {
    anchor_ = std::min(anchor_, last);
  }
}

void EpubAnnotationUi::prepareWordGeometry(EpubActivity& act) {
  if (!act.section || !act.epub) {
    return;
  }
  ensureDiskListLoaded(act);
  const ViewportInfo info = act.calculateViewport();
  const int fontId = act.bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);
  const int mt = info.totalMarginTop;
  const int ml = info.totalMarginLeft;

  const bool wordIndexCacheHit = wordIndexCacheSpine_ == act.currentSpineIndex &&
                                 wordIndexCachePage_ == act.section->currentPage && wordIndexCacheFontId_ == fontId &&
                                 wordIndexCacheHeaderFontId_ == headerFontId && wordIndexCacheMarginL_ == ml &&
                                 wordIndexCacheMarginT_ == mt;

  const bool anyWordText =
      std::any_of(words_.begin(), words_.end(), [](const PageWordHit& w) { return !w.text.empty(); });

  // Reading mode may build the index with omitStoredWordStrings (no per-word strings). Annotation needs strings for
  // extractRangeText / save — force rebuild when the cache has words but no text.
  if (wordIndexCacheHit && !words_.empty() && anyWordText) {
    storedRanges_.clear();
    focus_ = std::min(focus_, words_.size() - 1);
    if (selectingStarted_) {
      anchor_ = std::min(anchor_, words_.size() - 1);
    }
    return;
  }

  storedRanges_.clear();
  auto page = act.section->loadPageFromSectionFile();
  if (!page) {
    words_.clear();
    lineFirst_.clear();
    return;
  }
  constexpr bool omitStoredWordStrings = false;
  buildPageWordIndex(*page, act.renderer, fontId, headerFontId, ml, mt, words_, &lineFirst_, omitStoredWordStrings);
  setWordIndexCache(act.currentSpineIndex, act.section->currentPage, fontId, headerFontId, ml, mt);
}

void EpubAnnotationUi::captureFramebuffer(EpubActivity& act) {
  for (auto& ch : captureChunks_) {
    ch.reset();
  }
  captureMonolithic_.reset();
  captureUsesMonolithic_ = false;
  captureBytes_ = 0;
  captureValid_ = false;

  // Free GfxRenderer's grayscale/BW-shadow chunks (~48KB) if a prior path left them allocated — otherwise capture often
  // fails trying to duplicate the framebuffer while heap is still holding that copy.
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

void EpubAnnotationUi::repaint(EpubActivity& act) {
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

void EpubAnnotationUi::drawStoredOverlay(EpubActivity& act) {
  if (storedRanges_.empty()) {
    return;
  }
  for (const auto& pr : storedRanges_) {
    drawLatticeHighlightForWordIndexRange(act, pr.first, pr.second);
  }
  act.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void EpubAnnotationUi::drawSpanOnCurrentPage(EpubActivity& act, const HighlightSpan& raw) {
  if (words_.empty() || !act.section) {
    return;
  }
  const HighlightSpan span = normalizedSpan(raw);
  const int cs = act.currentSpineIndex;
  const int cp = act.section->currentPage;
  if (cmpLoc(cs, cp, 0, span.startSpine, span.startPage, 0) < 0) {
    return;
  }
  if (cmpLoc(cs, cp, 0, span.endSpine, span.endPage, 0) > 0) {
    return;
  }
  const size_t last = words_.size() - 1;
  size_t lo = 0;
  size_t hi = last;
  if (cs == span.startSpine && cp == span.startPage) {
    lo = std::min(span.startWord, last);
  }
  if (cs == span.endSpine && cp == span.endPage) {
    hi = std::min(span.endWord, last);
  }
  if (lo > hi) {
    return;
  }
  drawLatticeHighlightForWordIndexRange(act, lo, hi);
}

void EpubAnnotationUi::drawHighlights(EpubActivity& act) {
  if (!mode_ || words_.empty()) {
    return;
  }
  for (const auto& pr : pendingSpans_) {
    drawSpanOnCurrentPage(act, pr);
  }
  if (selectingStarted_) {
    drawSpanOnCurrentPage(act, liveSelectionSpan(act));
    return;
  }
  if (focus_ < words_.size()) {
    drawLatticeHighlightForWordIndexRange(act, focus_, focus_);
  }
}

void EpubAnnotationUi::drawUiOverlay(EpubActivity& act) {
  if (!mode_ || suppressOverlayDraw_) {
    return;
  }
  const GfxRenderer::Orientation o = act.renderer.getOrientation();
  drawHighlights(act);
  act.renderer.setOrientation(GfxRenderer::Portrait);
  const char* backHint = hasSaveableContent() ? "Save" : "Exit";
  const char* mid = selectingStarted_ ? "Stop" : "Start";
  const auto labels = act.mappedInput.mapLabels(backHint, mid, "Prev", "Next");
  act.renderer.ui.buttonHints(MONTSERRAT_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  act.renderer.ui.sideButtonHints(MONTSERRAT_10_FONT_ID, "Reset", "Up", "Down");
  act.renderer.setOrientation(o);
  act.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void EpubAnnotationUi::moveFocusWord(const int delta) {
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

void EpubAnnotationUi::moveFocusLine(const int delta) {
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
    focus_ = lineFirst_[lineIdx];
  } else {
    if (lineIdx + 1 >= lineFirst_.size()) {
      return;
    }
    lineIdx++;
    focus_ = lineFirst_[lineIdx];
  }
}

bool EpubAnnotationUi::canPageTurnFromHighlight(EpubActivity& act, const bool forward) const {
  if (!act.epub || !act.section) {
    return false;
  }
  if (forward) {
    if (act.section->pageCount > 0 && act.section->currentPage + 1 < act.section->pageCount) {
      return true;
    }
    return act.currentSpineIndex + 1 < act.epub->getSpineItemsCount();
  }
  if (act.section->currentPage > 0) {
    return true;
  }
  return act.currentSpineIndex > 0;
}

bool EpubAnnotationUi::pageTurnFromHighlight(EpubActivity& act, const bool forward) {
  if (!canPageTurnFromHighlight(act, forward)) {
    return false;
  }
  suppressOverlayDraw_ = true;
  act.pageTurn(forward);
  act.renderScreen(true);
  suppressOverlayDraw_ = false;
  captureFramebuffer(act);
  if (words_.empty()) {
    focus_ = 0;
  } else if (forward) {
    focus_ = 0;
  } else {
    focus_ = words_.size() - 1;
  }
  return true;
}

void EpubAnnotationUi::handleInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;

  if (m.wasReleased(MappedInputManager::Button::Power)) {
    const unsigned long ht = m.getHeldTime();
    if (ht < 600) {
      const bool hadSavedFile =
          act.epub && act.section &&
          annotations_.pageShardExists(act.epub->getCachePath(), act.currentSpineIndex, act.section->currentPage);
      if (hadSavedFile) {
        clearAllStoredHighlightsOnCurrentPage(act);
      } else {
        resetSelectionToStart(act);
      }
      return;
    }
  }
  if (m.wasReleased(MappedInputManager::Button::Back)) {
    if (hasSaveableContent()) {
      saveToStorage(act);
    } else {
      exit(act);
    }
    act.startPageTimer();
    return;
  }
  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!selectingStarted_) {
      selectingStarted_ = true;
      anchor_ = focus_;
      selAnchorSpine_ = act.currentSpineIndex;
      selAnchorPage_ = act.section ? act.section->currentPage : 0;
    } else {
      pendingSpans_.push_back(liveSelectionSpan(act));
      selectingStarted_ = false;
      selAnchorSpine_ = -1;
      selAnchorPage_ = -1;
    }
    act.updateRequired = true;
    return;
  }
  if (tryNavigationHoldRepeat(act)) {
    return;
  }
}

void EpubAnnotationUi::saveToStorage(EpubActivity& act) {
  std::vector<HighlightSpan> spans = pendingSpans_;
  if (selectingStarted_) {
    spans.push_back(liveSelectionSpan(act));
  }
  normalizeSpans(spans);
  if (spans.empty()) {
    act.readerPopup("Nothing to save");
    return;
  }

  if (!act.section || !act.epub) {
    act.readerPopup("Could not save");
    exit(act);
    return;
  }

  const std::string cachePath = act.epub->getCachePath();
  const uint32_t ts = static_cast<uint32_t>(time(nullptr));
  bool anyOk = false;

  for (const auto& sp : spans) {
    const HighlightSpan n = normalizedSpan(sp);
    const std::string seg = extractSpanText(act, n);
    if (seg.empty()) {
      continue;
    }
    EpubAnnotationRecord neu{};
    neu.timestamp = ts;
    neu.text = seg;
    neu.startSpine = static_cast<uint16_t>(n.startSpine);
    neu.startPage = static_cast<uint16_t>(n.startPage);
    neu.endSpine = static_cast<uint16_t>(n.endSpine);
    neu.endPage = static_cast<uint16_t>(n.endPage);
    if (n.startSpine == n.endSpine && n.startPage == n.endPage) {
      neu.pageWordLo = static_cast<uint16_t>(n.startWord);
      neu.pageWordHi = static_cast<uint16_t>(n.endWord);
      neu.startPageWordLo = EpubAnnotations::kWildcard;
      neu.startPageWordHi = EpubAnnotations::kWildcard;
    } else {
      neu.startPageWordLo = static_cast<uint16_t>(n.startWord);
      neu.startPageWordHi = EpubAnnotations::kThroughEndOfPage;
      neu.pageWordLo = 0;
      neu.pageWordHi = static_cast<uint16_t>(n.endWord);
    }

    if (annotations_.appendHighlight(cachePath, act.epub->getSpineItemsCount(), neu, act.currentSpineIndex,
                                     act.section->currentPage)) {
      anyOk = true;
    }
  }

  if (!anyOk) {
    act.readerPopup("Could not save");
    exit(act);
    return;
  }

  annotations_.ensurePageLoaded(cachePath, act.currentSpineIndex, act.section->currentPage);
  clearWordIndexCache();

  act.readerPopup(spans.size() > 1 ? "Highlights saved" : "Highlight saved");
  exit(act);
}
