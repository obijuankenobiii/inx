#include "EpubDictionaryUi.h"

#include <EpdFontFamily.h>
#include <Epub/Page.h>
#include <Epub/PageWordIndex.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "EpubActivity.h"
#include "WordOverlayNav.h"
#include "dictionary/DictionaryDefinitionLayout.h"
#include "dictionary/DictionaryRegistry.h"
#include "state/SavedDictionaryWords.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

constexpr unsigned long kChordHoldMs = 600;
constexpr int kHighlightLatticeStepPx = 2;

// Shared between performLookup() (to lay out definitionLines_ once, at the width it'll actually be
// rendered at) and drawDefinitionPanel() (to size/draw the panel itself).
constexpr int kDefinitionPanelMargin = 16;
constexpr int kDefinitionPanelPad = 20;

bool sameWordIgnoreCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

std::string stripHtmlToPlain(const std::string& html) {
  std::string out;
  out.reserve(html.size());
  bool inTag = false;
  for (unsigned char c : html) {
    if (c == '<') {
      inTag = true;
      continue;
    }
    if (c == '>') {
      inTag = false;
      if (!out.empty() && out.back() != ' ') {
        out.push_back(' ');
      }
      continue;
    }
    if (!inTag) {
      out.push_back(static_cast<char>(c));
    }
  }
  return out;
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
    // Start opening the dictionary while the chord is still held so Confirm is a RAM/SD seek,
    // not a multi-second .idx scan. Warm open() is a no-op.
    if (millis() - chordStartMs_ >= 80 && ESP.getFreeHeap() > 80000) {
      ensureDictionaryOpen(act);
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
  // The Down+Left entry chord (and a plain long-press Down) leave the button held while
  // handleInput() is about to stop running for the whole overlay session - reset its per-button
  // state now so it doesn't misfire a stale long-press the instant this overlay exits.
  act.btnBindings_.reset();
  mode_ = true;
  showingDefinition_ = false;
  lookedUpWord_.clear();
  releaseDefinitionMemory();
  focus_ = 0;
  lastNavEdgeDir_ = -1;

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
  releaseDefinitionMemory();
  std::vector<PageWordHit>().swap(words_);
  std::vector<size_t>().swap(lineFirst_);
  // Keep dict_ open for the rest of the book session: reopening rebuilds the checkpoint index with a
  // full .idx scan, which is the cold-path cost we just paid on the first lookup. RAM held is tens of
  // KB of checkpoints; reclaimed when the reader activity is destroyed (dict_ is a member).
  lastNavEdgeDir_ = -1;
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

/** See header - swaps with a default-constructed temporary rather than .clear(), so the heap
 *  capacity a big definition needed is actually returned instead of sitting reserved for reuse. */
void EpubDictionaryUi::releaseDefinitionMemory() {
  std::string().swap(currentDefinition_);
  std::string().swap(matchedHeadword_);
  std::vector<DefinitionBlock>().swap(definitionBlocks_);
  std::vector<DefinitionStyledLine>().swap(definitionLines_);
  definitionScrollLine_ = 0;
  definitionScrollable_ = false;
}

void EpubDictionaryUi::openFolder(const std::string& folderName) {
  if (folderName.empty()) {
    return;
  }
  const std::string folder = std::string(DictionaryRegistry::kDictionariesRoot) + "/" + folderName;
  if (dict_.isOpen() && dict_.folderPath() == folder) {
    return;
  }
  (void)dict_.open(folder);
}

void EpubDictionaryUi::ensureDictionaryOpen(EpubActivity& act) {
  preferredFolder_ = resolvePreferredFolder(act);
  if (preferredFolder_.empty()) {
    Serial.printf("[%lu] [DICT] ensureDictionaryOpen: no dictionary folders found\n", millis());
    return;
  }
  openFolder(preferredFolder_);
  Serial.printf("[%lu] [DICT] ensureDictionaryOpen: preferred='%s' open=%d session='%s'\n", millis(),
                preferredFolder_.c_str(), dict_.isOpen() ? 1 : 0, sessionFolder_.c_str());
}

std::string EpubDictionaryUi::resolvePreferredFolder(EpubActivity& act) {
  std::string detected;
  if (!words_.empty() && focus_ < words_.size()) {
    const size_t start = focus_ > 6 ? focus_ - 6 : 0;
    const size_t end = std::min(words_.size(), focus_ + 7);
    std::vector<std::string> window;
    window.reserve(end - start);
    for (size_t i = start; i < end; ++i) {
      window.push_back(StarDictLookup::stripSurroundingPunctuation(words_[i].text));
    }
    detected = DictionaryRegistry::detectLanguage(window, focus_ - start);
  }
  std::string bookLang;
  if (act.epub) {
    bookLang = act.epub->getLanguage();
  }
  return DictionaryRegistry::folderForLookup(detected, sessionFolder_, bookLang, READER_SETTINGS.dictionaryFolder);
}

void EpubDictionaryUi::setLangLabelFromFolder(const std::string& folderName) {
  activeLangLabel_ = DictionaryRegistry::displayLabel(folderName, dict_.lang());
  if (activeLangLabel_.empty() || activeLangLabel_ == "Auto") {
    activeLangLabel_ = DictionaryRegistry::displayLabel(folderName, DictionaryRegistry::inferLangFromName(folderName));
  }
}

void EpubDictionaryUi::layoutCurrentDefinition(EpubActivity& act, const bool truncated) {
  (void)truncated;

  std::vector<DefinitionBlock>().swap(definitionBlocks_);
  const int textWidth = (act.renderer.getScreenWidth() - kDefinitionPanelMargin * 2) - kDefinitionPanelPad * 2;
  if (ESP.getMaxAllocHeap() > 16000) {
    std::string folder = sessionFolder_.empty() ? preferredFolder_ : sessionFolder_;
    if (folder.empty() && dict_.isOpen()) {
      const std::string& path = dict_.folderPath();
      const size_t rootLen = std::strlen(DictionaryRegistry::kDictionariesRoot);
      if (path.size() > rootLen + 1) {
        folder = path.substr(rootLen + 1);
      }
    }
    std::string target = DictionaryRegistry::targetLangFromFolder(folder);
    if (target.empty()) {
      target = DictionaryRegistry::inferLangFromName(folder);
    }
    if (target.empty()) {
      target = "nl";
    }
    definitionLines_ =
        layoutDictionaryCard(act.renderer, currentDefinition_, textWidth, target.c_str(), lookedUpWord_.c_str());
  } else {
    definitionLines_.clear();
  }
  if (definitionLines_.empty()) {
    std::string plain = stripHtmlToPlain(currentDefinition_);
    if (plain.empty()) {
      plain = currentDefinition_;
    }
    if (!plain.empty()) {
      const std::string clipped =
          act.renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, plain.c_str(), std::max(1, textWidth));
      DefinitionStyledLine line;
      line.fontId = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
      line.atoms.push_back(DefinitionTextAtom(clipped.empty() ? plain : clipped, EpdFontFamily::REGULAR, false, false));
      definitionLines_.push_back(std::move(line));
    }
  }
  definitionScrollLine_ = 0;
}

bool EpubDictionaryUi::lookupInFolder(EpubActivity& act, const std::string& folderName, const std::string& queryWord,
                                      std::string& outDefinition, bool* outTruncated, const bool allowStem) {
  if (folderName.empty() || queryWord.empty()) {
    return false;
  }
  const std::string folder = std::string(DictionaryRegistry::kDictionariesRoot) + "/" + folderName;
  const bool alreadyWarm = dict_.isOpen() && dict_.folderPath() == folder;
  if (!alreadyWarm) {
    act.readerPopup(StarDictLookup::needsIndexBuild(folder) ? "Indexing dictionary..." : "Opening dictionary...");
  }
  openFolder(folderName);
  if (!dict_.isOpen()) {
    return false;
  }
  return allowStem ? dict_.lookup(queryWord, outDefinition, outTruncated)
                   : dict_.lookupExact(queryWord, outDefinition, outTruncated);
}

bool EpubDictionaryUi::tryUsefulLookup(EpubActivity& act, const std::string& folderName, const std::string& queryWord,
                                       bool* outTruncated) {
  auto accept = [&](std::string& def, const bool trunc) {
    if (!definitionHasUsefulGloss(def)) {
      return false;
    }
    currentDefinition_ = std::move(def);
    matchedHeadword_ = dict_.lastHitHeadword();
    if (outTruncated) {
      *outTruncated = trunc;
    }
    return true;
  };

  auto tryOnce = [&](const bool allowStem) {
    std::string def;
    bool trunc = false;
    if (!lookupInFolder(act, folderName, queryWord, def, &trunc, allowStem)) {
      return false;
    }
    if (accept(def, trunc)) {
      return true;
    }
    const std::string lemma = lemmaFromDefinition(def);
    if (lemma.empty() || lemma == queryWord) {
      return false;
    }
    std::string lemmaDef;
    bool lemmaTrunc = false;
    if (!lookupInFolder(act, folderName, lemma, lemmaDef, &lemmaTrunc, false)) {
      return false;
    }
    return accept(lemmaDef, lemmaTrunc);
  };

  return tryOnce(true);
}

void EpubDictionaryUi::cycleDictionary(EpubActivity& act, const int delta) {
  const auto folders = DictionaryRegistry::foldersInFallbackOrder(preferredFolder_);
  if (folders.size() < 2) {
    return;
  }
  std::string current;
  if (dict_.isOpen() && dict_.folderPath().size() > std::strlen(DictionaryRegistry::kDictionariesRoot) + 1) {
    current = dict_.folderPath().substr(std::strlen(DictionaryRegistry::kDictionariesRoot) + 1);
  } else {
    current = preferredFolder_;
  }
  int idx = 0;
  for (size_t i = 0; i < folders.size(); ++i) {
    if (folders[i] == current) {
      idx = static_cast<int>(i);
      break;
    }
  }
  idx = (idx + delta + static_cast<int>(folders.size())) % static_cast<int>(folders.size());
  bool truncated = false;
  currentDefinition_.clear();
  matchedHeadword_.clear();
  const std::string& chosen = folders[static_cast<size_t>(idx)];
  if (!tryUsefulLookup(act, chosen, lookedUpWord_, &truncated)) {
    currentDefinition_ = "No definition found.";
    matchedHeadword_.clear();
  }
  sessionFolder_ = chosen;
  preferredFolder_ = chosen;
  usedFallbackDict_ = false;
  setLangLabelFromFolder(chosen);
  layoutCurrentDefinition(act, truncated);
  act.updateRequired = true;
}

void EpubDictionaryUi::performLookup(EpubActivity& act) {
  if (words_.empty() || focus_ >= words_.size()) {
    return;
  }
  lookedUpWord_ = StarDictLookup::stripSurroundingPunctuation(words_[focus_].text);
  currentDefinition_.clear();
  matchedHeadword_.clear();
  definitionScrollLine_ = 0;
  wordAlreadySaved_ = !lookedUpWord_.empty() && SAVED_WORDS.contains(lookedUpWord_);
  esp_task_wdt_reset();

  bool truncated = false;
  usedFallbackDict_ = false;
  activeLangLabel_.clear();
  if (lookedUpWord_.empty()) {
    currentDefinition_ = "Nothing to look up.";
  } else if (ESP.getFreeHeap() < 28000) {
    currentDefinition_ = "Not enough memory for dictionary lookup.";
  } else {
    preferredFolder_ = resolvePreferredFolder(act);
    esp_task_wdt_reset();
    std::vector<std::string> folders = DictionaryRegistry::foldersForAutoLookup(preferredFolder_);
    if (folders.empty()) {
      currentDefinition_ = "No dictionary selected. Add StarDict folders under /dictionaries/.";
    } else {
      bool found = false;
      auto tryFolders = [&](const std::vector<std::string>& list, const bool otherLang) {
        for (size_t i = 0; i < list.size(); ++i) {
          truncated = false;
          currentDefinition_.clear();
          if (!tryUsefulLookup(act, list[i], lookedUpWord_, &truncated)) {
            esp_task_wdt_reset();
            continue;
          }
          found = true;
          usedFallbackDict_ = otherLang || i > 0;
          if (!otherLang && i == 0) {
            sessionFolder_ = list[i];
          }
          setLangLabelFromFolder(list[i]);
          return;
        }
      };
      tryFolders(folders, false);
      if (!found) {
        const std::vector<std::string> all = DictionaryRegistry::foldersInFallbackOrder(preferredFolder_);
        std::vector<std::string> rest;
        rest.reserve(all.size());
        for (const std::string& folder : all) {
          if (std::find(folders.begin(), folders.end(), folder) == folders.end()) {
            rest.push_back(folder);
          }
        }
        tryFolders(rest, true);
      }
      if (!found) {
        currentDefinition_ = "No definition found.";
        if (dict_.isOpen()) {
          setLangLabelFromFolder(preferredFolder_);
        }
      }
    }
  }
  layoutCurrentDefinition(act, truncated);
  showingDefinition_ = true;
  act.updateRequired = true;
}

bool EpubDictionaryUi::tryNavigationHoldRepeat(EpubActivity& act) {
  WordOverlayNav::EdgeState edge{lastNavEdgeMs_, lastNavEdgeDir_};
  const int nav = WordOverlayNav::handleDpad(
      act.mappedInput, edge, navRepeatDir_, navRepeatNextMs_, millis(),
      [this](const int delta) { moveFocusWord(delta); },
      [this](const int delta, const bool wrap) { moveFocusLine(delta, wrap); });
  lastNavEdgeMs_ = edge.lastMs;
  lastNavEdgeDir_ = edge.lastDir;
  if (nav == 2) {
    act.updateRequired = true;
  }
  return nav != 0;
}

/** Saves lookedUpWord_ plus the definition currently on screen. Idempotent — a repeat Confirm on an
 *  already-saved word is a no-op. */
void EpubDictionaryUi::saveCurrentWord(EpubActivity& act) {
  if (lookedUpWord_.empty() || wordAlreadySaved_) {
    return;
  }
  std::string lang = DictionaryRegistry::primaryLanguageTag(dict_.lang());
  if (lang.empty()) {
    lang = DictionaryRegistry::inferLangFromName(sessionFolder_.empty() ? preferredFolder_ : sessionFolder_);
  }
  if (SAVED_WORDS.add(lookedUpWord_, currentDefinition_, lang)) {
    wordAlreadySaved_ = true;
    act.updateRequired = true;
  }
}

void EpubDictionaryUi::moveFocusWord(const int delta) {
  WordOverlayNav::moveFocusWord(words_, focus_, delta);
}

void EpubDictionaryUi::moveFocusLine(const int delta, const bool wrap) {
  WordOverlayNav::moveFocusLine(words_, lineFirst_, focus_, delta, wrap);
}

void EpubDictionaryUi::handleInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;

  if (m.wasReleased(MappedInputManager::Button::Back)) {
    if (showingDefinition_) {
      showingDefinition_ = false;
      releaseDefinitionMemory();
      act.updateRequired = true;
    } else {
      exit(act);
      act.startPageTimer();
    }
    return;
  }
  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    if (showingDefinition_) {
      saveCurrentWord(act);
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
    } else if (m.wasReleased(MappedInputManager::Button::Left)) {
      cycleDictionary(act, -1);
    } else if (m.wasReleased(MappedInputManager::Button::Right)) {
      cycleDictionary(act, 1);
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
  const int minPanelTop = margin;

  const int titleFontId = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
  const int titleH = act.renderer.text.getLineHeight(titleFontId);
  const auto& styledLines = definitionLines_;

  int contentH = 0;
  for (const DefinitionStyledLine& sl : styledLines) {
    contentH += act.renderer.text.getLineHeight(sl.fontId) + sl.extraGapBeforePx;
  }

  constexpr int kTitleGapPx = 8;
  const int neededPanelH = pad * 2 + titleH + kTitleGapPx * 2 + contentH;
  const int minPanelH =
      pad * 2 + titleH + kTitleGapPx * 2 + act.renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) * 2;
  const int maxPanelH = panelBottom - minPanelTop;
  const int panelH = std::min(maxPanelH, std::max(minPanelH, neededPanelH));
  const int panelTop = panelBottom - panelH;

  // Same sharp-corner white-fill + black-border panel style as the menu/settings drawers
  // (MenuDrawer/SettingsDrawer background), not a rounded popup box.
  act.renderer.rectangle.fill(panelX, panelTop, panelW, panelH, false);
  act.renderer.rectangle.render(panelX, panelTop, panelW, panelH, true);

  int y = panelTop + pad + titleH;
  {
    const int tagFontId = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
    std::string tag;
    if (wordAlreadySaved_) {
      tag = "\xE2\x98\x85 Saved";
    }
    if (!activeLangLabel_.empty() && activeLangLabel_ != "Auto") {
      if (!tag.empty()) {
        tag += "  ";
      }
      tag += activeLangLabel_;
      if (usedFallbackDict_) {
        tag += "?";
      }
    }
    const int tagW = tag.empty() ? 0 : act.renderer.text.getWidth(tagFontId, tag.c_str());
    const int tagGap = tag.empty() ? 0 : 10;
    if (!tag.empty()) {
      const int tagY = y - titleH + (titleH - act.renderer.text.getLineHeight(tagFontId)) / 2;
      act.renderer.text.render(tagFontId, panelX + panelW - pad - tagW, tagY, tag.c_str(), true);
    }

    const int titleMaxW = std::max(1, panelW - pad * 2 - tagW - tagGap);
    int x = panelX + pad;
    const char* title = lookedUpWord_.c_str();
    act.renderer.text.render(titleFontId, x, y - titleH, title, true, EpdFontFamily::BOLD);
    x += act.renderer.text.getWidth(titleFontId, title, EpdFontFamily::BOLD);

    if (!matchedHeadword_.empty() && !sameWordIgnoreCase(lookedUpWord_, matchedHeadword_)) {
      const int lemmaFontId = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
      const int lemmaY = y - titleH + (titleH - act.renderer.text.getLineHeight(lemmaFontId)) / 2;
      const char* sep = "  <  ";
      const int sepW = act.renderer.text.getWidth(lemmaFontId, sep);
      const int remaining = titleMaxW - (x - (panelX + pad));
      if (remaining > sepW + 12) {
        act.renderer.text.render(lemmaFontId, x, lemmaY, sep, true, EpdFontFamily::REGULAR);
        x += sepW;
        const std::string lemma =
            act.renderer.text.truncate(lemmaFontId, matchedHeadword_.c_str(), std::max(1, remaining - sepW));
        act.renderer.text.render(lemmaFontId, x, lemmaY, lemma.c_str(), true, EpdFontFamily::REGULAR);
      }
    }
  }
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

  renderStyledLines(act.renderer, styledLines, panelX + pad, y, contentBottom, definitionScrollLine_);
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
  const char* back = showingDefinition_ ? "Close" : "Exit";
  const char* mid = showingDefinition_ ? (wordAlreadySaved_ ? "Saved" : "Save") : "Look up";
  const char* leftHint = showingDefinition_ ? "Lang" : "Prev";
  const char* rightHint = showingDefinition_ ? "Lang" : "Next";
  const auto labels = act.mappedInput.mapLabels(back, mid, leftHint, rightHint);
  act.renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  const bool showUpDown = !showingDefinition_ || definitionScrollable_;
  act.renderer.ui.sideButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "", showUpDown ? "Up" : "", showUpDown ? "Down" : "");
  act.renderer.setOrientation(o);
  act.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
