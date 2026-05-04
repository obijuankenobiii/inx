/**
 * @file EpubAnnotations.cpp
 */

#include "EpubAnnotations.h"

#include <Serialization.h>

#include <SDCardManager.h>
#include <algorithm>
#include <cstdio>
#include <sstream>

namespace {

constexpr uint32_t kAnnMagicV3 = 0x334E4E41;  // "ANN3"

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
  bool storedRespectCssIndent = false;
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
  if (ss < 0 || es < ss || es >= spineItemsCount) {
    return false;
  }
  if (ss == es) {
    return appendPagesForSpine(cachePath, ss, sp, ep, out) && !out.empty();
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
  loadAnn3(pageShardPath(cachePath, spine, page), records_);
  cacheSpine_ = spine;
  cachePage_ = page;
}

void EpubAnnotations::clearPageShard(const std::string& cachePath, const int spine, const int page) {
  const std::string path = pageShardPath(cachePath, spine, page);
  if (SdMan.exists(path.c_str())) {
    SdMan.remove(path.c_str());
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
    return cs == ss && cp >= sp && cp <= ep;
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
  if (r.pageWordLo == EpubAnnotations::kWildcard) {
    return false;
  }
  const int ss = static_cast<int>(r.startSpine);
  const int es = static_cast<int>(r.endSpine);
  const int sp = static_cast<int>(r.startPage);
  const int ep = static_cast<int>(r.endPage);
  const size_t n = annWords.size();

  auto appendRange = [&](const size_t wordLo, const size_t wordHi) {
    if (n == 0 || wordLo >= n || wordHi >= n || wordLo > wordHi) {
      return;
    }
    raw.emplace_back(wordLo, wordHi);
  };

  if (ss == es && sp == ep) {
    if (cs == ss && cp == ep) {
      appendRange(static_cast<size_t>(r.pageWordLo), static_cast<size_t>(r.pageWordHi));
    }
    return true;
  }

  if (ss != es || cs != ss) {
    return false;
  }

  if (cp == sp && cp < ep) {
    if (r.startPageWordLo != EpubAnnotations::kWildcard) {
      appendRange(static_cast<size_t>(r.startPageWordLo), static_cast<size_t>(r.startPageWordHi));
      return true;
    }
    return false;
  }
  if (cp == ep && cp > sp) {
    appendRange(static_cast<size_t>(r.pageWordLo), static_cast<size_t>(r.pageWordHi));
    return true;
  }
  if (cp > sp && cp < ep && n > 0) {
    appendRange(0, n - 1);
    return true;
  }
  return true;
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
    const std::string& ann = diskRec.text;
    std::vector<std::string> aw;
    {
      std::istringstream iss(ann);
      std::string w;
      while (iss >> w) {
        aw.push_back(std::move(w));
      }
    }
    if (aw.empty()) {
      continue;
    }
    const size_t n = annWords.size();
    for (size_t a = 0; a < aw.size(); ++a) {
      for (size_t i = 0; i < n; ++i) {
        if (annWords[i].text != aw[a]) {
          continue;
        }
        size_t k = 0;
        while (a + k < aw.size() && i + k < n && annWords[i + k].text == aw[a + k]) {
          ++k;
        }
        if (k > 0) {
          raw.emplace_back(i, i + k - 1);
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
