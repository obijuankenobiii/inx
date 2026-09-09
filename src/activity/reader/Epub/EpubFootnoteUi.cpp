#include "EpubFootnoteUi.h"

#include <Epub/Page.h>
#include <Epub/PageWordIndex.h>
#include <Epub/parsers/FootnoteFragmentExtractor.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstring>
#include <new>

#include "EpubActivity.h"
#include "dictionary/DictionaryDefinitionLayout.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

constexpr unsigned long kNavEdgeDebounceMs = 130;
constexpr unsigned long kNavRepeatInitialMs = 700;
constexpr unsigned long kNavRepeatIntervalMs = 95;
constexpr int kHighlightLatticeStepPx = 2;

// Shared between resolveFootnoteBody() (to lay out bodyLines_ once, at the width it'll actually be
// rendered at) and drawBodyPanel() (to size/draw the panel itself). Same values as
// EpubDictionaryUi's definition panel, so the two overlays read as one visual family.
constexpr int kPanelMargin = 16;
constexpr int kPanelPad = 20;

// Fixed-size (stack) buffers used by EpubFootnoteUi::loadFootnoteBodyText's on-SD search - see that
// method's doc comment. kChunk+kOverlap must match the declared window[] size below.
constexpr size_t kSearchChunkBytes = 512;
constexpr size_t kSearchOverlapBytes = 96;  // longer than any realistic id="..." attribute
// Many real EPUBs put a footnote's id on a short inline backlink tag with the actual note text as
// sibling content in the enclosing block (see FootnoteFragmentExtractor's findEnclosingBlockStart) -
// that means the useful content isn't only after the match, it can start somewhat before it too, so
// the capture window reads some leading context in addition to the trailing content.
constexpr size_t kCaptureLeadingBytes = 2048;
constexpr size_t kCaptureBytes = 4096;  // generous for any single footnote's markup

}  // namespace

EpubFootnoteUi::EpubFootnoteUi() = default;

bool EpubFootnoteUi::isDuplicateNavEdge(const int dir, const unsigned long now) {
  if (lastNavEdgeDir_ == dir && (now - lastNavEdgeMs_) < kNavEdgeDebounceMs) {
    return true;
  }
  lastNavEdgeMs_ = now;
  lastNavEdgeDir_ = dir;
  return false;
}

void EpubFootnoteUi::prepareWordGeometry(EpubActivity& act) {
  words_.clear();
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
    return;
  }
  std::vector<PageWordHit> allWords;
  constexpr bool omitStoredWordStrings = false;
  buildPageWordIndex(*page, act.renderer, fontId, headerFontId, ml, mt, allWords, nullptr, omitStoredWordStrings);
  words_.reserve(allWords.size());
  for (auto& w : allWords) {
    if (!w.footnoteTarget.empty()) {
      words_.push_back(std::move(w));
    }
  }
}

void EpubFootnoteUi::captureFramebuffer(EpubActivity& act) {
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

void EpubFootnoteUi::enter(EpubActivity& act) {
  if (!act.section || !act.epub) {
    return;
  }
  act.btnBindings_.reset();
  mode_ = true;
  showingBody_ = false;
  releaseBodyMemory();
  focus_ = 0;
  lastNavEdgeDir_ = -1;
  navRepeatDir_ = -1;

  prepareWordGeometry(act);
  if (words_.empty()) {
    act.readerPopup("No footnotes on this page");
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

void EpubFootnoteUi::exit(EpubActivity& act) {
  mode_ = false;
  showingBody_ = false;
  releaseBodyMemory();
  std::vector<PageWordHit>().swap(words_);
  lastNavEdgeDir_ = -1;
  navRepeatDir_ = -1;
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

void EpubFootnoteUi::releaseBodyMemory() {
  std::vector<DefinitionBlock>().swap(bodyBlocks_);
  std::vector<DefinitionStyledLine>().swap(bodyLines_);
  bodyScrollLine_ = 0;
  bodyScrollable_ = false;
}

void EpubFootnoteUi::resolveFootnoteBody(EpubActivity& act) {
  Serial.printf("[%lu] [FTN] resolveFootnoteBody: focus=%u words=%u\n", millis(), (unsigned)focus_,
               (unsigned)words_.size());
  if (words_.empty() || focus_ >= words_.size()) {
    return;
  }
  releaseBodyMemory();

  // Encoded by ChapterHtmlSlimParser::classifyFootnoteLink as "<S|F>:<resolvedPath>#<fragmentId>" -
  // resolvedPath is already a fully-resolved internal EPUB path (or empty for same-document).
  const std::string& target = words_[focus_].footnoteTarget;
  Serial.printf("[%lu] [FTN] target=%s\n", millis(), target.c_str());
  std::string body;
  if (target.size() < 3 || target[1] != ':') {
    body = "Invalid footnote reference.";
  } else {
    const std::string encoded = target.substr(2);
    const size_t hashPos = encoded.find('#');
    if (hashPos == std::string::npos) {
      body = "Invalid footnote reference.";
    } else {
      std::string internalPath = encoded.substr(0, hashPos);
      const std::string fragmentId = encoded.substr(hashPos + 1);
      if (internalPath.empty()) {
        internalPath = act.epub->getSpineItem(act.currentSpineIndex).href;
      }
      Serial.printf("[%lu] [FTN] internalPath=%s fragmentId=%s\n", millis(), internalPath.c_str(),
                   fragmentId.c_str());
      const std::string inner = loadFootnoteBodyText(act, internalPath, fragmentId);
      Serial.printf("[%lu] [FTN] loadFootnoteBodyText returned %u bytes\n", millis(), (unsigned)inner.size());
      body = inner.empty() ? "Footnote content not found." : inner;
    }
  }

  Serial.printf("[%lu] [FTN] parseHtmlToBlocks: body=%u bytes\n", millis(), (unsigned)body.size());
  bodyBlocks_ = parseHtmlToBlocks(body);
  Serial.printf("[%lu] [FTN] layoutDefinitionBlocks: %u blocks\n", millis(), (unsigned)bodyBlocks_.size());
  const int textWidth = (act.renderer.getScreenWidth() - kPanelMargin * 2) - kPanelPad * 2;
  bodyLines_ = layoutDefinitionBlocks(act.renderer, bodyBlocks_, textWidth);
  Serial.printf("[%lu] [FTN] resolveFootnoteBody done: %u lines\n", millis(), (unsigned)bodyLines_.size());
  showingBody_ = true;
  act.updateRequired = true;
}

std::string EpubFootnoteUi::loadFootnoteBodyText(EpubActivity& act, const std::string& internalPath,
                                                 const std::string& fragmentId) {
  if (internalPath.empty() || fragmentId.empty() || !act.epub) {
    return "";
  }

  // Same temp-file pattern as Epub::parseTocNcxFile/parseTocNavFile: extract via the zip's own
  // bounded (~1KB read buffer + ~33KB inflator dictionary, regardless of item size) decompression
  // path straight to SD, rather than holding the decompressed item in RAM - a notes/endnotes chapter
  // can be 100KB+ of markup, well beyond what's safe to hold as one in-memory buffer on this device.
  const std::string tempPath = act.epub->getCachePath() + "/.footnote_extract.tmp";
  {
    FsFile tempFile;
    if (!SdMan.openFileForWrite("FTN", tempPath, tempFile)) {
      return "";
    }
    const bool extracted = act.epub->readItemContentsToStream(internalPath, tempFile, 1024);
    tempFile.flush();
    tempFile.sync();
    tempFile.close();
    Serial.printf("[%lu] [FTN] extracted=%d\n", millis(), extracted ? 1 : 0);
    if (!extracted) {
      SdMan.remove(tempPath.c_str());
      return "";
    }
  }

  FsFile f;
  if (!SdMan.openFileForRead("FTN", tempPath, f)) {
    SdMan.remove(tempPath.c_str());
    return "";
  }
  Serial.printf("[%lu] [FTN] temp file size=%llu\n", millis(), static_cast<unsigned long long>(f.size()));

  const std::string doubleQuoted = "id=\"" + fragmentId + "\"";
  const std::string singleQuoted = "id='" + fragmentId + "'";

  // Phase 1: find id="fragmentId" by scanning the temp file in small fixed chunks, kept in a
  // heap-allocated buffer - NOT a stack array. This runs several calls deep in the normal
  // input-handling chain (loopTask), whose stack is not sized to absorb multi-KB local buffers; an
  // earlier version used a stack array here and overflowed loopTask's stack on a real device.
  // kSearchOverlapBytes of the previous chunk is kept so a match spanning a chunk boundary is never
  // missed.
  constexpr size_t kWindowBufSize = kSearchChunkBytes + kSearchOverlapBytes + 1;
  std::unique_ptr<char[]> window(new (std::nothrow) char[kWindowBufSize]);
  if (!window) {
    f.close();
    SdMan.remove(tempPath.c_str());
    return "";
  }
  size_t windowLen = 0;
  long windowFileStart = 0;
  long foundOffset = -1;
  while (true) {
    const int r = f.read(reinterpret_cast<uint8_t*>(window.get() + windowLen), kSearchChunkBytes);
    if (r <= 0) {
      break;
    }
    windowLen += static_cast<size_t>(r);
    const std::string windowStr(window.get(), windowLen);  // bounded copy, <= kSearchChunkBytes+kSearchOverlapBytes
    size_t hit = windowStr.find(doubleQuoted);
    if (hit == std::string::npos) {
      hit = windowStr.find(singleQuoted);
    }
    if (hit != std::string::npos) {
      const size_t lastOpen = windowStr.rfind('<', hit);
      foundOffset = windowFileStart + static_cast<long>(lastOpen != std::string::npos ? lastOpen : hit);
      break;
    }
    const size_t keep = std::min(windowLen, kSearchOverlapBytes);
    if (keep > 0 && keep < windowLen) {
      memmove(window.get(), window.get() + (windowLen - keep), keep);
    }
    windowFileStart += static_cast<long>(windowLen - keep);
    windowLen = keep;
    if (static_cast<size_t>(r) < kSearchChunkBytes) {
      break;  // short read - end of file
    }
  }
  window.reset();  // done with the search buffer before allocating the (larger) capture buffer
  Serial.printf("[%lu] [FTN] search done: foundOffset=%ld\n", millis(), foundOffset);

  std::string result;
  if (foundOffset >= 0) {
    // Seek back a bit before the match too - the id is often on a short inline backlink tag whose
    // enclosing block (where the real footnote text lives) starts a little earlier in the file; see
    // FootnoteFragmentExtractor's findEnclosingBlockStart.
    const long captureStart = std::max(0L, foundOffset - static_cast<long>(kCaptureLeadingBytes));
    constexpr size_t kCaptureBufSize = kCaptureLeadingBytes + kCaptureBytes + 1;
    std::unique_ptr<char[]> captureBuf(new (std::nothrow) char[kCaptureBufSize]);
    Serial.printf("[%lu] [FTN] captureBuf alloc=%d captureStart=%ld\n", millis(), captureBuf ? 1 : 0, captureStart);
    if (captureBuf && f.seek(static_cast<uint32_t>(captureStart))) {
      const int got = f.read(reinterpret_cast<uint8_t*>(captureBuf.get()), kCaptureLeadingBytes + kCaptureBytes);
      Serial.printf("[%lu] [FTN] captured %d bytes, calling extractElementInnerHtmlById\n", millis(), got);
      if (got > 0) {
        result = extractElementInnerHtmlById(std::string(captureBuf.get(), static_cast<size_t>(got)), fragmentId);
      }
      Serial.printf("[%lu] [FTN] extractElementInnerHtmlById returned %u bytes\n", millis(), (unsigned)result.size());
    }
  }

  f.close();
  SdMan.remove(tempPath.c_str());
  Serial.printf("[%lu] [FTN] loadFootnoteBodyText done\n", millis());
  return result;
}

bool EpubFootnoteUi::tryNavigationHoldRepeat(EpubActivity& act) {
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
  const bool leftHeld = m.isPressed(Btn::Left);
  const bool rightHeld = m.isPressed(Btn::Right);
  if (!leftHeld && !rightHeld) {
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
  } else {
    navRepeatDir_ = -1;
    return false;
  }
  navRepeatNextMs_ = now + kNavRepeatIntervalMs;
  act.updateRequired = true;
  return true;
}

void EpubFootnoteUi::moveFocusWord(const int delta) {
  Serial.printf("[%lu] [FTN] moveFocusWord(%d): focus=%u words=%u\n", millis(), delta, (unsigned)focus_,
               (unsigned)words_.size());
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

void EpubFootnoteUi::handleInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;

  if (m.wasReleased(MappedInputManager::Button::Back)) {
    if (showingBody_) {
      showingBody_ = false;
      releaseBodyMemory();
      act.updateRequired = true;
    } else {
      exit(act);
      act.startPageTimer();
    }
    return;
  }
  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!showingBody_) {
      resolveFootnoteBody(act);
      act.updateRequired = true;
    }
    return;
  }
  if (showingBody_) {
    constexpr size_t kScrollLinesPerPress = 3;
    if (m.wasPressed(MappedInputManager::Button::Up)) {
      bodyScrollLine_ = (bodyScrollLine_ > kScrollLinesPerPress) ? bodyScrollLine_ - kScrollLinesPerPress : 0;
      act.updateRequired = true;
    } else if (m.wasPressed(MappedInputManager::Button::Down)) {
      bodyScrollLine_ += kScrollLinesPerPress;
      act.updateRequired = true;
    }
    return;
  }
  if (tryNavigationHoldRepeat(act)) {
    return;
  }
}

void EpubFootnoteUi::repaint(EpubActivity& act) {
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

void EpubFootnoteUi::drawFocusHighlight(EpubActivity& act) {
  if (words_.empty() || focus_ >= words_.size()) {
    return;
  }
  const PageWordHit& w = words_[focus_];
  act.renderer.ui.fillSparseInkLatticeInRect(w.screenX, std::max(0, w.screenY), std::max(1, w.screenW),
                                             std::max(3, w.screenH), kHighlightLatticeStepPx);
}

void EpubFootnoteUi::drawBodyPanel(EpubActivity& act) {
  const int screenW = act.renderer.getScreenWidth();
  const int screenH = act.renderer.getScreenHeight();
  constexpr int margin = kPanelMargin;
  constexpr int pad = kPanelPad;
  const int panelX = margin;
  const int panelW = screenW - margin * 2;
  const int panelBottom = screenH - margin - 40;  // leave room for the button-hint row below
  const int defaultPanelTop = screenH * 2 / 5;
  const int minPanelTop = margin;

  const int titleFontId = MONTSERRAT_12_FONT_ID;
  const int titleH = act.renderer.text.getLineHeight(titleFontId);
  const auto& styledLines = bodyLines_;

  int contentH = 0;
  for (const DefinitionStyledLine& sl : styledLines) {
    contentH += act.renderer.text.getLineHeight(sl.fontId) + sl.extraGapBeforePx;
  }

  constexpr int kTitleGapPx = 8;
  const int neededPanelH = pad * 2 + titleH + kTitleGapPx * 2 + contentH;
  const int defaultPanelH = panelBottom - defaultPanelTop;
  const int maxPanelH = panelBottom - minPanelTop;
  const int panelH = std::min(maxPanelH, std::max(defaultPanelH, neededPanelH));
  const int panelTop = panelBottom - panelH;

  act.renderer.rectangle.fill(panelX, panelTop, panelW, panelH, false);
  act.renderer.rectangle.render(panelX, panelTop, panelW, panelH, true);

  int y = panelTop + pad + titleH;
  act.renderer.text.render(titleFontId, panelX + pad, y - titleH, "Footnote", true, EpdFontFamily::BOLD);
  y += kTitleGapPx;
  act.renderer.line.render(panelX + pad, y, panelX + panelW - pad, y, true, LineRender::Style::Dotted);
  y += kTitleGapPx;

  const int contentBottom = panelTop + panelH - pad;
  const int availableH = contentBottom - y;

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
  bodyScrollable_ = maxScrollLine > 0;
  bodyScrollLine_ = std::min(bodyScrollLine_, static_cast<size_t>(maxScrollLine));

  renderStyledLines(act.renderer, styledLines, panelX + pad, y, contentBottom, bodyScrollLine_);
}

void EpubFootnoteUi::drawUiOverlay(EpubActivity& act) {
  if (!mode_) {
    return;
  }
  const GfxRenderer::Orientation o = act.renderer.getOrientation();
  if (showingBody_) {
    drawBodyPanel(act);
  } else {
    drawFocusHighlight(act);
  }
  act.renderer.setOrientation(GfxRenderer::Portrait);
  const char* back = showingBody_ ? "Close" : "Exit";
  const char* mid = showingBody_ ? "" : "View";
  const auto labels = act.mappedInput.mapLabels(back, mid, "Prev", "Next");
  act.renderer.ui.buttonHints(MONTSERRAT_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  const bool showUpDown = !showingBody_ || bodyScrollable_;
  act.renderer.ui.sideButtonHints(MONTSERRAT_10_FONT_ID, "", showUpDown ? "Up" : "", showUpDown ? "Down" : "");
  act.renderer.setOrientation(o);
  act.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
