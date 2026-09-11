#include "dictionary/DictionaryRegistry.h"

#include <SDCardManager.h>
#include <Serialization.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "util/StringUtils.h"

namespace {
constexpr char kOverridesPath[] = "/.system/dict_langs.bin";

std::string toLowerAscii(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string readIfoKey(const std::string& folderPath, const char* key) {
  FsFile dir = SdMan.open(folderPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return "";
  }
  std::string ifoPath;
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      char name[160] = {};
      file.getName(name, sizeof(name));
      if (name[0] != '.' && StringUtils::checkFileExtension(std::string(name), ".ifo")) {
        ifoPath = folderPath + "/" + name;
      }
    }
    file.close();
    if (!ifoPath.empty()) {
      break;
    }
  }
  dir.close();
  if (ifoPath.empty()) {
    return "";
  }

  const String contents = SdMan.readFile(ifoPath.c_str());
  if (contents.isEmpty()) {
    return "";
  }
  const std::string prefix = std::string(key) + "=";
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
    if (line.startsWith(prefix.c_str())) {
      String value = line.substring(static_cast<int>(prefix.size()));
      value.trim();
      return value.c_str();
    }
  }
  return "";
}

struct Override {
  std::string folder;
  std::string lang;
};

std::vector<Override> loadOverrides() {
  std::vector<Override> out;
  FsFile file;
  if (!SdMan.openFileForRead("DICT", kOverridesPath, file)) {
    return out;
  }
  uint8_t count = 0;
  serialization::readPod(file, count);
  for (uint8_t i = 0; i < count && i < 16; ++i) {
    Override row;
    serialization::readString(file, row.folder);
    serialization::readString(file, row.lang);
    if (!row.folder.empty()) {
      out.push_back(std::move(row));
    }
  }
  file.close();
  return out;
}

void saveOverrides(const std::vector<Override>& rows) {
  SdMan.mkdir("/.system");
  FsFile file;
  if (!SdMan.openFileForWrite("DICT", kOverridesPath, file)) {
    return;
  }
  const uint8_t count = static_cast<uint8_t>(std::min<size_t>(rows.size(), 16));
  serialization::writePod(file, count);
  for (uint8_t i = 0; i < count; ++i) {
    serialization::writeString(file, rows[i].folder);
    serialization::writeString(file, rows[i].lang);
  }
  file.close();
}
}  // namespace

const char* const DictionaryRegistry::kLangCycle[] = {"", "nl", "en", "fr", "la"};

std::string DictionaryRegistry::primaryLanguageTag(const std::string& bcp47) {
  std::string tag = toLowerAscii(bcp47);
  while (!tag.empty() && (tag.front() == ' ' || tag.front() == '"')) {
    tag.erase(tag.begin());
  }
  const size_t cut = tag.find_first_of("-_ .;/");
  if (cut != std::string::npos) {
    tag.resize(cut);
  }
  if (tag == "dut" || tag == "nld") {
    return "nl";
  }
  if (tag == "eng") {
    return "en";
  }
  if (tag == "fra" || tag == "fre") {
    return "fr";
  }
  if (tag == "lat" || tag == "la") {
    return "la";
  }
  return tag;
}

std::string DictionaryRegistry::inferLangFromName(const std::string& name) {
  const std::string lower = toLowerAscii(name);
  // Latin before Dutch: "latijn-nederlands" contains "neder".
  if (lower.find("latin") != std::string::npos || lower.find("latijn") != std::string::npos ||
      lower.find("la-") == 0 || lower == "la" || lower.find("la_") != std::string::npos) {
    return "la";
  }
  if (lower.find("dutch") != std::string::npos || lower.find("neder") != std::string::npos ||
      lower.find("nl-") == 0 || lower == "nl" || lower.find("nl_") != std::string::npos) {
    return "nl";
  }
  if (lower.find("french") != std::string::npos || lower.find("franc") != std::string::npos ||
      lower.find("fr-") == 0 || lower == "fr" || lower.find("fr_") != std::string::npos) {
    return "fr";
  }
  if (lower.find("english") != std::string::npos || lower.find("oxford") != std::string::npos ||
      lower.find("webster") != std::string::npos || lower.find("en-") == 0 || lower == "en" ||
      lower.find("en_") != std::string::npos) {
    return "en";
  }
  return "";
}

std::string DictionaryRegistry::langLabel(const std::string& lang) {
  if (lang == "nl") {
    return "Dutch";
  }
  if (lang == "en") {
    return "English";
  }
  if (lang == "fr") {
    return "French";
  }
  if (lang == "la") {
    return "Latin";
  }
  if (lang.empty()) {
    return "Auto";
  }
  return lang;
}

std::string DictionaryRegistry::displayLabel(const std::string& folder, const std::string& lang) {
  if (folderIsBilingual(folder)) {
    const std::string lower = toLowerAscii(folder);
    const std::string src = langLabel(primaryLanguageTag(lower.substr(0, 2)));
    if (src != "Auto") {
      return src;
    }
  }
  const std::string fromLang = langLabel(primaryLanguageTag(lang));
  if (!fromLang.empty() && fromLang != "Auto") {
    return fromLang;
  }
  return langLabel(inferLangFromName(folder));
}

std::string DictionaryRegistry::targetLangFromFolder(const std::string& folder) {
  const std::string lower = toLowerAscii(folder);
  if (folderIsBilingual(folder)) {
    return primaryLanguageTag(lower.substr(3, 2));
  }
  if (lower.find("neder") != std::string::npos || lower.find("dutch") != std::string::npos ||
      lower.find("-nl") != std::string::npos || lower.find("_nl") != std::string::npos) {
    return "nl";
  }
  return "";
}

bool DictionaryRegistry::folderLooksLikeDictionary(const std::string& folderPath) {
  FsFile dir = SdMan.open(folderPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return false;
  }
  bool hasIdx = false;
  bool hasDict = false;
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      char name[160] = {};
      file.getName(name, sizeof(name));
      if (name[0] == '.') {
        file.close();
        continue;
      }
      if (StringUtils::checkFileExtension(std::string(name), ".idx")) {
        hasIdx = true;
      } else if (StringUtils::checkFileExtension(std::string(name), ".dict")) {
        hasDict = true;
      }
    }
    file.close();
    if (hasIdx && hasDict) {
      break;
    }
  }
  dir.close();
  return hasIdx && hasDict;
}

std::string DictionaryRegistry::readIfoLang(const std::string& folderPath) {
  return primaryLanguageTag(readIfoKey(folderPath, "lang"));
}

std::string DictionaryRegistry::readIfoBookname(const std::string& folderPath) {
  return readIfoKey(folderPath, "bookname");
}

std::vector<DictionaryRegistry::Entry> DictionaryRegistry::scan() {
  std::vector<Entry> out;
  FsFile dir = SdMan.open(kDictionariesRoot);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return out;
  }
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) {
      char name[160] = {};
      file.getName(name, sizeof(name));
      const std::string folderPath = std::string(kDictionariesRoot) + "/" + name;
      if (folderLooksLikeDictionary(folderPath)) {
        Entry entry;
        entry.folder = name;
        const std::string override = overrideFor(name);
        if (!override.empty()) {
          entry.lang = override;
        } else {
          entry.lang = readIfoLang(folderPath);
          if (entry.lang.empty()) {
            entry.lang = inferLangFromName(name);
          }
        }
        entry.bookname = readIfoBookname(folderPath);
        entry.bilingual = folderIsBilingual(name);
        out.push_back(std::move(entry));
      }
    }
    file.close();
  }
  dir.close();
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.folder < b.folder; });
  return out;
}

namespace {
bool wordEquals(const std::string& word, const char* token) {
  return word == token;
}

bool wordInList(const std::string& word, const char* const* list, const int n) {
  for (int i = 0; i < n; ++i) {
    if (wordEquals(word, list[i])) {
      return true;
    }
  }
  return false;
}

bool containsUtf8(const std::string& word, const char* needle) {
  return word.find(needle) != std::string::npos;
}

void scoreToken(const std::string& raw, const int weight, int& nl, int& en, int& fr, int& la) {
  std::string word = toLowerAscii(raw);
  if (word.empty()) {
    return;
  }

  // Distinctive graphemes beat function-word overlap ("de"/"en"/"is" exist in multiple languages).
  if (containsUtf8(word, "ij") || containsUtf8(word, "\xC4\xB3")) {  // ij / ĳ
    nl += 5 * weight;
  }
  if (containsUtf8(word, "\xC3\xA7") || containsUtf8(word, "\xC5\x93") || containsUtf8(word, "\xC5\x92")) {  // ç œ Œ
    fr += 5 * weight;
  }
  static const char* kFrMarks[] = {"\xC3\xA9", "\xC3\xA8", "\xC3\xAA", "\xC3\xA0", "\xC3\xA2",
                                   "\xC3\xB9", "\xC3\xBB", "\xC3\xB4", "\xC3\xAE", "\xC3\xAF"};
  for (const char* mark : kFrMarks) {
    if (containsUtf8(word, mark)) {
      fr += 2 * weight;
      break;
    }
  }
  static const char* kLaMarks[] = {"\xC4\x81", "\xC4\x93", "\xC4\xAB", "\xC5\x8D", "\xC5\xAB",
                                   "\xC4\x80", "\xC4\x92", "\xC4\xAA", "\xC5\x8C", "\xC5\xAA"};  // āēīōū
  for (const char* mark : kLaMarks) {
    if (containsUtf8(word, mark)) {
      la += 5 * weight;
      break;
    }
  }
  if (word.size() >= 4) {
    const std::string tail4 = word.size() >= 4 ? word.substr(word.size() - 4) : word;
    if (tail4 == "ibus" || tail4 == "orum" || tail4 == "arum" || tail4 == "ntur") {
      la += 4 * weight;
    }
  }

  static const char* kEnHi[] = {"the",   "and",  "of",    "with", "that",   "this", "from", "which",
                                "would", "could", "should", "been", "were",  "they", "their", "have",
                                "what",  "when", "not",   "was",  "for",    "but",  "his",  "her"};
  static const char* kNlHi[] = {"het", "een", "niet", "ook", "nog",  "naar", "wordt", "geen",
                                "jij", "ik",  "mijn", "maar", "zijn", "voor", "bij",   "uit",
                                "als", "wel", "dit",  "deze", "haar", "hem",  "hij",   "wij"};
  static const char* kFrHi[] = {"les",  "des",  "une",  "dans", "sont", "cette", "aux",  "elle",
                                "vous", "avec", "qui",  "que",  "pas",  "pour",  "est",  "ces",
                                "mon",  "ma",   "mes",  "son",  "ses",  "leur",  "sur",  "plus"};
  static const char* kLaHi[] = {"sunt",  "erat",  "esse",  "tamen", "igitur", "enim",  "atque", "neque",
                                "quidem", "nobis", "vobis", "mihi",  "eius",   "eorum", "quibus", "apud",
                                "haec",  "quae",  "quod",  "sine",  "nam",    "ergo",  "sed",    "dum"};
  static const char* kShared[] = {"de", "en", "is", "la", "un", "a", "in", "to", "du", "et", "le"};

  if (wordInList(word, kEnHi, static_cast<int>(sizeof(kEnHi) / sizeof(kEnHi[0])))) {
    en += 3 * weight;
  }
  if (wordInList(word, kNlHi, static_cast<int>(sizeof(kNlHi) / sizeof(kNlHi[0])))) {
    nl += 3 * weight;
  }
  if (wordInList(word, kFrHi, static_cast<int>(sizeof(kFrHi) / sizeof(kFrHi[0])))) {
    fr += 3 * weight;
  }
  if (wordInList(word, kLaHi, static_cast<int>(sizeof(kLaHi) / sizeof(kLaHi[0])))) {
    la += 3 * weight;
  }
  if (wordInList(word, kShared, static_cast<int>(sizeof(kShared) / sizeof(kShared[0])))) {
    // Shared closed-class words are almost useless alone; keep a whisper so a French "le/la/les"
    // cluster can still beat English when accents are missing.
    fr += 1 * weight;
    nl += 1 * weight;
  }
}

std::string folderMatchingLang(const std::vector<DictionaryRegistry::Entry>& entries, const std::string& lang) {
  if (lang.empty()) {
    return "";
  }
  std::string bilingual;
  std::string mono;
  for (const DictionaryRegistry::Entry& entry : entries) {
    if (entry.lang != lang) {
      continue;
    }
    if (entry.bilingual) {
      if (bilingual.empty()) {
        bilingual = entry.folder;
      }
    } else if (mono.empty()) {
      mono = entry.folder;
    }
  }
  return !bilingual.empty() ? bilingual : mono;
}

std::string folderMatchingName(const std::vector<DictionaryRegistry::Entry>& entries, const std::string& folder) {
  if (folder.empty()) {
    return "";
  }
  for (const DictionaryRegistry::Entry& entry : entries) {
    if (entry.folder == folder) {
      return entry.folder;
    }
  }
  return "";
}
}  // namespace

std::string DictionaryRegistry::detectLanguage(const std::vector<std::string>& words, const size_t focusIndex) {
  int nl = 0;
  int en = 0;
  int fr = 0;
  int la = 0;
  for (size_t i = 0; i < words.size(); ++i) {
    const int weight = (i == focusIndex) ? 2 : 1;
    scoreToken(words[i], weight, nl, en, fr, la);
  }

  struct Scored {
    const char* lang;
    int score;
  };
  Scored ranked[] = {{"nl", nl}, {"en", en}, {"fr", fr}, {"la", la}};
  std::sort(ranked, ranked + 4, [](const Scored& a, const Scored& b) { return a.score > b.score; });
  // Need a real lead so mixed English prose with one French name doesn't flip the dictionary.
  if (ranked[0].score >= 4 && ranked[0].score >= ranked[1].score + 2) {
    return ranked[0].lang;
  }
  return "";
}

bool DictionaryRegistry::folderIsBilingual(const std::string& folder) {
  const std::string lower = toLowerAscii(folder);
  if (lower.size() < 5) {
    return false;
  }
  const char sep = lower[2];
  if (sep != '-' && sep != '_') {
    return false;
  }
  if (!std::isalpha(static_cast<unsigned char>(lower[0])) || !std::isalpha(static_cast<unsigned char>(lower[1])) ||
      !std::isalpha(static_cast<unsigned char>(lower[3])) || !std::isalpha(static_cast<unsigned char>(lower[4]))) {
    return false;
  }
  if (lower.size() > 5 && std::isalpha(static_cast<unsigned char>(lower[5]))) {
    return false;
  }
  return lower[0] != lower[3] || lower[1] != lower[4];
}

std::string DictionaryRegistry::folderForLanguage(const std::string& epubLanguage, const char* fallbackFolder) {
  return folderForLookup("", "", epubLanguage, fallbackFolder);
}

std::string DictionaryRegistry::folderForLookup(const std::string& detectedLang, const std::string& sessionFolder,
                                                const std::string& epubLanguage, const char* fallbackFolder) {
  const auto entries = scan();
  if (const std::string hit = folderMatchingLang(entries, primaryLanguageTag(detectedLang)); !hit.empty()) {
    return hit;
  }
  if (const std::string hit = folderMatchingName(entries, sessionFolder); !hit.empty()) {
    return hit;
  }
  if (const std::string hit = folderMatchingLang(entries, primaryLanguageTag(epubLanguage)); !hit.empty()) {
    return hit;
  }
  if (fallbackFolder && fallbackFolder[0] != '\0') {
    if (const std::string hit = folderMatchingName(entries, fallbackFolder); !hit.empty()) {
      return hit;
    }
  }
  if (!entries.empty()) {
    return entries.front().folder;
  }
  return fallbackFolder ? std::string(fallbackFolder) : std::string();
}

std::vector<std::string> DictionaryRegistry::foldersInFallbackOrder(const std::string& preferredFolder) {
  const auto entries = scan();
  std::vector<std::string> out;
  out.reserve(entries.size());
  if (!preferredFolder.empty()) {
    out.push_back(preferredFolder);
  }
  for (const Entry& entry : entries) {
    if (entry.folder != preferredFolder) {
      out.push_back(entry.folder);
    }
  }
  return out;
}

std::vector<std::string> DictionaryRegistry::foldersForAutoLookup(const std::string& preferredFolder) {
  const auto entries = scan();
  std::string source;
  bool preferredBilingual = false;
  for (const Entry& entry : entries) {
    if (entry.folder == preferredFolder) {
      source = entry.lang;
      preferredBilingual = entry.bilingual;
      break;
    }
  }
  if (source.empty()) {
    source = inferLangFromName(preferredFolder);
    preferredBilingual = folderIsBilingual(preferredFolder);
  }

  std::vector<std::string> bilingual;
  std::vector<std::string> mono;
  for (const Entry& entry : entries) {
    if (entry.folder == preferredFolder || entry.lang != source) {
      continue;
    }
    if (entry.bilingual) {
      bilingual.push_back(entry.folder);
    } else {
      mono.push_back(entry.folder);
    }
  }

  std::vector<std::string> out;
  out.reserve(entries.size());
  if (!preferredFolder.empty()) {
    out.push_back(preferredFolder);
  }
  if (preferredBilingual) {
    out.insert(out.end(), bilingual.begin(), bilingual.end());
    out.insert(out.end(), mono.begin(), mono.end());
  } else {
    out.insert(out.end(), mono.begin(), mono.end());
    out.insert(out.end(), bilingual.begin(), bilingual.end());
  }
  return out;
}

std::string DictionaryRegistry::overrideFor(const std::string& folder) {
  for (const Override& row : loadOverrides()) {
    if (row.folder == folder) {
      return row.lang;
    }
  }
  return "";
}

void DictionaryRegistry::setOverride(const std::string& folder, const std::string& lang) {
  auto rows = loadOverrides();
  bool found = false;
  for (Override& row : rows) {
    if (row.folder == folder) {
      row.lang = lang;
      found = true;
      break;
    }
  }
  if (!found && !lang.empty()) {
    rows.push_back(Override{folder, lang});
  }
  if (lang.empty()) {
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Override& row) { return row.folder == folder; }),
               rows.end());
  }
  saveOverrides(rows);
}

int DictionaryRegistry::langCycleIndex(const std::string& lang) {
  for (int i = 0; i < kLangCycleCount; ++i) {
    if (lang == kLangCycle[i]) {
      return i;
    }
  }
  return 0;
}
