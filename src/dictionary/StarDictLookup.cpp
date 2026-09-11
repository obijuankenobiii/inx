#include "StarDictLookup.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include <esp_task_wdt.h>

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

bool isWordByte(const unsigned char c) {
  return c >= 0x80 || std::isalnum(c) != 0 || c == '\'' || c == '-';
}

/** UTF-8 General Punctuation U+2000–U+206F (curly quotes, dashes) is E2 80/81 xx. */
bool isGeneralPunctuationAt(const unsigned char* b, const size_t i, const size_t end) {
  return i + 2 < end && b[i] == 0xE2 && (b[i + 1] == 0x80 || b[i + 1] == 0x81);
}

/** æ/œ and vowel macrons so a page word like iustitiæ matches iustitiae / iustitia in the index. */
std::string foldLatinOrthography(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 4);
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const unsigned char d = (i + 1 < s.size()) ? static_cast<unsigned char>(s[i + 1]) : 0;
    if (c == 0xC3 && i + 1 < s.size()) {
      if (d == 0xA6 || d == 0x86) {  // æ Æ
        out += "ae";
        i += 2;
        continue;
      }
    }
    if (c == 0xC5 && i + 1 < s.size()) {
      if (d == 0x93 || d == 0x92) {  // œ Œ
        out += "oe";
        i += 2;
        continue;
      }
      if (d == 0x8D || d == 0x8C) {  // ō Ō
        out.push_back('o');
        i += 2;
        continue;
      }
      if (d == 0xAB || d == 0xAA) {  // ū Ū
        out.push_back('u');
        i += 2;
        continue;
      }
    }
    if (c == 0xC4 && i + 1 < s.size()) {
      if (d == 0x81 || d == 0x80) {  // ā Ā
        out.push_back('a');
        i += 2;
        continue;
      }
      if (d == 0x93 || d == 0x92) {  // ē Ē
        out.push_back('e');
        i += 2;
        continue;
      }
      if (d == 0xAB || d == 0xAA) {  // ī Ī
        out.push_back('i');
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
    ++i;
  }
  return out;
}

/** l'/d'/… and curly-quote elision. Punctuation, not morphology — the remainder is a different
 *  .idx region than the contracted form. */
std::string stripClitic(const std::string& word) {
  auto starts = [&](const char* prefix) {
    const size_t n = std::strlen(prefix);
    return word.size() > n + 1 && word.compare(0, n, prefix) == 0 &&
           std::isalpha(static_cast<unsigned char>(word[n])) != 0;
  };
  static const char* kAscii[] = {"l'", "d'", "n'", "m'", "t'", "s'", "c'", "j'", "qu'"};
  for (const char* prefix : kAscii) {
    if (starts(prefix)) {
      return word.substr(std::strlen(prefix));
    }
  }
  constexpr const char* kRightQuote = "\xE2\x80\x99";  // ’
  constexpr const char* kLeftQuote = "\xE2\x80\x98";   // ‘
  auto afterUtf8Quote = [&](const char* quote) -> std::string {
    const size_t qn = std::strlen(quote);
    if (word.size() > 1 + qn && word.compare(1, qn, quote) == 0 &&
        std::isalpha(static_cast<unsigned char>(word[0])) != 0 &&
        std::isalpha(static_cast<unsigned char>(word[1 + qn])) != 0) {
      return word.substr(1 + qn);
    }
    if (word.size() > 2 + qn && word.compare(0, 2, "qu") == 0 && word.compare(2, qn, quote) == 0 &&
        std::isalpha(static_cast<unsigned char>(word[2 + qn])) != 0) {
      return word.substr(2 + qn);
    }
    return "";
  };
  const std::string afterRight = afterUtf8Quote(kRightQuote);
  if (!afterRight.empty()) {
    return afterRight;
  }
  const std::string afterLeft = afterUtf8Quote(kLeftQuote);
  if (!afterLeft.empty()) {
    return afterLeft;
  }
  size_t cut = std::string::npos;
  for (size_t i = 0; i < word.size(); ++i) {
    if (word[i] == '\'') {
      cut = i + 1;
    } else if (i + 2 < word.size() && static_cast<unsigned char>(word[i]) == 0xE2 &&
               static_cast<unsigned char>(word[i + 1]) == 0x80 &&
               (static_cast<unsigned char>(word[i + 2]) == 0x98 ||
                static_cast<unsigned char>(word[i + 2]) == 0x99)) {
      cut = i + 3;
    } else if (i + 1 < word.size() && static_cast<unsigned char>(word[i]) == 0xC2 &&
               static_cast<unsigned char>(word[i + 1]) == 0xB4) {
      cut = i + 2;
    }
  }
  if (cut != std::string::npos && cut < word.size() &&
      std::isalpha(static_cast<unsigned char>(word[cut])) != 0) {
    return word.substr(cut);
  }
  return "";
}

/** Same English endings CrossPoint 1.5 uses. French/Latin inflections are resolved from the
 *  sorted .idx neighborhood instead of a suffix table that can never be complete. */
void addEnglishStems(const std::string& form, std::vector<std::string>& out) {
  auto add = [&](const std::string& s) {
    if (s.size() >= 2 && std::find(out.begin(), out.end(), s) == out.end()) {
      out.push_back(s);
    }
  };
  const size_t n = form.size();
  if (n > 2 && form[n - 2] == '\'' && form[n - 1] == 's') {
    add(form.substr(0, n - 2));
  }
  if (n > 4 && form.compare(n - 4, 4, "\xE2\x80\x99s") == 0) {
    add(form.substr(0, n - 4));
  }
  if (n > 4 && form.compare(n - 3, 3, "ing") == 0) {
    const std::string base = form.substr(0, n - 3);
    add(base);
    add(base + "e");
    if (base.size() >= 3 && base[base.size() - 1] == base[base.size() - 2]) {
      add(base.substr(0, base.size() - 1));
    }
  }
  if (n > 3 && form.compare(n - 3, 3, "ied") == 0) {
    add(form.substr(0, n - 3) + "y");
  }
  if (n > 3 && form.compare(n - 2, 2, "ed") == 0) {
    const std::string base = form.substr(0, n - 2);
    add(base);
    add(base + "e");
    if (base.size() >= 3 && base[base.size() - 1] == base[base.size() - 2]) {
      add(base.substr(0, base.size() - 1));
    }
  }
  if (n > 4 && form.compare(n - 3, 3, "ies") == 0) {
    add(form.substr(0, n - 3) + "y");
  }
  if (n > 3 && form.compare(n - 2, 2, "es") == 0) {
    add(form.substr(0, n - 2));
  }
  if (n > 2 && form[n - 1] == 's' && form[n - 2] != 's') {
    add(form.substr(0, n - 1));
  }
}

std::string mapLatinJToI(const std::string& lower) {
  std::string out = lower;
  for (char& c : out) {
    if (c == 'j') {
      c = 'i';
    }
  }
  return out;
}

/** Higher is better. Inflected forms share a long prefix with a short leftover on each side
 *  (iudicia/iudicium, omnia/omnis) or one is a prefix of the other (sonne/sonner). Derived
 *  compounds (iudicia ⊂ iudiciarius) lose to the same-length lemma. */
int inflectionScore(const std::string& query, const std::string& head) {
  if (query == head) {
    return 100000;
  }
  if (query.size() < 4 || head.size() < 4) {
    return -1;
  }
  const size_t n = std::min(query.size(), head.size());
  size_t lcp = 0;
  while (lcp < n && query[lcp] == head[lcp]) {
    ++lcp;
  }
  if (lcp < 3) {
    return -1;
  }
  const size_t minLen = std::min(query.size(), head.size());
  if (lcp * 3 < minLen * 2) {
    return -1;
  }
  const int qRest = static_cast<int>(query.size() - lcp);
  const int hRest = static_cast<int>(head.size() - lcp);
  const int lenDiff = std::abs(static_cast<int>(query.size()) - static_cast<int>(head.size()));
  const int lcpI = static_cast<int>(lcp);
  if (qRest > 0 && hRest > 0) {
    if (qRest > 4 || hRest > 4) {
      return -1;
    }
    return 2000 + lcpI * 10 - (qRest + hRest) * 20 - lenDiff * 50;
  }
  const int rest = std::max(qRest, hRest);
  if (rest == 0 || rest > 3) {
    return -1;
  }
  return 1000 + lcpI * 10 - rest * 20 - lenDiff * 50;
}

std::vector<std::string> queryVariants(const std::string& lower, const bool includeEnglishStems) {
  std::vector<std::string> out;
  auto add = [&](const std::string& s) {
    if (s.size() >= 2 && std::find(out.begin(), out.end(), s) == out.end()) {
      out.push_back(s);
    }
  };

  const std::string folded = foldLatinOrthography(lower);
  add(folded);
  const std::string asI = mapLatinJToI(folded);
  add(asI);

  const std::string declitic = stripClitic(folded);
  add(declitic);
  if (!declitic.empty()) {
    add(mapLatinJToI(declitic));
  }

  if (includeEnglishStems) {
    addEnglishStems(folded, out);
    if (!declitic.empty()) {
      addEnglishStems(declitic, out);
    }
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

bool writeLe32(FsFile& file, const uint32_t value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), 4) == 4;
}

bool readLe32(FsFile& file, uint32_t& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), 4) == 4;
}

// Same layout as CrossPoint 1.5 .qidx, plus a flags word (version 2) for sort-order.
constexpr uint32_t kQidxMagic = 0x58444951;  // "QIDX" little-endian
constexpr uint32_t kQidxVersion = 2;
constexpr uint32_t kQidxHeaderWords = 6;
constexpr uint32_t kDefinitionHeapHeadroomBytes = 8 * 1024;

}  // namespace

std::vector<std::string> StarDictLookup::alternateForms(const std::string& queryWord) {
  const std::string cleaned = stripSurroundingPunctuation(queryWord);
  return queryVariants(toLowerCopy(cleaned), true);
}

constexpr uint32_t StarDictLookup::kMaxDefinitionBytes;

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
  // swap, not .clear() — sampleOffsets_ can be a few thousand uint32s; .clear() would keep capacity.
  std::vector<uint32_t>().swap(sampleOffsets_);
  std::vector<DefCacheEntry>().swap(defCache_);
  std::string().swap(lastHitHeadword_);
  std::string().swap(folderPath_);
  std::string().swap(bookname_);
  std::string().swap(lang_);
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
    } else if (key == "lang") {
      lang_ = value.c_str();
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
  const std::string qidxPath = folderPath + "/" + kSampleIndexFileName;
  if (!loadSampleIndex(qidxPath) && !buildSampleIndex(qidxPath)) {
    Serial.printf("[%lu] [DICT] Could not load or build sample index for %s\n", millis(), folderPath.c_str());
    close();
    return false;
  }
  SdMan.remove((folderPath + "/.inx-stardict-cp").c_str());

  folderPath_ = folderPath;
  isOpen_ = true;
  Serial.printf("[%lu] [DICT] Opened '%s' (%u words, %u samples, %s offsets, sort=%s) in %lums\n", millis(),
                bookname_.c_str(), wordCount_, static_cast<unsigned>(sampleOffsets_.size()),
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

std::string StarDictLookup::stripSurroundingPunctuation(const std::string& word) {
  const auto* b = reinterpret_cast<const unsigned char*>(word.data());
  size_t start = 0;
  size_t end = word.size();
  while (start < end) {
    if (!isWordByte(b[start])) {
      ++start;
    } else if (isGeneralPunctuationAt(b, start, end)) {
      start += 3;
    } else {
      break;
    }
  }
  while (end > start) {
    if (!isWordByte(b[end - 1])) {
      --end;
    } else if (end - start >= 3 && isGeneralPunctuationAt(b, end - 3, end)) {
      end -= 3;
    } else {
      break;
    }
  }
  return word.substr(start, end - start);
}

bool StarDictLookup::needsIndexBuild(const std::string& folderPath) {
  const std::string qidxPath = folderPath + "/" + kSampleIndexFileName;
  FsFile file;
  if (!SdMan.openFileForRead("DICT", qidxPath, file)) {
    return true;
  }
  uint32_t magic = 0;
  uint32_t version = 0;
  uint32_t interval = 0;
  const bool ok = readLe32(file, magic) && readLe32(file, version) && readLe32(file, interval) &&
                  magic == kQidxMagic && version == kQidxVersion && interval == kSampleInterval;
  file.close();
  return !ok;
}

bool StarDictLookup::loadSampleIndex(const std::string& qidxPath) {
  sampleOffsets_.clear();
  FsFile file;
  if (!SdMan.openFileForRead("DICT", qidxPath, file)) {
    return false;
  }
  uint32_t magic = 0;
  uint32_t version = 0;
  uint32_t interval = 0;
  uint32_t sampleCount = 0;
  uint32_t cachedIdxSize = 0;
  uint32_t flags = 0;
  if (!readLe32(file, magic) || !readLe32(file, version) || !readLe32(file, interval) ||
      !readLe32(file, sampleCount) || !readLe32(file, cachedIdxSize) || !readLe32(file, flags) ||
      magic != kQidxMagic || version != kQidxVersion || interval != kSampleInterval ||
      cachedIdxSize != idxFileSize_ || sampleCount == 0 ||
      sampleCount > std::max<uint32_t>(1u, idxFileSize_ / 9u / kSampleInterval + 2u)) {
    file.close();
    return false;
  }

  std::vector<uint32_t> loaded(sampleCount);
  if (file.read(reinterpret_cast<uint8_t*>(loaded.data()), sampleCount * 4) !=
      static_cast<int>(sampleCount * 4)) {
    file.close();
    return false;
  }
  file.close();
  if (loaded[0] != 0) {
    return false;
  }

  sampleOffsets_ = std::move(loaded);
  caseInsensitiveSort_ = (flags & 1u) != 0;
  Serial.printf("[%lu] [DICT] Loaded %u qidx samples (%s sort)\n", millis(),
                static_cast<unsigned>(sampleOffsets_.size()),
                caseInsensitiveSort_ ? "case-insensitive" : "byte-order");
  return true;
}

bool StarDictLookup::buildSampleIndex(const std::string& qidxPath) {
  sampleOffsets_.clear();
  FsFile out;
  if (!SdMan.openFileForWrite("DICT", qidxPath, out)) {
    return false;
  }

  const uint32_t placeholder[kQidxHeaderWords] = {};
  bool ok = out.write(reinterpret_cast<const uint8_t*>(placeholder), sizeof(placeholder)) == sizeof(placeholder) &&
            writeLe32(out, 0);
  uint32_t sampleCount = ok ? 1 : 0;

  IdxCursor cur(idxFile_, idxFileSize_);
  if (!ok || !cur.seek(0)) {
    out.close();
    SdMan.remove(qidxPath.c_str());
    return false;
  }

  uint32_t count = 0;
  std::string prevText;
  std::string prevLower;
  bool havePrev = false;
  uint32_t byteOrderViolations = 0;
  uint32_t caseFoldViolations = 0;

  while (ok && cur.position() < idxFileSize_) {
    const uint32_t entryOffset = cur.position();
    std::string entryText;
    uint64_t dictOffset = 0;
    uint32_t dictSize = 0;
    if ((count & 0xFF) == 0) {
      esp_task_wdt_reset();
    }
    if (!readIdxEntry(cur, entryText, dictOffset, dictSize)) {
      Serial.printf("[%lu] [DICT] buildSampleIndex: read failed at offset=%u after %u entries\n", millis(),
                    entryOffset, count);
      break;
    }
    if (cur.position() <= entryOffset) {
      Serial.printf("[%lu] [DICT] buildSampleIndex: non-advancing entry at offset=%u\n", millis(), entryOffset);
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
    prevText = std::move(entryText);
    prevLower = std::move(entryLower);
    havePrev = true;
    ++count;
    if (count % kSampleInterval == 0 && cur.position() < idxFileSize_) {
      ok = writeLe32(out, cur.position());
      ++sampleCount;
    }
  }

  const uint32_t violationBudget = std::max<uint32_t>(4, count / 5000);
  caseInsensitiveSort_ = (caseFoldViolations <= violationBudget) && (byteOrderViolations > violationBudget);
  const uint32_t flags = caseInsensitiveSort_ ? 1u : 0u;
  const uint32_t header[kQidxHeaderWords] = {kQidxMagic, kQidxVersion, kSampleInterval, sampleCount, idxFileSize_,
                                             flags};
  ok = ok && sampleCount > 0 && out.seekSet(0) &&
       out.write(reinterpret_cast<const uint8_t*>(header), sizeof(header)) == sizeof(header);
  out.close();
  if (!ok) {
    Serial.printf("[%lu] [DICT] Index build failed, removing %s\n", millis(), qidxPath.c_str());
    SdMan.remove(qidxPath.c_str());
    return false;
  }

  Serial.printf("[%lu] [DICT] Indexed %u entries (%u samples, sort=%s)\n", millis(), count, sampleCount,
                caseInsensitiveSort_ ? "case-insensitive" : "byte-order");
  return loadSampleIndex(qidxPath);
}

int StarDictLookup::compareForSearch(const std::string& a, const std::string& aLower, const std::string& b,
                                     const std::string& bLower) const {
  if (caseInsensitiveSort_) {
    return aLower.compare(bLower);
  }
  return a.compare(b);
}

void StarDictLookup::collectSameWordHits(IdxCursor& cur, const std::string& wordLower,
                                         std::vector<std::pair<uint64_t, uint32_t>>& outHits) {
  static constexpr size_t kMaxSameWordHits = 6;
  while (outHits.size() < kMaxSameWordHits && cur.position() < idxFileSize_) {
    std::string entryWord;
    uint64_t dictOffset = 0;
    uint32_t dictSize = 0;
    if (!readIdxEntry(cur, entryWord, dictOffset, dictSize)) {
      break;
    }
    if (toLowerCopy(entryWord) != wordLower) {
      break;
    }
    outHits.emplace_back(dictOffset, dictSize);
  }
}

bool StarDictLookup::lookupViaSamples(const std::string& candidate, const std::string& candidateLower,
                                      std::vector<std::pair<uint64_t, uint32_t>>& outHits, std::string& outMatchedWord,
                                      const bool allowFuzzy) {
  outHits.clear();
  outMatchedWord.clear();
  if (sampleOffsets_.empty()) {
    return false;
  }

  IdxCursor cur(idxFile_, idxFileSize_);
  size_t lo = 0;
  size_t hi = sampleOffsets_.size() - 1;
  while (lo < hi) {
    const size_t mid = (lo + hi + 1) / 2;
    if (!cur.seek(sampleOffsets_[mid])) {
      lo = 0;
      break;
    }
    std::string sampleWord;
    uint64_t unusedOffset = 0;
    uint32_t unusedSize = 0;
    if (!readIdxEntry(cur, sampleWord, unusedOffset, unusedSize)) {
      lo = 0;
      break;
    }
    const std::string sampleLower = toLowerCopy(sampleWord);
    if (compareForSearch(sampleWord, sampleLower, candidate, candidateLower) <= 0) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }

  const uint32_t scanStart = sampleOffsets_[lo];
  const uint32_t windowEnd = (lo + 1 < sampleOffsets_.size()) ? sampleOffsets_[lo + 1] : idxFileSize_;
  // Inflected forms sit next to the lemma; if the insertion point is on a sample boundary the
  // lemma may be the first word of the next 256-entry window, so fuzzy scans one extra window.
  const uint32_t fuzzyEnd =
      allowFuzzy ? ((lo + 2 < sampleOffsets_.size()) ? sampleOffsets_[lo + 2] : idxFileSize_) : windowEnd;
  if (!cur.seek(scanStart)) {
    return false;
  }

  const std::string candidateFolded = foldLatinOrthography(candidateLower);
  int bestScore = -1;
  std::string bestWord;
  uint64_t bestOffset = 0;
  uint32_t bestSize = 0;
  uint32_t bestNextPos = 0;
  uint32_t scanned = 0;

  while (cur.position() < fuzzyEnd) {
    if ((scanned & 0x3F) == 0) {
      esp_task_wdt_reset();
    }
    ++scanned;

    std::string entryWord;
    uint64_t dictOffset = 0;
    uint32_t dictSize = 0;
    if (!readIdxEntry(cur, entryWord, dictOffset, dictSize)) {
      break;
    }
    const std::string entryLower = toLowerCopy(entryWord);
    const int cmp = compareForSearch(entryWord, entryLower, candidate, candidateLower);
    const std::string entryFolded = foldLatinOrthography(entryLower);
    if (cmp == 0 || entryFolded == candidateFolded) {
      outHits.emplace_back(dictOffset, dictSize);
      collectSameWordHits(cur, entryLower, outHits);
      outMatchedWord = entryWord;
      return true;
    }
    if (!allowFuzzy) {
      if (cmp > 0 || cur.position() >= windowEnd) {
        break;
      }
      continue;
    }
    const int score = inflectionScore(candidateFolded, entryFolded);
    if (score > bestScore) {
      bestScore = score;
      bestWord = entryWord;
      bestOffset = dictOffset;
      bestSize = dictSize;
      bestNextPos = cur.position();
    }
  }

  if (!allowFuzzy || bestScore < 0) {
    return false;
  }

  outHits.emplace_back(bestOffset, bestSize);
  if (cur.seek(bestNextPos)) {
    collectSameWordHits(cur, toLowerCopy(bestWord), outHits);
  }
  outMatchedWord = bestWord;
  return true;
}

bool StarDictLookup::lookupViaLinearScan(const std::vector<std::string>& candidatesLower, std::string& outHitLower,
                                         std::vector<std::pair<uint64_t, uint32_t>>& outHits) {
  outHits.clear();
  if (candidatesLower.empty()) {
    return false;
  }

  int bestPriority = -1;
  uint64_t bestOffset = 0;
  uint32_t bestSize = 0;
  std::string bestLower;

  IdxCursor cur(idxFile_, idxFileSize_);
  if (!cur.seek(0)) {
    return false;
  }

  uint32_t scanned = 0;
  while (cur.position() < idxFileSize_) {
    if ((scanned & 0xFF) == 0) {
      esp_task_wdt_reset();
    }
    ++scanned;

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
          if (bestPriority == 0) {
            outHitLower = bestLower;
            outHits.emplace_back(bestOffset, bestSize);
            collectSameWordHits(cur, bestLower, outHits);
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
  outHits.emplace_back(bestOffset, bestSize);
  return true;
}

bool StarDictLookup::readDefinition(const uint64_t dictOffset, const uint32_t dictSize, std::string& outDefinition,
                                    bool* outTruncated) {
  if (dictSize == 0 || !dictFile_.seekSet(dictOffset)) {
    return false;
  }
  const uint32_t readSize = std::min(dictSize, kMaxDefinitionBytes);
  if (ESP.getMaxAllocHeap() < readSize + kDefinitionHeapHeadroomBytes) {
    Serial.printf("[%lu] [DICT] Low heap for %u byte definition (maxAlloc=%u)\n", millis(), readSize,
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));
    return false;
  }
  if (outTruncated) {
    *outTruncated = readSize < dictSize;
  }
  outDefinition.resize(readSize);
  const int readN = dictFile_.read(&outDefinition[0], readSize);
  return readN == static_cast<int>(readSize);
}

bool StarDictLookup::readConcatenatedDefinitions(const std::vector<std::pair<uint64_t, uint32_t>>& hits,
                                                 std::string& outDefinition, bool* outTruncated) {
  outDefinition.clear();
  if (hits.empty()) {
    return false;
  }

  std::vector<uint32_t> take;
  take.reserve(hits.size());
  uint32_t total = 0;
  for (size_t i = 0; i < hits.size(); ++i) {
    const uint32_t size = hits[i].second;
    if (size == 0) {
      continue;
    }
    if (total + size <= kMaxDefinitionBytes) {
      take.push_back(size);
      total += size;
      continue;
    }
    if (take.empty()) {
      take.push_back(kMaxDefinitionBytes);
      total = kMaxDefinitionBytes;
    }
    break;
  }
  if (take.empty()) {
    return false;
  }

  if (ESP.getMaxAllocHeap() < total + kDefinitionHeapHeadroomBytes) {
    return readDefinition(hits[0].first, hits[0].second, outDefinition, outTruncated);
  }

  outDefinition.reserve(total);
  for (size_t i = 0; i < take.size(); ++i) {
    if (!dictFile_.seekSet(hits[i].first)) {
      return false;
    }
    const size_t at = outDefinition.size();
    outDefinition.resize(at + take[i]);
    if (dictFile_.read(&outDefinition[at], take[i]) != static_cast<int>(take[i])) {
      return false;
    }
  }
  if (outTruncated) {
    *outTruncated = take.size() < hits.size() || take[0] < hits[0].second;
  }
  return true;
}

bool StarDictLookup::cacheGet(const std::string& keyLower, std::string& outDefinition, bool* outTruncated) {
  for (size_t i = 0; i < defCache_.size(); ++i) {
    if (defCache_[i].keyLower == keyLower) {
      outDefinition = defCache_[i].definition;
      lastHitHeadword_ = defCache_[i].hitHeadword;
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

void StarDictLookup::cachePut(const std::string& keyLower, const std::string& definition, const std::string& hitHeadword,
                             const bool truncated) {
  for (size_t i = 0; i < defCache_.size(); ++i) {
    if (defCache_[i].keyLower == keyLower) {
      defCache_[i].definition = definition;
      defCache_[i].hitHeadword = hitHeadword;
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
  defCache_.insert(defCache_.begin(), DefCacheEntry{keyLower, definition, hitHeadword, truncated});
}

bool StarDictLookup::lookup(const std::string& queryWord, std::string& outDefinition, bool* outTruncated) {
  return lookupInternal(queryWord, outDefinition, outTruncated, true);
}

bool StarDictLookup::lookupExact(const std::string& queryWord, std::string& outDefinition, bool* outTruncated) {
  return lookupInternal(queryWord, outDefinition, outTruncated, false);
}

bool StarDictLookup::lookupInternal(const std::string& queryWord, std::string& outDefinition, bool* outTruncated,
                                    const bool allowStem) {
  if (!isOpen_) {
    Serial.printf("[%lu] [DICT] lookup('%s'): dictionary not open\n", millis(), queryWord.c_str());
    lastHitHeadword_.clear();
    return false;
  }

  const std::string cleaned = stripSurroundingPunctuation(queryWord);
  if (cleaned.empty()) {
    Serial.printf("[%lu] [DICT] lookup('%s'): empty after stripSurroundingPunctuation\n", millis(),
                  queryWord.c_str());
    lastHitHeadword_.clear();
    return false;
  }

  const std::string lowerCleaned = toLowerCopy(cleaned);

  // Cache is keyed by the cleaned lowercased query (not the stem that eventually hit) so the same
  // on-screen word always resolves instantly on re-lookup.
  if (cacheGet(lowerCleaned, outDefinition, outTruncated)) {
    if (allowStem) {
      Serial.printf("[%lu] [DICT] lookup('%s'): cache hit\n", millis(), queryWord.c_str());
      return true;
    }
    const std::string hitLower = toLowerCopy(lastHitHeadword_);
    if (hitLower == lowerCleaned || hitLower == toLowerCopy(stripSurroundingPunctuation(lastHitHeadword_))) {
      Serial.printf("[%lu] [DICT] lookupExact('%s'): cache hit\n", millis(), queryWord.c_str());
      return true;
    }
  }

  // Word forms to try, in priority order: as typed, lower, Title; then orthography / elision /
  // English stems. French and Latin inflections are not guessed here — they are resolved from
  // nearby .idx entries after these exact probes miss.
  std::vector<std::string> candidates;
  candidates.reserve(16);
  pushUnique(candidates, cleaned);
  pushUnique(candidates, lowerCleaned);
  pushUnique(candidates, toTitleCaseCopy(cleaned));
  for (const std::string& variant : queryVariants(lowerCleaned, allowStem)) {
    pushUnique(candidates, variant);
    pushUnique(candidates, toTitleCaseCopy(variant));
  }

  const unsigned long t0 = millis();
  std::vector<std::pair<uint64_t, uint32_t>> hits;
  bool found = false;
  std::string hitCandidate;
  const bool detectedCi = caseInsensitiveSort_;

  auto tryExact = [&]() {
    for (const std::string& candidate : candidates) {
      const std::string candidateLower = toLowerCopy(candidate);
      std::string matched;
      if (lookupViaSamples(candidate, candidateLower, hits, matched, false)) {
        found = true;
        hitCandidate = matched.empty() ? candidate : matched;
        return;
      }
    }
  };

  auto tryFuzzy = [&]() {
    std::vector<std::string> fuzzyQueries;
    fuzzyQueries.reserve(4);
    pushUnique(fuzzyQueries, cleaned);
    pushUnique(fuzzyQueries, lowerCleaned);
    const std::string folded = foldLatinOrthography(lowerCleaned);
    pushUnique(fuzzyQueries, folded);
    const std::string declitic = stripClitic(folded);
    if (!declitic.empty()) {
      pushUnique(fuzzyQueries, declitic);
    }
    for (const std::string& candidate : fuzzyQueries) {
      const std::string candidateLower = toLowerCopy(candidate);
      std::string matched;
      if (lookupViaSamples(candidate, candidateLower, hits, matched, true)) {
        found = true;
        hitCandidate = matched.empty() ? candidate : matched;
        return;
      }
    }
  };

  tryExact();
  if (!found && allowStem) {
    tryFuzzy();
  }
  if (!found) {
    caseInsensitiveSort_ = !detectedCi;
    tryExact();
    if (!found && allowStem) {
      tryFuzzy();
    }
    if (found) {
      Serial.printf("[%lu] [DICT] lookup('%s'): hit after flipping sort order to %s (%lums)\n", millis(),
                    queryWord.c_str(), caseInsensitiveSort_ ? "case-insensitive" : "byte-order", millis() - t0);
    } else {
      caseInsensitiveSort_ = detectedCi;
    }
  } else {
    Serial.printf("[%lu] [DICT] lookup('%s'): fast path hit on candidate='%s' (%lums)\n", millis(),
                  queryWord.c_str(), hitCandidate.c_str(), millis() - t0);
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
    if (lookupViaLinearScan(linearCandidates, hitLower, hits)) {
      found = true;
      hitCandidate = hitLower;
    }
    Serial.printf("[%lu] [DICT] lookup('%s'): linear scan %s (%lums)\n", millis(), queryWord.c_str(),
                  found ? "hit" : "miss", millis() - t1);
  }

  if (!found || hits.empty()) {
    Serial.printf("[%lu] [DICT] lookup('%s'): not in index (%lums)\n", millis(), queryWord.c_str(), millis() - t0);
    lastHitHeadword_.clear();
    return false;
  }

  Serial.printf("[%lu] [DICT] lookup('%s'): matched '%s', %u idx hit(s), dictOffset=%llu dictSize=%u\n", millis(),
                queryWord.c_str(), hitCandidate.c_str(), static_cast<unsigned>(hits.size()),
                static_cast<unsigned long long>(hits[0].first), hits[0].second);

  bool truncated = false;
  if (!readConcatenatedDefinitions(hits, outDefinition, &truncated)) {
    Serial.printf("[%lu] [DICT] lookup('%s'): definition read failed\n", millis(), queryWord.c_str());
    lastHitHeadword_.clear();
    return false;
  }
  if (outTruncated) {
    *outTruncated = truncated;
  }
  lastHitHeadword_ = hitCandidate;
  cachePut(lowerCleaned, outDefinition, lastHitHeadword_, truncated);
  Serial.printf("[%lu] [DICT] lookup('%s'): total %lums (def %u bytes%s)\n", millis(), queryWord.c_str(),
                millis() - t0, static_cast<unsigned>(outDefinition.size()), truncated ? ", truncated" : "");
  return true;
}
