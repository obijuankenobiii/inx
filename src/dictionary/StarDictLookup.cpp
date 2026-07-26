#include "StarDictLookup.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>

#include "util/StringUtils.h"

namespace {

uint32_t readBigEndian32(FsFile& f) {
  uint8_t b[4] = {0, 0, 0, 0};
  if (f.read(b, 4) != 4) {
    return 0;
  }
  return (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
         (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
}

uint64_t readBigEndian64(FsFile& f) {
  uint8_t b[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  if (f.read(b, 8) != 8) {
    return 0;
  }
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v = (v << 8) | static_cast<uint64_t>(b[i]);
  }
  return v;
}

/** Reads a null-terminated word from f into out. Returns false on EOF before a terminator or if
 *  the word looks corrupt (unreasonably long, which would otherwise scan indefinitely). */
bool readCString(FsFile& f, std::string& out) {
  out.clear();
  char c = 0;
  while (true) {
    if (f.read(&c, 1) != 1) {
      return false;
    }
    if (c == '\0') {
      return true;
    }
    out += c;
    if (out.size() > 256) {
      return false;
    }
  }
}

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

  // Possessive: "dog's" -> "dog" (stripPunctuation keeps apostrophes, so this can reach here).
  if (n > 2 && lower[n - 2] == '\'' && lower[n - 1] == 's') {
    add(lower.substr(0, n - 2));
  }

  // -ing: running->runn->run (doubled consonant), making->make (silent e), jumping->jump.
  if (n > 4 && lower.compare(n - 3, 3, "ing") == 0) {
    const std::string base = lower.substr(0, n - 3);
    add(base);
    add(base + "e");
    if (base.size() >= 3 && base[base.size() - 1] == base[base.size() - 2]) {
      add(base.substr(0, base.size() - 1));
    }
  }

  // -ied: tried->tri->try.
  if (n > 3 && lower.compare(n - 3, 3, "ied") == 0) {
    add(lower.substr(0, n - 3) + "y");
  }

  // -ed: jumped->jump, liked/lik->like, stopped/stopp->stop.
  if (n > 3 && lower.compare(n - 2, 2, "ed") == 0) {
    const std::string base = lower.substr(0, n - 2);
    add(base);
    add(base + "e");
    if (base.size() >= 3 && base[base.size() - 1] == base[base.size() - 2]) {
      add(base.substr(0, base.size() - 1));
    }
  }

  // -ies: flies->fly, berries->berry.
  if (n > 4 && lower.compare(n - 3, 3, "ies") == 0) {
    add(lower.substr(0, n - 3) + "y");
  }

  // -es: boxes->box, watches->watch.
  if (n > 3 && lower.compare(n - 2, 2, "es") == 0) {
    add(lower.substr(0, n - 2));
  }

  // -s: books->book (skip "ss" endings like "glass" and words already handled above).
  if (n > 2 && lower[n - 1] == 's' && lower[n - 2] != 's') {
    add(lower.substr(0, n - 1));
  }

  return out;
}

}  // namespace

int StarDictLookup::compareWord(const std::string& a, const std::string& b) {
  // Plain byte-order comparison, matching StarDict's conventional strcmp-based .idx sort - this is
  // the true on-disk order the checkpoint binary search relies on.
  return a.compare(b);
}

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
  std::string().swap(bookname_);
  std::string().swap(sameTypeSequence_);
  wordCount_ = 0;
  idxFileSize_ = 0;
  use64BitOffsets_ = false;
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
  // regenerated by a converter that didn't update the header). Always trust the actual on-disk size -
  // buildCheckpoints()/lookupViaLinearScan() bound their scans by this value, so a wrong number here
  // would silently truncate or overrun the index.
  const uint32_t actualIdxSize = static_cast<uint32_t>(idxFile_.fileSize());
  if (idxFileSize_ != actualIdxSize) {
    Serial.printf("[%lu] [DICT] .ifo idxfilesize=%u does not match actual .idx size=%u - using actual\n", millis(),
                  idxFileSize_, actualIdxSize);
  }
  idxFileSize_ = actualIdxSize;
  Serial.printf("[%lu] [DICT] .idx actual size=%u .dict actual size=%llu\n", millis(), idxFileSize_,
                static_cast<unsigned long long>(dictFile_.fileSize()));

  if (!buildCheckpoints()) {
    Serial.printf("[%lu] [DICT] Could not build index checkpoints for %s\n", millis(), folderPath.c_str());
    close();
    return false;
  }

  isOpen_ = true;
  Serial.printf("[%lu] [DICT] Opened '%s' (%u words, %u checkpoints, %s offsets)\n", millis(), bookname_.c_str(),
                wordCount_, static_cast<unsigned>(checkpoints_.size()), use64BitOffsets_ ? "64-bit" : "32-bit");
  return true;
}

bool StarDictLookup::readIdxEntryAt(const uint32_t idxOffset, std::string& outEntryText, uint64_t& outDictOffset,
                                    uint32_t& outDictSize, uint32_t& outNextOffset) {
  if (!idxFile_.seekSet(idxOffset)) {
    return false;
  }
  if (!readCString(idxFile_, outEntryText)) {
    return false;
  }
  outDictOffset = use64BitOffsets_ ? readBigEndian64(idxFile_) : static_cast<uint64_t>(readBigEndian32(idxFile_));
  outDictSize = readBigEndian32(idxFile_);
  outNextOffset = static_cast<uint32_t>(idxFile_.position());
  return true;
}

bool StarDictLookup::buildCheckpoints() {
  checkpoints_.clear();
  uint32_t offset = 0;
  uint32_t count = 0;
  while (offset < idxFileSize_) {
    std::string entryText;
    uint64_t dictOffset = 0;
    uint32_t dictSize = 0, nextOffset = 0;
    if (!readIdxEntryAt(offset, entryText, dictOffset, dictSize, nextOffset)) {
      Serial.printf("[%lu] [DICT] buildCheckpoints: read failed at offset=%u after %u entries (idxFileSize=%u)\n",
                    millis(), offset, count, idxFileSize_);
      break;
    }
    if (count % kCheckpointStride == 0) {
      checkpoints_.push_back(Checkpoint{offset, entryText});
      if (checkpoints_.size() <= 3) {
        Serial.printf("[%lu] [DICT] checkpoint #%u @offset=%u entry='%s'\n", millis(),
                      static_cast<unsigned>(checkpoints_.size() - 1), offset, entryText.c_str());
      }
    }
    ++count;
    if (nextOffset <= offset) {
      // Malformed/looping entry - stop rather than spin forever.
      Serial.printf("[%lu] [DICT] buildCheckpoints: non-advancing entry at offset=%u ('%s') - stopping\n", millis(),
                    offset, entryText.c_str());
      break;
    }
    offset = nextOffset;
  }
  Serial.printf("[%lu] [DICT] buildCheckpoints: scanned %u entries total (.ifo wordcount=%u), %u checkpoints, "
                "last checkpoint entry='%s'\n",
                millis(), count, wordCount_, static_cast<unsigned>(checkpoints_.size()),
                checkpoints_.empty() ? "" : checkpoints_.back().entryText.c_str());
  return !checkpoints_.empty();
}

bool StarDictLookup::lookupViaCheckpoints(const std::string& candidate, uint64_t& outDictOffset,
                                          uint32_t& outDictSize) {
  if (checkpoints_.empty()) {
    return false;
  }
  // Binary search checkpoints_ for the last checkpoint whose word is <= candidate. Only correct if
  // .idx is actually sorted in plain byte order, per the documented StarDict convention.
  size_t lo = 0, hi = checkpoints_.size();
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (compareWord(checkpoints_[mid].entryText, candidate) <= 0) {
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

  uint32_t offset = scanStart;
  while (offset < scanEnd) {
    std::string entryWord;
    uint64_t dictOffset = 0;
    uint32_t dictSize = 0, nextOffset = 0;
    if (!readIdxEntryAt(offset, entryWord, dictOffset, dictSize, nextOffset)) {
      break;
    }
    const int cmp = compareWord(entryWord, candidate);
    if (cmp == 0) {
      outDictOffset = dictOffset;
      outDictSize = dictSize;
      return true;
    }
    if (cmp > 0) {
      break;  // passed where candidate would sort - not present in this checkpoint bracket.
    }
    if (nextOffset <= offset) {
      break;
    }
    offset = nextOffset;
  }
  return false;
}

bool StarDictLookup::lookupViaLinearScan(const std::string& candidateLower, uint64_t& outDictOffset,
                                         uint32_t& outDictSize) {
  uint32_t offset = 0;
  while (offset < idxFileSize_) {
    std::string entryWord;
    uint64_t dictOffset = 0;
    uint32_t dictSize = 0, nextOffset = 0;
    if (!readIdxEntryAt(offset, entryWord, dictOffset, dictSize, nextOffset)) {
      break;
    }
    if (toLowerCopy(entryWord) == candidateLower) {
      outDictOffset = dictOffset;
      outDictSize = dictSize;
      return true;
    }
    if (nextOffset <= offset) {
      break;
    }
    offset = nextOffset;
  }
  return false;
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

  uint64_t dictOffset = 0;
  uint32_t dictSize = 0;
  bool found = false;

  const std::string lowerCleaned = toLowerCopy(cleaned);

  // Word forms to try, in priority order: the word as typed/cased, then (if the word carries an
  // inflectional suffix like -s/-ed/-ing/'s) heuristic base forms, so "running"/"books"/"jumped"
  // can still resolve to "run"/"book"/"jump" when the inflected form isn't its own dictionary entry.
  std::vector<std::string> candidates = {cleaned, lowerCleaned, toTitleCaseCopy(cleaned)};
  for (const std::string& stem : stemCandidates(lowerCleaned)) {
    candidates.push_back(stem);
    candidates.push_back(toTitleCaseCopy(stem));
  }

  const unsigned long t0 = millis();
  std::string hitCandidate;
  for (const std::string& candidate : candidates) {
    if (lookupViaCheckpoints(candidate, dictOffset, dictSize)) {
      found = true;
      hitCandidate = candidate;
      Serial.printf("[%lu] [DICT] lookup('%s'): fast path hit on candidate='%s' (%lums)\n", millis(),
                    queryWord.c_str(), candidate.c_str(), millis() - t0);
      break;
    }
  }

  if (!found) {
    Serial.printf("[%lu] [DICT] lookup('%s'): fast path missed (%lums), falling back to linear scan over %u "
                  "idx bytes\n",
                  millis(), queryWord.c_str(), millis() - t0, idxFileSize_);
    const unsigned long t1 = millis();
    // The fast path assumes .idx is sorted in plain byte order (the documented StarDict
    // convention). Many third-party-generated dictionaries sort case-insensitively instead, which
    // silently breaks the checkpoint binary search above for every lookup. Fall back to a full
    // sequential scan, which is correct regardless of the actual on-disk order. Try the same
    // as-typed-then-stemmed candidate list here too, so the stemming fallback isn't lost for
    // dictionaries that only work via linear scan.
    std::vector<std::string> linearCandidates = {lowerCleaned};
    for (const std::string& stem : stemCandidates(lowerCleaned)) {
      linearCandidates.push_back(stem);
    }
    for (const std::string& candidate : linearCandidates) {
      if (lookupViaLinearScan(candidate, dictOffset, dictSize)) {
        found = true;
        hitCandidate = candidate;
        break;
      }
    }
    Serial.printf("[%lu] [DICT] lookup('%s'): linear scan %s (%lums)\n", millis(), queryWord.c_str(),
                  found ? "hit" : "miss", millis() - t1);
  }

  if (!found) {
    return false;
  }

  Serial.printf("[%lu] [DICT] lookup('%s'): matched '%s', dictOffset=%llu dictSize=%u\n", millis(),
                queryWord.c_str(), hitCandidate.c_str(), static_cast<unsigned long long>(dictOffset), dictSize);

  if (dictSize == 0 || !dictFile_.seekSet(dictOffset)) {
    Serial.printf("[%lu] [DICT] lookup('%s'): dictSize==0 or seekSet(%llu) failed\n", millis(), queryWord.c_str(),
                  static_cast<unsigned long long>(dictOffset));
    return false;
  }
  const uint32_t readSize = std::min(dictSize, kMaxDefinitionBytes);
  if (outTruncated) {
    *outTruncated = readSize < dictSize;
  }
  outDefinition.resize(readSize);
  const int readN = dictFile_.read(&outDefinition[0], readSize);
  if (readN != static_cast<int>(readSize)) {
    Serial.printf("[%lu] [DICT] lookup('%s'): read %d of %u expected bytes\n", millis(), queryWord.c_str(), readN,
                  readSize);
    return false;
  }
  return true;
}
