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

/** Greedy word-wrap: splits text into lines no wider than maxWidth at the given font. */
std::vector<std::string> wrapTextToWidth(const GfxRenderer& renderer, const int fontId, const std::string& text,
                                         const int maxWidth) {
  std::vector<std::string> lines;
  std::string current;
  size_t i = 0;
  while (i < text.size()) {
    size_t spacePos = text.find(' ', i);
    const std::string word = (spacePos == std::string::npos) ? text.substr(i) : text.substr(i, spacePos - i);
    const std::string candidate = current.empty() ? word : current + " " + word;
    if (!current.empty() && renderer.text.getWidth(fontId, candidate.c_str()) > maxWidth) {
      lines.push_back(current);
      current = word;
    } else {
      current = candidate;
    }
    i = (spacePos == std::string::npos) ? text.size() : spacePos + 1;
  }
  if (!current.empty()) {
    lines.push_back(current);
  }
  return lines;
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
    return;
  }
  const std::string folder = std::string("/dictionaries/") + SETTINGS.dictionaryFolder;
  dict_.open(folder);
}

void EpubDictionaryUi::performLookup(EpubActivity& act) {
  if (words_.empty() || focus_ >= words_.size()) {
    return;
  }
  ensureDictionaryOpen();
  lookedUpWord_ = stripSurroundingPunctuation(words_[focus_].text);
  currentDefinition_.clear();

  if (lookedUpWord_.empty()) {
    currentDefinition_ = "Nothing to look up.";
  } else if (SETTINGS.dictionaryFolder[0] == '\0') {
    currentDefinition_ = "No dictionary selected. Pick one in Settings > Reader > Choose dictionary.";
  } else if (!dict_.isOpen()) {
    currentDefinition_ = "Could not open the selected dictionary.";
  } else if (!dict_.lookup(lookedUpWord_, currentDefinition_)) {
    currentDefinition_ = "No definition found.";
  }
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
    // Word navigation is frozen while a definition is on screen; only Back/Confirm above apply.
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
  constexpr int margin = 16;
  constexpr int pad = 20;
  const int panelX = margin;
  const int panelW = screenW - margin * 2;
  const int panelTop = screenH * 2 / 5;
  const int panelH = screenH - panelTop - margin - 40;  // leave room for the button-hint row below

  // Same sharp-corner white-fill + black-border panel style as the menu/settings drawers
  // (MenuDrawer/SettingsDrawer background), not a rounded popup box.
  act.renderer.rectangle.fill(panelX, panelTop, panelW, panelH, false);
  act.renderer.rectangle.render(panelX, panelTop, panelW, panelH, true);

  const int titleFontId = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
  const int bodyFontId = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
  int y = panelTop + pad + act.renderer.text.getLineHeight(titleFontId);
  act.renderer.text.render(titleFontId, panelX + pad, y - act.renderer.text.getLineHeight(titleFontId),
                           lookedUpWord_.c_str(), true, EpdFontFamily::BOLD);
  y += 10;

  const int textWidth = panelW - pad * 2;
  const auto lines = wrapTextToWidth(act.renderer, bodyFontId, currentDefinition_, textWidth);
  const int lineH = act.renderer.text.getLineHeight(bodyFontId);
  const int maxLines = std::max(1, (panelTop + panelH - pad - y) / lineH);
  for (size_t i = 0; i < lines.size() && static_cast<int>(i) < maxLines; ++i) {
    const bool lastVisible = static_cast<int>(i) == maxLines - 1 && lines.size() > static_cast<size_t>(maxLines);
    std::string lineText = lines[i];
    if (lastVisible) {
      lineText += " …";
    }
    act.renderer.text.render(bodyFontId, panelX + pad, y, lineText.c_str(), true, EpdFontFamily::REGULAR);
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
  act.renderer.ui.sideButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "", "Up", "Down");
  act.renderer.setOrientation(o);
  act.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
