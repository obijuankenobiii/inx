#include "StarDictLookup.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "util/StringUtils.h"

namespace {

std::string toLowerCopy(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

std::string toTitleCaseCopy(const std::string& s) {
  std::string out = toLowerCopy(s);
  if (!out.empty()) {
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  }
  return out;
}

/** Strips leading/trailing characters that aren't letters/digits/apostrophe/hyphen, so a word
 *  lifted straight from rendered book text (with trailing commas/periods/quotes) can still match
 *  a dictionary entry. */
std::string stripPunctuation(const std::string& s) {
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

/** Generates candidate base forms for a possibly-inflected English word (possessive, plural,
 *  past tense -ed, gerund -ing), so a form absent from the dictionary (e.g. "running", "jumped",
 *  "books") can still resolve to its base entry ("run", "jump", "book"). Heuristic suffix-stripping,
 *  not a full stemmer - callers try each candidate as an exact lookup and take the first hit, so
 *  over-generating (a few wrong candidates) is harmless as long as the right one is in the list. */
std::vector<std::string> stemCandidates(const std::string& lower) {
  std::vector<std::string> out;
  const size_t n = lower.size();
  auto add = [&](const std::string& s) {
    if (s.size() >= 2) {
      out.push_back(s);
    }
  };

  if (n > 2 && lower[n - 2] == '\'' && lower[n - 1] == 's') {
    add(lower.substr(0, n - 2));
  }

  if (n > 4 && lower.compare(n - 3, 3, "ing") == 0) {
    const std::string base = lower.substr(0, n - 3);
    add(base);
    add(base + "e");
    if (base.size() >= 3 && base[base.size() - 1] == base[base.size() - 2]) {
      add(base.substr(0, base.size() - 1));
    }
  }

  if (n > 3 && lower.compare(n - 3, 3, "ied") == 0) {
    add(lower.substr(0, n - 3) + "y");
  }

  if (n > 3 && lower.compare(n - 2, 2, "ed") == 0) {
    const std::string base = lower.substr(0, n - 2);
    add(base);
    add(base + "e");
    if (base.size() >= 3 && base[base.size() - 1] == base[base.size() - 2]) {
      add(base.substr(0, base.size() - 1));
    }
  }

  if (n > 4 && lower.compare(n - 3, 3, "ies") == 0) {
    add(lower.substr(0, n - 3) + "y");
  }

  if (n > 3 && lower.compare(n - 2, 2, "es") == 0) {
    add(lower.substr(0, n - 2));
  }

  if (n > 2 && lower[n - 1] == 's' && lower[n - 2] != 's') {
    add(lower.substr(0, n - 1));
  }

  return out;
}

void pushUnique(std::vector<std::string>& list, const std::string& s) {
  if (s.empty()) {
    return;
  }
  if (std::find(list.begin(), list.end(), s) == list.end()) {
    list.push_back(s);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// IdxCursor - buffered sequential .idx access
// ---------------------------------------------------------------------------

StarDictLookup::IdxCursor::IdxCursor(FsFile& file, const uint32_t fileSize) : file_(file), fileSize_(fileSize) {}

bool StarDictLookup::IdxCursor::fillFrom(const uint32_t absOffset) {
  if (absOffset >= fileSize_) {
    bufBase_ = absOffset;
    bufLen_ = 0;
    return false;
  }
  if (!file_.seekSet(absOffset)) {
    bufBase_ = absOffset;
    bufLen_ = 0;
    return false;
  }
  const uint32_t toRead = std::min(kBufSize, fileSize_ - absOffset);
  const int n = file_.read(buf_, toRead);
  if (n <= 0) {
    bufBase_ = absOffset;
    bufLen_ = 0;
    return false;
  }
  bufBase_ = absOffset;
  bufLen_ = static_cast<uint32_t>(n);
  return true;
}

bool StarDictLookup::IdxCursor::ensure(const uint32_t needBytes) {
  if (pos_ >= fileSize_) {
    return false;
  }
  if (pos_ >= bufBase_ && (pos_ + needBytes) <= (bufBase_ + bufLen_)) {
    return true;
  }
  // Prefer filling a fresh window starting at pos_. If the remaining file is shorter than needBytes
  // we still succeed as long as at least one byte is available (callers check partial failures).
  return fillFrom(pos_) && bufLen_ > 0;
}

bool StarDictLookup::IdxCursor::seek(const uint32_t absOffset) {
  pos_ = absOffset;
  if (pos_ >= bufBase_ && pos_ < (bufBase_ + bufLen_)) {
    return pos_ <= fileSize_;
  }
  // Lazy: don't hit the SD until the next read. Mark buffer empty so ensure() refills.
  if (pos_ < bufBase_ || pos_ >= (bufBase_ + bufLen_)) {
    bufLen_ = 0;
  }
  return pos_ <= fileSize_;
}

bool StarDictLookup::IdxCursor::readRaw(uint8_t* dest, const uint32_t n) {
  uint32_t remaining = n;
  uint32_t wrote = 0;
  while (remaining > 0) {
    if (!ensure(1)) {
      return false;
    }
    const uint32_t bufOff = pos_ - bufBase_;
    const uint32_t avail = bufLen_ - bufOff;
    const uint32_t take = std::min(remaining, avail);
    std::memcpy(dest + wrote, buf_ + bufOff, take);
    pos_ += take;
    wrote += take;
    remaining -= take;
  }
  return true;
}

bool StarDictLookup::IdxCursor::readCString(std::string& out) {
  out.clear();
  while (true) {
    if (!ensure(1)) {
      return false;
    }
    const uint8_t c = buf_[pos_ - bufBase_];
    ++pos_;
    if (c == 0) {
      return true;
    }
    out.push_back(static_cast<char>(c));
    if (out.size() > 256) {
      return false;
    }
  }
}

bool StarDictLookup::IdxCursor::readBE32(uint32_t& out) {
  uint8_t b[4];
  if (!readRaw(b, 4)) {
    return false;
  }
  out = (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
        (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
  return true;
}

bool StarDictLookup::IdxCursor::readBE64(uint64_t& out) {
  uint8_t b[8];
  if (!readRaw(b, 8)) {
    return false;
  }
  out = 0;
  for (int i = 0; i < 8; ++i) {
    out = (out << 8) | static_cast<uint64_t>(b[i]);
  }
  return true;
}

// ---------------------------------------------------------------------------
// StarDictLookup
// ---------------------------------------------------------------------------

void StarDictLookup::close() {
  if (idxFile_) {
    idxFile_.close();
  }
  if (dictFile_) {
    dictFile_.close();
  }
  // swap, not .clear() - checkpoints_ is the in-RAM index built by buildCheckpoints() (hundreds of
  // entries for a large dictionary), and .clear() alone would leave that capacity reserved.
  std::vector<Checkpoint>().swap(checkpoints_);
  std::vector<DefCacheEntry>().swap(defCache_);
  std::string().swap(folderPath_);
  std::string().swap(bookname_);
  std::string().swap(sameTypeSequence_);
  wordCount_ = 0;
  idxFileSize_ = 0;
  use64BitOffsets_ = false;
  caseInsensitiveSort_ = false;
  isOpen_ = false;
}

bool StarDictLookup::parseIfo(const std::string& ifoPath) {
  const String contents = SdMan.readFile(ifoPath.c_str());
  if (contents.isEmpty()) {
    return false;
  }

  int lineStart = 0;
  const int len = contents.length();
  while (lineStart < len) {
    int lineEnd = contents.indexOf('\n', lineStart);
    if (lineEnd < 0) {
      lineEnd = len;
    }
    String line = contents.substring(lineStart, lineEnd);
    line.trim();
    lineStart = lineEnd + 1;

    const int eq = line.indexOf('=');
    if (eq <= 0) {
      continue;
    }
    const String key = line.substring(0, eq);
    String value = line.substring(eq + 1);
    value.trim();

    if (key == "bookname") {
      bookname_ = value.c_str();
    } else if (key == "wordcount") {
      wordCount_ = static_cast<uint32_t>(value.toInt());
    } else if (key == "idxfilesize") {
      idxFileSize_ = static_cast<uint32_t>(value.toInt());
    } else if (key == "sametypesequence") {
      sameTypeSequence_ = value.c_str();
    } else if (key == "idxoffsetbits") {
      use64BitOffsets_ = (value.toInt() == 64);
    }
  }

  return true;
}

bool StarDictLookup::open(const std::string& folderPath) {
  // Already open on the same folder - keep warm index/files (common when dictionary mode is re-entered).
  if (isOpen_ && folderPath_ == folderPath) {
    return true;
  }
  close();

  std::string ifoPath, idxPath, dictPath;
  for (const String& name : SdMan.listFiles(folderPath.c_str())) {
    // Skip dotfiles, in particular macOS AppleDouble sidecar junk ("._stardict.idx" etc) that macOS
    // silently creates when copying onto FAT32/exFAT SD cards - these have real-looking extensions
    // and would otherwise shadow the actual .ifo/.idx/.dict files.
    if (name.length() > 0 && name[0] == '.') {
      continue;
    }
    const std::string full = folderPath + "/" + name.c_str();
    if (StringUtils::checkFileExtension(name, ".ifo")) {
      ifoPath = full;
    } else if (StringUtils::checkFileExtension(name, ".idx")) {
      idxPath = full;
    } else if (StringUtils::checkFileExtension(name, ".dict")) {
      dictPath = full;
    }
  }

  if (ifoPath.empty() || idxPath.empty() || dictPath.empty()) {
    Serial.printf("[%lu] [DICT] Missing .ifo/.idx/.dict under %s (ifo='%s' idx='%s' dict='%s')\n", millis(),
                  folderPath.c_str(), ifoPath.c_str(), idxPath.c_str(), dictPath.c_str());
    return false;
  }
  Serial.printf("[%lu] [DICT] Found ifo='%s' idx='%s' dict='%s'\n", millis(), ifoPath.c_str(), idxPath.c_str(),
                dictPath.c_str());

  if (!parseIfo(ifoPath)) {
    Serial.printf("[%lu] [DICT] Could not parse %s\n", millis(), ifoPath.c_str());
    return false;
  }
  Serial.printf("[%lu] [DICT] .ifo says bookname='%s' wordcount=%u idxfilesize=%u sametypesequence='%s' "
                "idxoffsetbits64=%d\n",
                millis(), bookname_.c_str(), wordCount_, idxFileSize_, sameTypeSequence_.c_str(),
                use64BitOffsets_ ? 1 : 0);

  if (!SdMan.openFileForRead("DICT", idxPath, idxFile_) || !SdMan.openFileForRead("DICT", dictPath, dictFile_)) {
    Serial.printf("[%lu] [DICT] Could not open .idx/.dict under %s\n", millis(), folderPath.c_str());
    close();
    return false;
  }

  // Some third-party-generated .ifo files carry a stale/wrong idxfilesize (e.g. after the .idx was
  // regenerated by a converter that didn't update the header). Always trust the actual on-disk size.
  const uint32_t actualIdxSize = static_cast<uint32_t>(idxFile_.fileSize());
  if (idxFileSize_ != actualIdxSize) {
    Serial.printf("[%lu] [DICT] .ifo idxfilesize=%u does not match actual .idx size=%u - using actual\n", millis(),
                  idxFileSize_, actualIdxSize);
  }
  idxFileSize_ = actualIdxSize;
  Serial.printf("[%lu] [DICT] .idx actual size=%u .dict actual size=%llu\n", millis(), idxFileSize_,
                static_cast<unsigned long long>(dictFile_.fileSize()));

  const unsigned long t0 = millis();
  if (!buildCheckpoints()) {
    Serial.printf("[%lu] [DICT] Could not build index checkpoints for %s\n", millis(), folderPath.c_str());
    close();
    return false;
  }

  folderPath_ = folderPath;
  isOpen_ = true;
  Serial.printf("[%lu] [DICT] Opened '%s' (%u words, %u checkpoints, %s offsets, sort=%s) in %lums\n", millis(),
                bookname_.c_str(), wordCount_, static_cast<unsigned>(checkpoints_.size()),
                use64BitOffsets_ ? "64-bit" : "32-bit", caseInsensitiveSort_ ? "case-insensitive" : "byte-order",
                millis() - t0);
  return true;
}

bool StarDictLookup::readIdxEntry(IdxCursor& cur, std::string& outEntryText, uint64_t& outDictOffset,
                                  uint32_t& outDictSize) {
  if (!cur.readCString(outEntryText)) {
    return false;
  }
  if (use64BitOffsets_) {
    if (!cur.readBE64(outDictOffset)) {
      return false;
    }
  } else {
    uint32_t off32 = 0;
    if (!cur.readBE32(off32)) {
      return false;
    }
    outDictOffset = off32;
  }
  return cur.readBE32(outDictSize);
}

bool StarDictLookup::buildCheckpoints() {
  checkpoints_.clear();
  if (wordCount_ > 0) {
    // Reserve roughly wordCount/stride slots so we don't reallocate during the scan.
    const size_t estimate = static_cast<size_t>(wordCount_ / kCheckpointStride) + 2;
    checkpoints_.reserve(std::min(estimate, static_cast<size_t>(8192)));
  }

  IdxCursor cur(idxFile_, idxFileSize_);
  if (!cur.seek(0)) {
    return false;
  }

  uint32_t count = 0;
  std::string prevText;
  std::string prevLower;
  bool havePrev = false;
  uint32_t byteOrderViolations = 0;
  uint32_t caseFoldViolations = 0;

  while (cur.position() < idxFileSize_) {
    const uint32_t entryOffset = cur.position();
    std::string entryText;
    uint64_t dictOffset = 0;
    uint32_t dictSize = 0;
    if (!readIdxEntry(cur, entryText, dictOffset, dictSize)) {
      Serial.printf("[%lu] [DICT] buildCheckpoints: read failed at offset=%u after %u entries (idxFileSize=%u)\n",
                    millis(), entryOffset, count, idxFileSize_);
      break;
    }
    if (cur.position() <= entryOffset) {
      Serial.printf("[%lu] [DICT] buildCheckpoints: non-advancing entry at offset=%u ('%s') - stopping\n", millis(),
                    entryOffset, entryText.c_str());
      break;
    }

    const std::string entryLower = toLowerCopy(entryText);
    if (havePrev) {
      if (prevText.compare(entryText) > 0) {
        ++byteOrderViolations;
      }
      if (prevLower.compare(entryLower) > 0) {
        ++caseFoldViolations;
      }
    }
    prevText = entryText;
    prevLower = entryLower;
    havePrev = true;

    if (count % kCheckpointStride == 0) {
      checkpoints_.emplace_back(entryOffset, entryText, entryLower);
      if (checkpoints_.size() <= 3) {
        Serial.printf("[%lu] [DICT] checkpoint #%u @offset=%u entry='%s'\n", millis(),
                      static_cast<unsigned>(checkpoints_.size() - 1), entryOffset, entryText.c_str());
      }
    }
    ++count;
  }

  // Prefer case-insensitive binary search when the file is (mostly) sorted that way and NOT sorted
  // by plain byte order - the classic third-party failure mode that used to force a full linear scan.
  // Allow a few local violations (duplicate/near-dup noise) before rejecting a sort order.
  const uint32_t violationBudget = std::max<uint32_t>(4, count / 5000);
  const bool byteOk = byteOrderViolations <= violationBudget;
  const bool caseOk = caseFoldViolations <= violationBudget;
  if (caseOk && !byteOk) {
    caseInsensitiveSort_ = true;
  } else {
    caseInsensitiveSort_ = false;
  }

  Serial.printf("[%lu] [DICT] buildCheckpoints: scanned %u entries (.ifo wordcount=%u), %u checkpoints, "
                "byteOrderViolations=%u caseFoldViolations=%u sort=%s, last='%s'\n",
                millis(), count, wordCount_, static_cast<unsigned>(checkpoints_.size()), byteOrderViolations,
                caseFoldViolations, caseInsensitiveSort_ ? "case-insensitive" : "byte-order",
                checkpoints_.empty() ? "" : checkpoints_.back().entryText.c_str());
  return !checkpoints_.empty();
}

int StarDictLookup::compareForSearch(const std::string& a, const std::string& aLower, const std::string& b,
                                     const std::string& bLower) const {
  if (caseInsensitiveSort_) {
    return aLower.compare(bLower);
  }
  return a.compare(b);
}

bool StarDictLookup::lookupViaCheckpoints(const std::string& candidate, const std::string& candidateLower,
                                          uint64_t& outDictOffset, uint32_t& outDictSize) {
  if (checkpoints_.empty()) {
    return false;
  }

  // Binary search for the last checkpoint whose word is <= candidate under the detected sort order.
  size_t lo = 0, hi = checkpoints_.size();
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const Checkpoint& cp = checkpoints_[mid];
    if (compareForSearch(cp.entryText, cp.entryLower, candidate, candidateLower) <= 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo == 0) {
    return false;
  }

  const uint32_t scanStart = checkpoints_[lo - 1].idxOffset;
  const uint32_t scanEnd = (lo < checkpoints_.size()) ? checkpoints_[lo].idxOffset : idxFileSize_;

  IdxCursor cur(idxFile_, idxFileSize_);
  if (!cur.seek(scanStart)) {
    return false;
  }

  while (cur.position() < scanEnd) {
    std::string entryWord;
    uint64_t dictOffset = 0;
    uint32_t dictSize = 0;
    if (!readIdxEntry(cur, entryWord, dictOffset, dictSize)) {
      break;
    }
    const std::string entryLower = toLowerCopy(entryWord);
    const int cmp = compareForSearch(entryWord, entryLower, candidate, candidateLower);
    if (cmp == 0) {
      outDictOffset = dictOffset;
      outDictSize = dictSize;
      return true;
    }
    if (cmp > 0) {
      // Passed where the candidate would sort - not present in this bracket under the assumed order.
      // For case-insensitive sort, also accept a case-only mismatch that strcmp would reject but
      // lower-compare already handled via compareForSearch == 0 above.
      break;
    }
  }
  return false;
}

bool StarDictLookup::lookupViaLinearScan(const std::vector<std::string>& candidatesLower, std::string& outHitLower,
                                         uint64_t& outDictOffset, uint32_t& outDictSize) {
  if (candidatesLower.empty()) {
    return false;
  }

  // Track best (lowest index in candidatesLower) hit so we prefer the as-typed base form over a stem.
  int bestPriority = -1;
  uint64_t bestOffset = 0;
  uint32_t bestSize = 0;
  std::string bestLower;

  IdxCursor cur(idxFile_, idxFileSize_);
  if (!cur.seek(0)) {
    return false;
  }

  while (cur.position() < idxFileSize_) {
    std::string entryWord;
    uint64_t dictOffset = 0;
    uint32_t dictSize = 0;
    if (!readIdxEntry(cur, entryWord, dictOffset, dictSize)) {
      break;
    }
    const std::string entryLower = toLowerCopy(entryWord);
    for (size_t i = 0; i < candidatesLower.size(); ++i) {
      if (entryLower == candidatesLower[i]) {
        if (bestPriority < 0 || static_cast<int>(i) < bestPriority) {
          bestPriority = static_cast<int>(i);
          bestOffset = dictOffset;
          bestSize = dictSize;
          bestLower = entryLower;
          // Exact first candidate - can't do better.
          if (bestPriority == 0) {
            outHitLower = bestLower;
            outDictOffset = bestOffset;
            outDictSize = bestSize;
            return true;
          }
        }
        break;
      }
    }
  }

  if (bestPriority < 0) {
    return false;
  }
  outHitLower = bestLower;
  outDictOffset = bestOffset;
  outDictSize = bestSize;
  return true;
}

bool StarDictLookup::readDefinition(const uint64_t dictOffset, const uint32_t dictSize, std::string& outDefinition,
                                    bool* outTruncated) {
  if (dictSize == 0 || !dictFile_.seekSet(dictOffset)) {
    return false;
  }
  const uint32_t readSize = std::min(dictSize, kMaxDefinitionBytes);
  if (outTruncated) {
    *outTruncated = readSize < dictSize;
  }
  outDefinition.resize(readSize);
  const int readN = dictFile_.read(&outDefinition[0], readSize);
  return readN == static_cast<int>(readSize);
}

bool StarDictLookup::cacheGet(const std::string& keyLower, std::string& outDefinition, bool* outTruncated) {
  for (size_t i = 0; i < defCache_.size(); ++i) {
    if (defCache_[i].keyLower == keyLower) {
      outDefinition = defCache_[i].definition;
      if (outTruncated) {
        *outTruncated = defCache_[i].truncated;
      }
      // Move to front (MRU).
      if (i > 0) {
        DefCacheEntry hit = std::move(defCache_[i]);
        defCache_.erase(defCache_.begin() + static_cast<std::ptrdiff_t>(i));
        defCache_.insert(defCache_.begin(), std::move(hit));
      }
      return true;
    }
  }
  return false;
}

void StarDictLookup::cachePut(const std::string& keyLower, const std::string& definition, const bool truncated) {
  for (size_t i = 0; i < defCache_.size(); ++i) {
    if (defCache_[i].keyLower == keyLower) {
      defCache_[i].definition = definition;
      defCache_[i].truncated = truncated;
      if (i > 0) {
        DefCacheEntry hit = std::move(defCache_[i]);
        defCache_.erase(defCache_.begin() + static_cast<std::ptrdiff_t>(i));
        defCache_.insert(defCache_.begin(), std::move(hit));
      }
      return;
    }
  }
  if (defCache_.size() >= kDefCacheSlots) {
    defCache_.pop_back();
  }
  defCache_.insert(defCache_.begin(), DefCacheEntry{keyLower, definition, truncated});
}

bool StarDictLookup::lookup(const std::string& queryWord, std::string& outDefinition, bool* outTruncated) {
  if (!isOpen_) {
    Serial.printf("[%lu] [DICT] lookup('%s'): dictionary not open\n", millis(), queryWord.c_str());
    return false;
  }

  const std::string cleaned = stripPunctuation(queryWord);
  if (cleaned.empty()) {
    Serial.printf("[%lu] [DICT] lookup('%s'): empty after stripPunctuation\n", millis(), queryWord.c_str());
    return false;
  }

  const std::string lowerCleaned = toLowerCopy(cleaned);

  // Cache is keyed by the cleaned lowercased query (not the stem that eventually hit) so the same
  // on-screen word always resolves instantly on re-lookup.
  if (cacheGet(lowerCleaned, outDefinition, outTruncated)) {
    Serial.printf("[%lu] [DICT] lookup('%s'): cache hit\n", millis(), queryWord.c_str());
    return true;
  }

  // Word forms to try, in priority order: as typed, lower, Title; then stems (lower + Title).
  std::vector<std::string> candidates;
  candidates.reserve(12);
  pushUnique(candidates, cleaned);
  pushUnique(candidates, lowerCleaned);
  pushUnique(candidates, toTitleCaseCopy(cleaned));
  for (const std::string& stem : stemCandidates(lowerCleaned)) {
    pushUnique(candidates, stem);
    pushUnique(candidates, toTitleCaseCopy(stem));
  }

  const unsigned long t0 = millis();
  uint64_t dictOffset = 0;
  uint32_t dictSize = 0;
  bool found = false;
  std::string hitCandidate;

  for (const std::string& candidate : candidates) {
    const std::string candidateLower = toLowerCopy(candidate);
    if (lookupViaCheckpoints(candidate, candidateLower, dictOffset, dictSize)) {
      found = true;
      hitCandidate = candidate;
      Serial.printf("[%lu] [DICT] lookup('%s'): fast path hit on candidate='%s' (%lums)\n", millis(),
                    queryWord.c_str(), candidate.c_str(), millis() - t0);
      break;
    }
  }

  if (!found) {
    Serial.printf("[%lu] [DICT] lookup('%s'): fast path missed (%lums), buffered linear scan over %u idx bytes\n",
                  millis(), queryWord.c_str(), millis() - t0, idxFileSize_);
    const unsigned long t1 = millis();
    std::vector<std::string> linearCandidates;
    linearCandidates.reserve(candidates.size());
    for (const std::string& c : candidates) {
      pushUnique(linearCandidates, toLowerCopy(c));
    }
    std::string hitLower;
    if (lookupViaLinearScan(linearCandidates, hitLower, dictOffset, dictSize)) {
      found = true;
      hitCandidate = hitLower;
    }
    Serial.printf("[%lu] [DICT] lookup('%s'): linear scan %s (%lums)\n", millis(), queryWord.c_str(),
                  found ? "hit" : "miss", millis() - t1);
  }

  if (!found) {
    return false;
  }

  Serial.printf("[%lu] [DICT] lookup('%s'): matched '%s', dictOffset=%llu dictSize=%u\n", millis(),
                queryWord.c_str(), hitCandidate.c_str(), static_cast<unsigned long long>(dictOffset), dictSize);

  bool truncated = false;
  if (!readDefinition(dictOffset, dictSize, outDefinition, &truncated)) {
    Serial.printf("[%lu] [DICT] lookup('%s'): definition read failed\n", millis(), queryWord.c_str());
    return false;
  }
  if (outTruncated) {
    *outTruncated = truncated;
  }
  cachePut(lowerCleaned, outDefinition, truncated);
  Serial.printf("[%lu] [DICT] lookup('%s'): total %lums (def %u bytes%s)\n", millis(), queryWord.c_str(),
                millis() - t0, static_cast<unsigned>(outDefinition.size()), truncated ? ", truncated" : "");
  return true;
}
