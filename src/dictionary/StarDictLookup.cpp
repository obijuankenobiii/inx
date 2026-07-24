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
  checkpoints_.clear();
  bookname_.clear();
  sameTypeSequence_.clear();
  wordCount_ = 0;
  idxFileSize_ = 0;
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
    }
  }

  return true;
}

bool StarDictLookup::open(const std::string& folderPath) {
  close();

  std::string ifoPath, idxPath, dictPath;
  for (const String& name : SdMan.listFiles(folderPath.c_str())) {
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
    Serial.printf("[%lu] [DICT] Missing .ifo/.idx/.dict under %s\n", millis(), folderPath.c_str());
    return false;
  }

  if (!parseIfo(ifoPath)) {
    Serial.printf("[%lu] [DICT] Could not parse %s\n", millis(), ifoPath.c_str());
    return false;
  }

  if (!SdMan.openFileForRead("DICT", idxPath, idxFile_) || !SdMan.openFileForRead("DICT", dictPath, dictFile_)) {
    Serial.printf("[%lu] [DICT] Could not open .idx/.dict under %s\n", millis(), folderPath.c_str());
    close();
    return false;
  }

  if (idxFileSize_ == 0) {
    idxFileSize_ = static_cast<uint32_t>(idxFile_.fileSize());
  }

  if (!buildCheckpoints()) {
    Serial.printf("[%lu] [DICT] Could not build index checkpoints for %s\n", millis(), folderPath.c_str());
    close();
    return false;
  }

  isOpen_ = true;
  Serial.printf("[%lu] [DICT] Opened '%s' (%u words, %u checkpoints)\n", millis(), bookname_.c_str(), wordCount_,
                static_cast<unsigned>(checkpoints_.size()));
  return true;
}

bool StarDictLookup::readIdxEntryAt(const uint32_t idxOffset, std::string& outEntryText, uint32_t& outDictOffset,
                                    uint32_t& outDictSize, uint32_t& outNextOffset) {
  if (!idxFile_.seekSet(idxOffset)) {
    return false;
  }
  if (!readCString(idxFile_, outEntryText)) {
    return false;
  }
  outDictOffset = readBigEndian32(idxFile_);
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
    uint32_t dictOffset = 0, dictSize = 0, nextOffset = 0;
    if (!readIdxEntryAt(offset, entryText, dictOffset, dictSize, nextOffset)) {
      break;
    }
    if (count % kCheckpointStride == 0) {
      checkpoints_.push_back(Checkpoint{offset, entryText});
    }
    ++count;
    if (nextOffset <= offset) {
      // Malformed/looping entry - stop rather than spin forever.
      break;
    }
    offset = nextOffset;
  }
  return !checkpoints_.empty();
}

bool StarDictLookup::lookup(const std::string& queryWord, std::string& outDefinition) {
  if (!isOpen_ || checkpoints_.empty()) {
    return false;
  }

  const std::string cleaned = stripPunctuation(queryWord);
  if (cleaned.empty()) {
    return false;
  }

  for (const std::string& candidate : {cleaned, toLowerCopy(cleaned), toTitleCaseCopy(cleaned)}) {
    // Binary search checkpoints_ for the last checkpoint whose word is <= candidate.
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
      continue;  // candidate sorts before the first checkpoint's word - not present under this casing.
    }
    const uint32_t scanStart = checkpoints_[lo - 1].idxOffset;
    const uint32_t scanEnd = (lo < checkpoints_.size()) ? checkpoints_[lo].idxOffset : idxFileSize_;

    uint32_t offset = scanStart;
    while (offset < scanEnd) {
      std::string entryWord;
      uint32_t dictOffset = 0, dictSize = 0, nextOffset = 0;
      if (!readIdxEntryAt(offset, entryWord, dictOffset, dictSize, nextOffset)) {
        break;
      }
      const int cmp = compareWord(entryWord, candidate);
      if (cmp == 0) {
        if (dictSize == 0 || !dictFile_.seekSet(dictOffset)) {
          return false;
        }
        outDefinition.resize(dictSize);
        const int readN = dictFile_.read(&outDefinition[0], dictSize);
        if (readN != static_cast<int>(dictSize)) {
          return false;
        }
        return true;
      }
      if (cmp > 0) {
        break;  // passed where candidate would be - not present under this casing.
      }
      if (nextOffset <= offset) {
        break;
      }
      offset = nextOffset;
    }
  }

  return false;
}
