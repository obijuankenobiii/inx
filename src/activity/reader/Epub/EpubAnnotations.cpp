/**
 * @file EpubAnnotations.cpp
 */

#include "EpubAnnotations.h"

#include <Arduino.h>
#include <Epub/Section.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <sstream>

#include "state/EpubNotesIndex.h"

namespace {

constexpr uint32_t kAnnMagicV3 = 0x334E4E41;  // "ANN3"

std::vector<std::string> splitAnnotationWords(const std::string& text) {
  std::vector<std::string> words;
  std::istringstream iss(text);
  std::string w;
  while (iss >> w) {
    words.push_back(std::move(w));
  }
  return words;
}

/** Checks whether `target` starts at pageWords[pos], joining subsequent hyphen-broken tokens if needed (a
 *  line wrap at a hyphenation point splits one word into multiple tokens, e.g. "Labora-" + "tory" for
 *  "Laboratory" - a font/margin change can introduce or remove such a split for a word that used to be
 *  captured whole). Returns how many tokens the match consumed (>=1), or 0 if `target` isn't found here. */
size_t matchWordAllowingHyphenation(const std::vector<PageWordHit>& pageWords, const size_t pos,
                                    const std::string& target) {
  if (pos >= pageWords.size()) {
    return 0;
  }
  std::string joined = pageWords[pos].text;
  size_t consumed = 1;
  if (joined == target) {
    return consumed;
  }
  while (!joined.empty() && joined.back() == '-' && pos + consumed < pageWords.size() &&
        joined.size() <= target.size()) {
    joined.pop_back();
    joined += pageWords[pos + consumed].text;
    ++consumed;
    if (joined == target) {
      return consumed;
    }
  }
  return 0;
}

/** True if a contiguous run starting at pageWords[startPos] spells out `phrase` word for word (allowing
 *  hyphen-rejoining per matchWordAllowingHyphenation). Returns the number of pageWords tokens the whole
 *  phrase consumed (>=phrase.size()), or 0 if it doesn't match here. */
size_t matchPhraseAt(const std::vector<PageWordHit>& pageWords, const size_t startPos,
                     const std::vector<std::string>& phrase) {
  size_t pos = startPos;
  for (const std::string& word : phrase) {
    const size_t consumed = matchWordAllowingHyphenation(pageWords, pos, word);
    if (consumed == 0) {
      return 0;
    }
    pos += consumed;
  }
  return pos - startPos;
}

/** True if annWords[wordLo..wordHi] is exactly the stored highlight text, word for word (mirroring the
 *  original capture - not necessarily one raw token per word, if hyphenation now splits one of them). A
 *  font/layout change repaginates the book, so a stored word-index range can end up pointing at completely
 *  different words on the new layout - this catches that instead of silently highlighting the wrong phrase.
 *  Older records with no stored text can't be verified this way, so they're left trusted as before. */
bool wordRangeMatchesStoredText(const std::vector<PageWordHit>& annWords, const size_t wordLo, const size_t wordHi,
                                const std::string& storedText) {
  if (storedText.empty()) {
    return true;
  }
  if (wordLo > wordHi || wordHi >= annWords.size()) {
    return false;
  }
  const std::vector<std::string> expected = splitAnnotationWords(storedText);
  if (expected.empty()) {
    return false;
  }
  const size_t consumed = matchPhraseAt(annWords, wordLo, expected);
  return consumed > 0 && wordLo + consumed - 1 == wordHi;
}

std::string pageShardPath(const std::string& cachePath, int spine, int page) {
  char buf[48];
  snprintf(buf, sizeof(buf), "/ann/s_%05d_p_%05d.bin", spine, page);
  return cachePath + buf;
}

bool readSectionPageCount(const std::string& cachePath, int spineIndex, uint16_t* outCount) {
  if (!outCount) {
    return false;
  }
  const std::string path = cachePath + "/sections/" + std::to_string(spineIndex) + ".bin";
  FsFile file;
  if (!SdMan.openFileForRead("SCT", path, file)) {
    return false;
  }
  uint8_t version = 0;
  serialization::readPod(file, version);
  if (version != 11 && version != 10) {
    file.close();
    return false;
  }
  int storedFontId = 0;
  float storedLineCompression = 0;
  bool storedExtraParagraphSpacing = false;
  uint8_t storedParagraphAlignment = 0;
  uint16_t storedViewportWidth = 0;
  uint16_t storedViewportHeight = 0;
  bool storedHyphenationEnabled = false;
  uint16_t storedPageCount = 0;
  uint32_t storedLutOffset = 0;
  serialization::readPod(file, storedFontId);
  serialization::readPod(file, storedLineCompression);
  serialization::readPod(file, storedExtraParagraphSpacing);
  serialization::readPod(file, storedParagraphAlignment);
  serialization::readPod(file, storedViewportWidth);
  serialization::readPod(file, storedViewportHeight);
  serialization::readPod(file, storedHyphenationEnabled);
  if (version >= 11) {
    bool storedRespectCssIndent = false;
    serialization::readPod(file, storedRespectCssIndent);
  }
  serialization::readPod(file, storedPageCount);
  serialization::readPod(file, storedLutOffset);
  file.close();
  *outCount = storedPageCount;
  return true;
}

bool appendPagesForSpine(const std::string& cachePath, int spine, int pLo, int pHi,
                         std::vector<std::pair<int, int>>& out) {
  uint16_t pc = 0;
  if (!readSectionPageCount(cachePath, spine, &pc) || pc == 0) {
    return false;
  }
  const int last = static_cast<int>(pc) - 1;
  const int lo = std::max(0, pLo);
  const int hi = std::min(last, pHi);
  if (lo > hi) {
    return false;
  }
  for (int p = lo; p <= hi; ++p) {
    out.emplace_back(spine, p);
  }
  return true;
}

bool enumeratePagesForRecord(const EpubAnnotationRecord& rec, const std::string& cachePath, int spineItemsCount,
                             std::vector<std::pair<int, int>>& out) {
  out.clear();
  constexpr uint16_t w = EpubAnnotations::kWildcard;
  if (rec.startSpine == w || rec.endSpine == w) {
    return false;
  }
  const int ss = static_cast<int>(rec.startSpine);
  const int es = static_cast<int>(rec.endSpine);
  const int sp = static_cast<int>(rec.startPage);
  const int ep = static_cast<int>(rec.endPage);
  if (es < ss || es >= spineItemsCount) {
    return false;
  }
  if (ss == es) {
    if (appendPagesForSpine(cachePath, ss, sp, ep, out) && !out.empty()) {
      return true;
    }
    // Section page-count unread (common right after a cache rebuild): still persist every page in the
    // span so the start page is not dropped when Save is pressed on the end page.
    out.clear();
    const int lo = std::min(sp, ep);
    const int hi = std::max(sp, ep);
    if (hi - lo > 64) {
      out.emplace_back(ss, sp);
      out.emplace_back(ss, ep);
      return true;
    }
    for (int p = lo; p <= hi; ++p) {
      out.emplace_back(ss, p);
    }
    return !out.empty();
  }
  constexpr int kHuge = 0x7fffffff;
  if (!appendPagesForSpine(cachePath, ss, sp, kHuge, out)) {
    return false;
  }
  for (int s = ss + 1; s <= es - 1; ++s) {
    (void)appendPagesForSpine(cachePath, s, 0, kHuge, out);
  }
  if (!appendPagesForSpine(cachePath, es, 0, ep, out)) {
    return false;
  }
  return !out.empty();
}

void trimOldest(std::vector<EpubAnnotationRecord>& records, size_t maxN) {
  while (records.size() > maxN) {
    records.erase(records.begin());
  }
}

bool loadAnn3(const std::string& path, std::vector<EpubAnnotationRecord>& out) {
  out.clear();
  FsFile rf;
  if (!SdMan.openFileForRead("ANN", path, rf)) {
    return false;
  }
  uint32_t magic = 0;
  if (rf.read(&magic, sizeof(magic)) != sizeof(magic)) {
    rf.close();
    return false;
  }
  if (magic != kAnnMagicV3) {
    rf.close();
    return false;
  }
  uint16_t count = 0;
  if (rf.read(&count, sizeof(count)) != sizeof(count)) {
    rf.close();
    return false;
  }
  constexpr uint16_t kMaxLoad = 250;
  for (uint16_t i = 0; i < count && i < kMaxLoad; ++i) {
    uint32_t ts = 0;
    uint16_t len = 0;
    if (rf.read(&ts, sizeof(ts)) != sizeof(ts)) {
      break;
    }
    if (rf.read(&len, sizeof(len)) != sizeof(len)) {
      break;
    }
    std::string s;
    if (len > 0) {
      std::vector<char> buf(static_cast<size_t>(len));
      if (rf.read(buf.data(), len) != len) {
        break;
      }
      s.assign(buf.begin(), buf.end());
    }
    EpubAnnotationRecord rec{};
    rec.timestamp = ts;
    rec.text = std::move(s);
    uint16_t ss = EpubAnnotations::kWildcard;
    uint16_t sp = 0;
    uint16_t es = EpubAnnotations::kWildcard;
    uint16_t ep = 0;
    if (rf.read(&ss, sizeof(ss)) != sizeof(ss)) {
      break;
    }
    if (rf.read(&sp, sizeof(sp)) != sizeof(sp)) {
      break;
    }
    if (rf.read(&es, sizeof(es)) != sizeof(es)) {
      break;
    }
    if (rf.read(&ep, sizeof(ep)) != sizeof(ep)) {
      break;
    }
    rec.startSpine = ss;
    rec.startPage = sp;
    rec.endSpine = es;
    rec.endPage = ep;
    uint16_t wl = EpubAnnotations::kWildcard;
    uint16_t wh = EpubAnnotations::kWildcard;
    uint16_t swl = EpubAnnotations::kWildcard;
    uint16_t swh = EpubAnnotations::kWildcard;
    if (rf.read(&wl, sizeof(wl)) != sizeof(wl)) {
      break;
    }
    if (rf.read(&wh, sizeof(wh)) != sizeof(wh)) {
      break;
    }
    if (rf.read(&swl, sizeof(swl)) != sizeof(swl)) {
      break;
    }
    if (rf.read(&swh, sizeof(swh)) != sizeof(swh)) {
      break;
    }
    rec.pageWordLo = wl;
    rec.pageWordHi = wh;
    rec.startPageWordLo = swl;
    rec.startPageWordHi = swh;
    out.push_back(std::move(rec));
  }
  rf.close();
  return true;
}

bool writeAnn3(const std::string& path, const std::vector<EpubAnnotationRecord>& records) {
  FsFile wf;
  if (!SdMan.openFileForWrite("ANN", path.c_str(), wf)) {
    return false;
  }
  const uint32_t mag = kAnnMagicV3;
  wf.write(&mag, sizeof(mag));
  uint16_t count = static_cast<uint16_t>(records.size());
  wf.write(&count, sizeof(count));
  for (const auto& rec : records) {
    wf.write(&rec.timestamp, sizeof(rec.timestamp));
    uint16_t len = static_cast<uint16_t>(rec.text.size());
    wf.write(&len, sizeof(len));
    if (len > 0) {
      wf.write(rec.text.data(), len);
    }
    wf.write(&rec.startSpine, sizeof(rec.startSpine));
    wf.write(&rec.startPage, sizeof(rec.startPage));
    wf.write(&rec.endSpine, sizeof(rec.endSpine));
    wf.write(&rec.endPage, sizeof(rec.endPage));
    wf.write(&rec.pageWordLo, sizeof(rec.pageWordLo));
    wf.write(&rec.pageWordHi, sizeof(rec.pageWordHi));
    wf.write(&rec.startPageWordLo, sizeof(rec.startPageWordLo));
    wf.write(&rec.startPageWordHi, sizeof(rec.startPageWordHi));
  }
  wf.close();
  return true;
}

}  // namespace

void EpubAnnotations::clearSession() {
  records_.clear();
  cacheSpine_ = -1;
  cachePage_ = -1;
}

void EpubAnnotations::ensurePageLoaded(const std::string& cachePath, const int spine, const int page) {
  if (cacheSpine_ == spine && cachePage_ == page) {
    return;
  }
  records_.clear();
  const std::string path = pageShardPath(cachePath, spine, page);
  if (SdMan.exists(path.c_str())) {
    loadAnn3(path, records_);
  }
  // Older saves wrote a multi-page highlight only to the page where Save was pressed (usually the
  // end page). Peek at the next shard so the start page can still paint its half.
  const std::string nextPath = pageShardPath(cachePath, spine, page + 1);
  if (SdMan.exists(nextPath.c_str())) {
    std::vector<EpubAnnotationRecord> nextRecs;
    if (loadAnn3(nextPath, nextRecs)) {
      for (EpubAnnotationRecord& rec : nextRecs) {
        if (!recordTouchesPage(rec, spine, page)) {
          continue;
        }
        const bool dup = std::any_of(records_.begin(), records_.end(), [&](const EpubAnnotationRecord& have) {
          return have.timestamp == rec.timestamp && have.startSpine == rec.startSpine &&
                 have.startPage == rec.startPage && have.endSpine == rec.endSpine && have.endPage == rec.endPage &&
                 have.text == rec.text;
        });
        if (!dup) {
          records_.push_back(std::move(rec));
        }
      }
    }
  }
  cacheSpine_ = spine;
  cachePage_ = page;
}

void EpubAnnotations::clearPageShard(const std::string& cachePath, const int spine, const int page) {
  const std::string path = pageShardPath(cachePath, spine, page);
  if (SdMan.exists(path.c_str())) {
    SdMan.remove(path.c_str());
    EpubNotesIndex::invalidate();
  }
  records_.clear();
  cacheSpine_ = -1;
  cachePage_ = -1;
}

bool EpubAnnotations::pageShardExists(const std::string& cachePath, const int spine, const int page) const {
  return SdMan.exists(pageShardPath(cachePath, spine, page).c_str());
}

bool EpubAnnotations::appendHighlight(const std::string& cachePath, const int spineItemsCount,
                                      const EpubAnnotationRecord& rec, const int fallbackSpine,
                                      const int fallbackPage) {
  std::vector<std::pair<int, int>> pages;
  if (!enumeratePagesForRecord(rec, cachePath, spineItemsCount, pages) || pages.empty()) {
    pages.clear();
    if (rec.startSpine != EpubAnnotations::kWildcard) {
      pages.emplace_back(static_cast<int>(rec.startSpine), static_cast<int>(rec.startPage));
    }
    if (rec.endSpine != EpubAnnotations::kWildcard &&
        (rec.endSpine != rec.startSpine || rec.endPage != rec.startPage)) {
      pages.emplace_back(static_cast<int>(rec.endSpine), static_cast<int>(rec.endPage));
    }
    pages.emplace_back(fallbackSpine, fallbackPage);
  }
  SdMan.mkdir((cachePath + "/" + std::string(kSubdir)).c_str());
  bool ok = false;
  for (const auto& pr : pages) {
    std::vector<EpubAnnotationRecord> pageRecs;
    loadAnn3(pageShardPath(cachePath, pr.first, pr.second), pageRecs);
    pageRecs.push_back(rec);
    trimOldest(pageRecs, static_cast<size_t>(kMaxPerPage));
    ok = writeAnn3(pageShardPath(cachePath, pr.first, pr.second), pageRecs) || ok;
  }
  cacheSpine_ = -1;
  if (ok) {
    EpubNotesIndex::invalidate();
  }
  return ok;
}

bool EpubAnnotations::recordTouchesPage(const EpubAnnotationRecord& r, const int currentSpine, const int currentPage) {
  if (r.startSpine == EpubAnnotations::kWildcard) {
    return true;
  }
  const int cs = currentSpine;
  const int cp = currentPage;
  const int ss = static_cast<int>(r.startSpine);
  const int es = static_cast<int>(r.endSpine);
  const int sp = static_cast<int>(r.startPage);
  const int ep = static_cast<int>(r.endPage);
  if (cs < ss || cs > es) {
    return false;
  }
  if (ss == es) {
    return cp >= sp && cp <= ep;
  }
  if (cs == ss) {
    return cp >= sp;
  }
  if (cs == es) {
    return cp <= ep;
  }
  return cs > ss && cs < es;
}

bool EpubAnnotations::tryAppendPreciseHighlightRanges(const EpubAnnotationRecord& r, const int cs, const int cp,
                                                      const std::vector<PageWordHit>& annWords,
                                                      std::vector<std::pair<size_t, size_t>>& raw) {
  const int ss = static_cast<int>(r.startSpine);
  const int es = static_cast<int>(r.endSpine);
  const int sp = static_cast<int>(r.startPage);
  const int ep = static_cast<int>(r.endPage);
  const size_t n = annWords.size();
  if (n == 0) {
    return false;
  }
  const size_t last = n - 1;

  auto clampAppend = [&](size_t wordLo, size_t wordHi) -> bool {
    if (wordLo > last) {
      return false;
    }
    wordHi = std::min(wordHi, last);
    if (wordLo > wordHi) {
      return false;
    }
    raw.emplace_back(wordLo, wordHi);
    return true;
  };

  const bool samePage = ss == es && sp == ep;
  if (samePage) {
    if (cs != ss || cp != sp || r.pageWordLo == EpubAnnotations::kWildcard) {
      return false;
    }
    const size_t lo = static_cast<size_t>(r.pageWordLo);
    const size_t hi = r.pageWordHi == EpubAnnotations::kWildcard ? last : static_cast<size_t>(r.pageWordHi);
    if (!wordRangeMatchesStoredText(annWords, lo, hi, r.text)) {
      return false;
    }
    return clampAppend(lo, hi);
  }

  if (cs == ss && cp == sp) {
    if (r.startPageWordLo == EpubAnnotations::kWildcard) {
      return false;
    }
    size_t hi = last;
    if (r.startPageWordHi != EpubAnnotations::kThroughEndOfPage && r.startPageWordHi != EpubAnnotations::kWildcard) {
      hi = static_cast<size_t>(r.startPageWordHi);
    }
    return clampAppend(static_cast<size_t>(r.startPageWordLo), hi);
  }
  if (cs == es && cp == ep) {
    if (r.pageWordLo == EpubAnnotations::kWildcard) {
      return false;
    }
    const size_t hi = r.pageWordHi == EpubAnnotations::kWildcard ? last : static_cast<size_t>(r.pageWordHi);
    return clampAppend(static_cast<size_t>(r.pageWordLo), hi);
  }
  if (recordTouchesPage(r, cs, cp)) {
    return clampAppend(0, last);
  }
  return false;
}

void EpubAnnotations::mergeStoredRangesForPage(const std::vector<EpubAnnotationRecord>& diskRecs,
                                               const int currentSpine, const int currentPage,
                                               const std::vector<PageWordHit>& annWords,
                                               std::vector<std::pair<size_t, size_t>>& outMerged) {
  outMerged.clear();
  if (annWords.empty() || diskRecs.empty()) {
    return;
  }
  std::vector<std::pair<size_t, size_t>> raw;
  for (const EpubAnnotationRecord& diskRec : diskRecs) {
    if (!recordTouchesPage(diskRec, currentSpine, currentPage)) {
      continue;
    }
    if (tryAppendPreciseHighlightRanges(diskRec, currentSpine, currentPage, annWords, raw)) {
      continue;
    }
    const std::vector<std::string> aw = splitAnnotationWords(diskRec.text);
    if (aw.empty()) {
      continue;
    }
    const size_t n = annWords.size();
    for (size_t a = 0; a < aw.size(); ++a) {
      for (size_t i = 0; i < n; ++i) {
        const size_t firstConsumed = matchWordAllowingHyphenation(annWords, i, aw[a]);
        if (firstConsumed == 0) {
          continue;
        }
        size_t pos = i + firstConsumed;
        size_t k = 1;
        while (a + k < aw.size() && pos < n) {
          const size_t consumed = matchWordAllowingHyphenation(annWords, pos, aw[a + k]);
          if (consumed == 0) {
            break;
          }
          pos += consumed;
          ++k;
        }
        // Only trust a run that reaches the end of the stored phrase (a full or tail match) or the end of
        // this page's words (a match that continues onto the next page, for a highlight spanning pages) -
        // a run that stops partway through both is coincidental (e.g. a common short word like "a"
        // recurring elsewhere on the page), not the highlighted phrase.
        if (a + k == aw.size() || pos == n) {
          raw.emplace_back(i, pos - 1);
        }
      }
    }
  }
  if (raw.empty()) {
    return;
  }
  std::sort(raw.begin(), raw.end());
  std::vector<std::pair<size_t, size_t>> merged;
  auto cur = raw[0];
  for (size_t j = 1; j < raw.size(); ++j) {
    if (raw[j].first <= cur.second + 1) {
      cur.second = std::max(cur.second, raw[j].second);
    } else {
      merged.push_back(cur);
      cur = raw[j];
    }
  }
  merged.push_back(cur);
  outMerged = std::move(merged);
}

void EpubAnnotations::migrateSpineAnnotations(const std::string& cachePath, const int spineIndex,
                                              const int newPageCount, GfxRenderer& renderer, const int bodyFontId,
                                              const int headerFontId, const int marginLeft, const int marginTop) {
  const std::string annDir = cachePath + "/" + std::string(kSubdir);
  if (!SdMan.exists(annDir.c_str())) {
    return;
  }
  std::vector<String> files = SdMan.listFiles(annDir.c_str());
  std::vector<std::string> shardPaths;
  for (const String& f : files) {
    int s = 0;
    int p = 0;
    if (std::sscanf(f.c_str(), "s_%d_p_%d.bin", &s, &p) != 2 || s != spineIndex) {
      continue;
    }
    shardPaths.push_back(annDir + "/" + f.c_str());
  }
  if (shardPaths.empty()) {
    return;
  }

  // A multi-page record was written into every shard it touches, so gathering from all of this spine's
  // shards can yield duplicates of the same logical record - collapse those before relocating anything.
  std::vector<EpubAnnotationRecord> allRecords;
  {
    std::vector<std::string> seenKeys;
    for (const std::string& path : shardPaths) {
      std::vector<EpubAnnotationRecord> recs;
      loadAnn3(path, recs);
      for (auto& r : recs) {
        char keyBuf[64];
        std::snprintf(keyBuf, sizeof(keyBuf), "%u|%u|%u|%u|%u", r.timestamp, r.startSpine, r.startPage, r.endSpine,
                     r.endPage);
        std::string key(keyBuf);
        key += "|";
        key += r.text;
        if (std::find(seenKeys.begin(), seenKeys.end(), key) != seenKeys.end()) {
          continue;
        }
        seenKeys.push_back(std::move(key));
        allRecords.push_back(std::move(r));
      }
    }
  }
  for (const std::string& path : shardPaths) {
    SdMan.remove(path.c_str());
  }
  if (allRecords.empty()) {
    return;
  }

  std::vector<bool> resolved(allRecords.size(), false);

  for (int page = 0; page < newPageCount; ++page) {
    if (std::all_of(resolved.begin(), resolved.end(), [](const bool b) { return b; })) {
      break;
    }
    std::unique_ptr<Page> pageObj = Section::loadCachedPage(cachePath, spineIndex, page);
    if (!pageObj) {
      continue;
    }
    std::vector<PageWordHit> pageWords;
    buildPageWordIndex(*pageObj, renderer, bodyFontId, headerFontId, marginLeft, marginTop, pageWords, nullptr,
                       /*omitStoredWordStrings=*/false);
    if (pageWords.empty()) {
      continue;
    }

    for (size_t idx = 0; idx < allRecords.size(); ++idx) {
      if (resolved[idx]) {
        continue;
      }
      EpubAnnotationRecord& r = allRecords[idx];
      // Only single-page-within-this-spine records get relocated by phrase search; a multi-page span keeps
      // its stored position below rather than risk mis-splitting it across the new pagination.
      if (r.startSpine != spineIndex || r.endSpine != spineIndex || r.startPage != r.endPage) {
        continue;
      }
      const std::vector<std::string> phrase = splitAnnotationWords(r.text);
      if (phrase.empty()) {
        continue;
      }
      for (size_t i = 0; i < pageWords.size(); ++i) {
        const size_t consumed = matchPhraseAt(pageWords, i, phrase);
        if (consumed > 0) {
          r.startPage = static_cast<uint16_t>(page);
          r.endPage = static_cast<uint16_t>(page);
          r.pageWordLo = static_cast<uint16_t>(i);
          r.pageWordHi = static_cast<uint16_t>(i + consumed - 1);
          r.startPageWordLo = EpubAnnotations::kWildcard;
          r.startPageWordHi = EpubAnnotations::kWildcard;
          resolved[idx] = true;
          break;
        }
      }
    }
  }

  // Anything not relocated (phrase not found anywhere, or a multi-page span) keeps its stored position so
  // the highlight isn't lost outright, clamped into range and with its index marked unknown so a future
  // render falls back to a same-page phrase search rather than trusting a now-unverifiable range.
  std::map<int, std::vector<EpubAnnotationRecord>> byNewPage;
  for (size_t idx = 0; idx < allRecords.size(); ++idx) {
    EpubAnnotationRecord& r = allRecords[idx];
    if (!resolved[idx]) {
      r.pageWordLo = EpubAnnotations::kWildcard;
      r.pageWordHi = EpubAnnotations::kWildcard;
      r.startPageWordLo = EpubAnnotations::kWildcard;
      r.startPageWordHi = EpubAnnotations::kWildcard;
      if (newPageCount > 0) {
        if (r.startPage >= newPageCount) r.startPage = static_cast<uint16_t>(newPageCount - 1);
        if (r.endPage >= newPageCount) r.endPage = static_cast<uint16_t>(newPageCount - 1);
      }
    }
    byNewPage[static_cast<int>(r.startPage)].push_back(r);
    if (r.endPage != r.startPage) {
      byNewPage[static_cast<int>(r.endPage)].push_back(r);
    }
  }

  SdMan.mkdir(annDir.c_str());
  for (auto& kv : byNewPage) {
    trimOldest(kv.second, static_cast<size_t>(kMaxPerPage));
    writeAnn3(pageShardPath(cachePath, spineIndex, kv.first), kv.second);
  }
  EpubNotesIndex::invalidate();
}
