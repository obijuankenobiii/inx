#include "EpubAnnotationUi.h"

#include "EpubActivity.h"

#include <Epub/Page.h>
#include <Epub/PageWordIndex.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

static_assert(8000U * 6U == HalDisplay::BUFFER_SIZE, "Capture chunk layout must match display buffer");
#include <HalGPIO.h>
#include <ctime>

#include <climits>
#include <cstring>
#include <new>
#include <algorithm>

#include "state/ImageBitmapGrayMaps.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

constexpr unsigned long kChordHoldMs = 600;
constexpr int kHighlightLatticeStepPx = 2;
/** ADC/button bounce can deliver two wasPressed edges ~ms apart; loop has no delay — suppress 2nd edge. */
constexpr unsigned long kNavEdgeDebounceMs = 130;
/** After initial edge, repeat nav while held (long-press highlight extension). */
constexpr unsigned long kNavRepeatInitialMs = 420;
constexpr unsigned long kNavRepeatIntervalMs = 95;

struct ReaderBitmapStyleGuard {
  BitmapGrayStyleScope scope;
  explicit ReaderBitmapStyleGuard(GfxRenderer& ren) : scope(ren, readerImageBitmapGrayStyle()) {}
};

}  // namespace

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
  pendingSpans_.clear();
  for (auto& ch : captureChunks_) {
    ch.reset();
  }
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
  if (!selectingStarted_ || words_.empty()) {
    return false;
  }
  const size_t lo = std::min(anchor_, focus_);
  const size_t hi = std::max(anchor_, focus_);
  return lo <= hi;
}

void EpubAnnotationUi::resetSelectionToStart(EpubActivity& act) {
  pendingSpans_.clear();
  selectingStarted_ = false;
  focus_ = 0;
  anchor_ = 0;
  act.updateRequired = true;
  act.startPageTimer();
}

void EpubAnnotationUi::clearAllStoredHighlightsOnCurrentPage(EpubActivity& act) {
  if (!act.epub || !act.section) {
    return;
  }
  annotations_.clearPageShard(act.epub->getCachePath(), act.currentSpineIndex, act.section->currentPage);
  storedRanges_.clear();
  pendingSpans_.clear();
  selectingStarted_ = false;
  focus_ = 0;
  anchor_ = 0;
  // Force full word-index rebuild so merge/geometry cannot reuse state tied to the deleted highlights.
  clearWordIndexCache();
  // Full redraw clears lattice from the framebuffer; then re-capture for annotation repaint path.
  act.renderScreen(true);
  captureFramebuffer(act);
  act.updateRequired = true;
  act.startPageTimer();
}

void EpubAnnotationUi::normalizeSpans(std::vector<std::pair<size_t, size_t>>& spans) {
  if (spans.empty()) {
    return;
  }
  std::sort(spans.begin(), spans.end());
  std::vector<std::pair<size_t, size_t>> out;
  auto cur = spans[0];
  for (size_t i = 1; i < spans.size(); ++i) {
    if (spans[i].first <= cur.second + 1) {
      cur.second = std::max(cur.second, spans[i].second);
    } else {
      out.push_back(cur);
      cur = spans[i];
    }
  }
  out.push_back(cur);
  spans.swap(out);
}

void EpubAnnotationUi::enter(EpubActivity& act) {
  if (!act.section || !act.epub) {
    return;
  }
  mode_ = true;
  selectingStarted_ = false;
  pendingSpans_.clear();
  annLastNavEdgeDir_ = -1;
  annNavRepeatDir_ = -1;
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
  words_.shrink_to_fit();
  lineFirst_.shrink_to_fit();
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
  pendingSpans_.clear();
  storedRanges_.clear();
  words_.clear();
  words_.shrink_to_fit();
  lineFirst_.clear();
  lineFirst_.shrink_to_fit();
  clearWordIndexCache();
  annLastNavEdgeDir_ = -1;
  annNavRepeatDir_ = -1;
  for (auto& ch : captureChunks_) {
    ch.reset();
  }
  captureMonolithic_.reset();
  captureUsesMonolithic_ = false;
  captureBytes_ = 0;
  captureValid_ = false;
  act.updateRequired = true;
}

bool EpubAnnotationUi::tryNavigationHoldRepeat(EpubActivity& act) {
  using Btn = MappedInputManager::Button;
  MappedInputManager& m = act.mappedInput;
  const unsigned long now = millis();

  // Initial edge: one step only (debounced). Schedules timed repeats while held — do not use raw isPressed on the same
  // path as wasPressed in one check, or the first frame double-counts.
  if (m.wasPressed(Btn::Left)) {
    if (isDuplicateNavEdge(0, now)) {
      return true;
    }
    moveFocusWord(-1);
    annNavRepeatDir_ = 0;
    annNavRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    act.startPageTimer();
    return true;
  }
  if (m.wasPressed(Btn::Right)) {
    if (isDuplicateNavEdge(1, now)) {
      return true;
    }
    moveFocusWord(1);
    annNavRepeatDir_ = 1;
    annNavRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    act.startPageTimer();
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
    act.startPageTimer();
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
    act.startPageTimer();
    return true;
  }

  // Long-hold: repeat after initial delay, then at fixed interval (held button only; avoids first-frame double step).
  if (annNavRepeatDir_ == 0 && m.isPressed(Btn::Left) && now >= annNavRepeatNextMs_) {
    moveFocusWord(-1);
    annNavRepeatNextMs_ = now + kNavRepeatIntervalMs;
    act.updateRequired = true;
    act.startPageTimer();
    return true;
  }
  if (annNavRepeatDir_ == 1 && m.isPressed(Btn::Right) && now >= annNavRepeatNextMs_) {
    moveFocusWord(1);
    annNavRepeatNextMs_ = now + kNavRepeatIntervalMs;
    act.updateRequired = true;
    act.startPageTimer();
    return true;
  }
  if (annNavRepeatDir_ == 2 && m.isPressed(Btn::Up) && now >= annNavRepeatNextMs_) {
    moveFocusLine(-1);
    annNavRepeatNextMs_ = now + kNavRepeatIntervalMs;
    act.updateRequired = true;
    act.startPageTimer();
    return true;
  }
  if (annNavRepeatDir_ == 3 && m.isPressed(Btn::Down) && now >= annNavRepeatNextMs_) {
    moveFocusLine(1);
    annNavRepeatNextMs_ = now + kNavRepeatIntervalMs;
    act.updateRequired = true;
    act.startPageTimer();
    return true;
  }

  if (!m.isPressed(Btn::Left) && !m.isPressed(Btn::Right) && !m.isPressed(Btn::Up) && !m.isPressed(Btn::Down)) {
    annNavRepeatDir_ = -1;
  }
  return false;
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

void EpubAnnotationUi::updateStoredRangesForPage(EpubActivity& act) {
  if (!act.section) {
    storedRanges_.clear();
    return;
  }
  EpubAnnotations::mergeStoredRangesForPage(annotations_.records(), act.currentSpineIndex, act.section->currentPage,
                                            words_, storedRanges_);
}

void EpubAnnotationUi::clampSelectionToValidWords() {
  if (words_.empty()) {
    pendingSpans_.clear();
    return;
  }
  const size_t last = words_.size() - 1;
  focus_ = std::min(focus_, last);
  if (selectingStarted_) {
    anchor_ = std::min(anchor_, last);
  }
  for (auto& pr : pendingSpans_) {
    pr.first = std::min(pr.first, last);
    pr.second = std::min(pr.second, last);
    if (pr.first > pr.second) {
      std::swap(pr.first, pr.second);
    }
  }
  pendingSpans_.erase(std::remove_if(pendingSpans_.begin(), pendingSpans_.end(),
                                       [](const std::pair<size_t, size_t>& p) { return p.first > p.second; }),
                        pendingSpans_.end());
}

void EpubAnnotationUi::prepareWordGeometry(EpubActivity& act) {
  if (!act.section || !act.epub) {
    return;
  }
  [[maybe_unused]] ReaderBitmapStyleGuard bitmapStyleGuard(act.renderer);
  ensureDiskListLoaded(act);
  const ViewportInfo info = act.calculateViewport();
  const int fontId = act.bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);
  const int mt = info.totalMarginTop;
  const int ml = info.totalMarginLeft;

  const bool wordIndexCacheHit =
      wordIndexCacheSpine_ == act.currentSpineIndex && wordIndexCachePage_ == act.section->currentPage &&
      wordIndexCacheFontId_ == fontId && wordIndexCacheHeaderFontId_ == headerFontId &&
      wordIndexCacheMarginL_ == ml && wordIndexCacheMarginT_ == mt;

  bool anyWordText = false;
  if (!words_.empty()) {
    for (const auto& w : words_) {
      if (!w.text.empty()) {
        anyWordText = true;
        break;
      }
    }
  }

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
  if (!fb || n == 0 || n != kCaptureChunkBytes * kCaptureChunkCount) {
    return;
  }

  bool chunkedOk = true;
  for (size_t i = 0; i < kCaptureChunkCount; ++i) {
    uint8_t* const buf = new (std::nothrow) uint8_t[kCaptureChunkBytes];
    if (!buf) {
      chunkedOk = false;
      for (size_t j = 0; j < i; ++j) {
        captureChunks_[j].reset();
      }
      break;
    }
    memcpy(buf, fb + i * kCaptureChunkBytes, kCaptureChunkBytes);
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
    for (size_t i = 0; i < kCaptureChunkCount; ++i) {
      if (!captureChunks_[i]) {
        act.renderScreen(true);
        return;
      }
      memcpy(fb + i * kCaptureChunkBytes, captureChunks_[i].get(), kCaptureChunkBytes);
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

void EpubAnnotationUi::drawHighlights(EpubActivity& act) {
  if (!mode_ || words_.empty()) {
    return;
  }
  for (const auto& pr : pendingSpans_) {
    if (pr.first < words_.size() && pr.second < words_.size() && pr.first <= pr.second) {
      drawLatticeHighlightForWordIndexRange(act, pr.first, pr.second);
    }
  }
  if (selectingStarted_) {
    const size_t lo = std::min(anchor_, focus_);
    const size_t hi = std::max(anchor_, focus_);
    drawLatticeHighlightForWordIndexRange(act, lo, hi);
    return;
  }
  if (focus_ < words_.size()) {
    drawLatticeHighlightForWordIndexRange(act, focus_, focus_);
  }
}

void EpubAnnotationUi::drawUiOverlay(EpubActivity& act) {
  if (!mode_) {
    return;
  }
  const GfxRenderer::Orientation o = act.renderer.getOrientation();
  drawHighlights(act);
  act.renderer.setOrientation(GfxRenderer::Portrait);
  const char* backHint = hasSaveableContent() ? "Save" : "Exit";
  const char* mid = selectingStarted_ ? "Stop" : "Start";
  const auto labels = act.mappedInput.mapLabels(backHint, mid, "Prev", "Next");
  act.renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  act.renderer.ui.sideButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "Reset", "Up", "Down");
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
    const size_t start = lineFirst_[lineIdx];
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

void EpubAnnotationUi::handleInput(EpubActivity& act) {
  MappedInputManager& m = act.mappedInput;

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
    } else {
      const size_t lo = std::min(anchor_, focus_);
      const size_t hi = std::max(anchor_, focus_);
      if (!words_.empty() && lo <= hi) {
        pendingSpans_.push_back({lo, std::min(hi, words_.size() - 1)});
      }
      selectingStarted_ = false;
    }
    act.updateRequired = true;
    act.startPageTimer();
    return;
  }
  if (tryNavigationHoldRepeat(act)) {
    return;
  }
}

void EpubAnnotationUi::saveToStorage(EpubActivity& act) {
  std::vector<std::pair<size_t, size_t>> spans = pendingSpans_;
  if (selectingStarted_ && !words_.empty()) {
    const size_t lo = std::min(anchor_, focus_);
    const size_t hi = std::max(anchor_, focus_);
    if (lo <= hi) {
      spans.push_back({lo, std::min(hi, words_.size() - 1)});
    }
  }
  normalizeSpans(spans);
  if (spans.empty()) {
    act.readerPopup("Nothing to save");
    return;
  }

  if (!act.section) {
    act.readerPopup("Could not save");
    exit(act);
    return;
  }

  const std::string cachePath = act.epub->getCachePath();
  const uint32_t ts = static_cast<uint32_t>(time(nullptr));
  bool anyOk = false;

  for (const auto& sp : spans) {
    const std::string seg = extractRangeText(sp.first, sp.second);
    if (seg.empty()) {
      continue;
    }
    EpubAnnotationRecord neu{};
    neu.timestamp = ts;
    neu.text = seg;
    {
      const uint16_t s = static_cast<uint16_t>(act.currentSpineIndex);
      const uint16_t p = static_cast<uint16_t>(act.section->currentPage);
      neu.startSpine = s;
      neu.startPage = p;
      neu.endSpine = s;
      neu.endPage = p;
    }
    neu.pageWordLo = static_cast<uint16_t>(sp.first);
    neu.pageWordHi = static_cast<uint16_t>(sp.second);
    neu.startPageWordLo = EpubAnnotations::kWildcard;
    neu.startPageWordHi = EpubAnnotations::kWildcard;

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
