/**
 * @file CssParser.cpp
 * @brief Definitions for CssParser.
 */

#include "CssParser.h"

#include <FsHelpers.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include "CssTrackedProperties.h"
#ifdef ARDUINO
#include <Esp.h>  // ESP.getFreeHeap() for the CSS heap-reserve guard
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace {

// Hard ceiling on stored rules (each holds only tracked properties, so it's small). The real bound at runtime
// is the heap-reserve guard in parse(); this just caps worst-case memory if heap is plentiful.
constexpr size_t kMaxCssRules = 1024;
constexpr uint32_t kCssParserCacheMagic = 0x43535042;  // "CSPB"
constexpr uint16_t kCssParserCacheVersion = 5;
constexpr uint8_t kCssPropertyInvalid = 0xFF;

uint8_t cssPropertyId(const std::string& name) {
  static const char* const kNames[] = {
      "background",       "background-color", "background-image", "border",       "border-bottom",
      "border-color",     "border-left",      "border-radius",    "border-right", "border-style",
      "border-top",       "border-width",     "display",          "font-size",    "font-style",
      "font-variant",     "font-variant-caps", "font-weight",      "height",       "initial-letter",
      "line-height",      "margin",           "margin-bottom",    "margin-left",  "margin-right",
      "margin-top",       "max-height",       "max-width",        "min-height",   "min-width",
      "padding",          "padding-bottom",   "padding-left",     "padding-right", "padding-top",
      "text-align",       "text-indent",      "vertical-align",   "width",        "float",
      "list-style",       "list-style-type",  "page-break-before", "page-break-after",
  };
  for (uint8_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i) {
    if (name == kNames[i]) {
      return i;
    }
  }
  return kCssPropertyInvalid;
}

const char* cssPropertyName(const uint8_t id) {
  static const char* const kNames[] = {
      "background",       "background-color", "background-image", "border",       "border-bottom",
      "border-color",     "border-left",      "border-radius",    "border-right", "border-style",
      "border-top",       "border-width",     "display",          "font-size",    "font-style",
      "font-variant",     "font-variant-caps", "font-weight",      "height",       "initial-letter",
      "line-height",      "margin",           "margin-bottom",    "margin-left",  "margin-right",
      "margin-top",       "max-height",       "max-width",        "min-height",   "min-width",
      "padding",          "padding-bottom",   "padding-left",     "padding-right", "padding-top",
      "text-align",       "text-indent",      "vertical-align",   "width",        "float",
      "list-style",       "list-style-type",  "page-break-before", "page-break-after",
  };
  return id < sizeof(kNames) / sizeof(kNames[0]) ? kNames[id] : "";
}

CssParser::CssProperties::iterator findProperty(CssParser::CssProperties& properties, const uint8_t id) {
  return std::find_if(properties.begin(), properties.end(),
                      [id](const CssParser::CssProperties::value_type& prop) { return prop.id == id; });
}

CssParser::CssProperties::const_iterator findProperty(const CssParser::CssProperties& properties, const uint8_t id) {
  return std::find_if(properties.begin(), properties.end(),
                      [id](const CssParser::CssProperties::value_type& prop) { return prop.id == id; });
}

bool hasProperty(const CssParser::CssProperties& properties, const std::string& name) {
  const uint8_t id = cssPropertyId(name);
  return id != kCssPropertyInvalid && findProperty(properties, id) != properties.end();
}

const std::string* propertyValue(const CssParser::CssProperties& properties, const std::string& name) {
  const uint8_t id = cssPropertyId(name);
  if (id == kCssPropertyInvalid) {
    return nullptr;
  }
  const auto it = findProperty(properties, id);
  return it == properties.end() ? nullptr : &it->value;
}

void setProperty(CssParser::CssProperties& properties, std::string name, std::string value) {
  const uint8_t id = cssPropertyId(name);
  if (id == kCssPropertyInvalid) {
    return;
  }
  const auto it = findProperty(properties, id);
  if (it != properties.end()) {
    it->value = std::move(value);
  } else {
    CssParser::CssProperty prop;
    prop.id = id;
    prop.value = std::move(value);
    properties.push_back(std::move(prop));
  }
}
constexpr uint16_t kMaxCssSourcePaths = 512;
constexpr uint16_t kMaxCssPropertiesPerRule = 64;
constexpr uint32_t kMaxCssCacheStringLen = 4096;

bool readBoundedString(FsFile& file, std::string& out) {
  uint32_t len = 0;
  serialization::readPod(file, len);
  if (len > kMaxCssCacheStringLen || len > file.available()) {
    return false;
  }
  out.resize(len);
  return len == 0 || file.read(&out[0], len) == static_cast<int>(len);
}

bool writeStringChecked(FsFile& file, const std::string& value) {
  if (value.size() > kMaxCssCacheStringLen) {
    return false;
  }
  serialization::writeString(file, value);
  return true;
}

bool isIdentCont(unsigned char c) { return std::isalnum(c) != 0 || c == '_' || c == '-'; }

void splitClassTokens(const std::string& classAttr, std::vector<std::string>& out) {
  out.clear();
  std::string cur;
  for (char c : classAttr) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) {
    out.push_back(cur);
  }
}

std::string trimCssWs(const std::string& str) {
  size_t start = 0;
  while (start < str.length() && isspace(static_cast<unsigned char>(str[start]))) {
    start++;
  }
  size_t end = str.length();
  while (end > start && isspace(static_cast<unsigned char>(str[end - 1]))) {
    end--;
  }
  return str.substr(start, end - start);
}

struct TextSlice {
  const char* data = nullptr;
  size_t size = 0;

  TextSlice() = default;
  TextSlice(const char* d, const size_t s) : data(d), size(s) {}

  bool empty() const { return size == 0; }
  char operator[](const size_t index) const { return data[index]; }
};

TextSlice makeSlice(const std::string& str) { return {str.data(), str.size()}; }

TextSlice trimCssWsView(TextSlice str) {
  size_t start = 0;
  while (start < str.size && std::isspace(static_cast<unsigned char>(str.data[start])) != 0) {
    ++start;
  }
  size_t end = str.size;
  while (end > start && std::isspace(static_cast<unsigned char>(str.data[end - 1])) != 0) {
    --end;
  }
  return {str.data + start, end - start};
}

std::vector<std::string> splitCssWhitespaceList(const std::string& raw) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : raw) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) {
    out.push_back(cur);
  }
  return out;
}

struct SelectorMatchInfo {
  bool matched = false;
  bool hasId = false;
  bool hasClass = false;
  bool hasType = false;
  bool firstLetter = false;
  // True when the selector has a descendant/child/sibling combinator (e.g. ".box .p1") whose ancestor part we
  // cannot verify here — we only match its last compound, so it over-matches. Ranked below a plain selector of
  // the same tier so an element's own rule (".p1") wins over an unverifiable scoped rule (".box .p1").
  bool contextual = false;
};

bool sliceEqualsString(const TextSlice slice, const std::string& str) {
  return slice.size == str.size() && std::equal(str.begin(), str.end(), slice.data);
}

bool classTokenListContains(const std::vector<std::string>& classTokensLower, const TextSlice token) {
  for (const auto& tok : classTokensLower) {
    if (sliceEqualsString(token, tok)) {
      return true;
    }
  }
  return false;
}

TextSlice lastCompoundView(TextSlice clauseLower) {
  clauseLower = trimCssWsView(clauseLower);
  if (clauseLower.empty()) {
    return {};
  }
  size_t end = clauseLower.size;
  while (end > 0 && std::isspace(static_cast<unsigned char>(clauseLower.data[end - 1])) != 0) {
    --end;
  }
  if (end == 0) {
    return {};
  }

  size_t start = end;
  while (start > 0) {
    const char c = clauseLower.data[start - 1];
    if (c == '>' || c == '+' || c == '~' || std::isspace(static_cast<unsigned char>(c)) != 0) {
      break;
    }
    --start;
  }
  return trimCssWsView({clauseLower.data + start, end - start});
}

bool compoundMatchesElement(TextSlice compoundLower, const std::string& elementTagLower,
                            const std::vector<std::string>& classTokensLower, const std::string& idLower,
                            SelectorMatchInfo* outInfo) {
  compoundLower = trimCssWsView(compoundLower);
  if (compoundLower.empty()) {
    return false;
  }

  SelectorMatchInfo info;
  size_t i = 0;

  if (compoundLower[0] != '.' && compoundLower[0] != '#' && compoundLower[0] != '[' && compoundLower[0] != '*' &&
      compoundLower[0] != ':' && compoundLower[0] != '>') {
    size_t typeEnd = 0;
    while (typeEnd < compoundLower.size) {
      const unsigned char uc = static_cast<unsigned char>(compoundLower.data[typeEnd]);
      if (std::isalnum(uc) != 0 || compoundLower.data[typeEnd] == '_' || compoundLower.data[typeEnd] == '-') {
        ++typeEnd;
        continue;
      }
      break;
    }
    if (typeEnd > 0) {
      info.hasType = true;
      const TextSlice typeName{compoundLower.data, typeEnd};
      const bool isUniversal = typeName.size == 1 && typeName.data[0] == '*';
      if (!isUniversal && !sliceEqualsString(typeName, elementTagLower)) {
        return false;
      }
      i = typeEnd;
    }
  }

  while (i < compoundLower.size) {
    const char c = compoundLower.data[i];
    if (c == ':') {
      break;
    }
    if (c == '.' || c == '#') {
      size_t j = i + 1;
      while (j < compoundLower.size && isIdentCont(static_cast<unsigned char>(compoundLower.data[j]))) {
        ++j;
      }
      const TextSlice token{compoundLower.data + i + 1, j - i - 1};
      if (!token.empty()) {
        if (c == '#') {
          info.hasId = true;
          if (!sliceEqualsString(token, idLower)) {
            return false;
          }
        } else {
          info.hasClass = true;
          if (!classTokenListContains(classTokensLower, token)) {
            return false;
          }
        }
      }
      i = j;
      continue;
    }
    ++i;
  }

  info.matched = true;
  if (outInfo != nullptr) {
    *outInfo = info;
  }
  return true;
}

SelectorMatchInfo matchSelectorList(const std::string& fullSelectorLower, const std::string& elementTagLower,
                                    const std::vector<std::string>& classTokensLower, const std::string& idLower,
                                    const bool requireFirstLetterPseudo = false) {
  SelectorMatchInfo result;
  size_t start = 0;

  while (start < fullSelectorLower.size()) {
    size_t comma = fullSelectorLower.find(',', start);
    const size_t clauseLen = comma == std::string::npos ? fullSelectorLower.size() - start : comma - start;
    TextSlice clause = trimCssWsView({fullSelectorLower.data() + start, clauseLen});
    if (!clause.empty()) {
      const std::string clauseStr(clause.data, clause.size);
      const bool clauseFirstLetter =
          clauseStr.find(":first-letter") != std::string::npos || clauseStr.find("::first-letter") != std::string::npos;
      if (!requireFirstLetterPseudo || clauseFirstLetter) {
        const TextSlice lastComp = lastCompoundView(clause);
        SelectorMatchInfo clauseInfo;
        if (compoundMatchesElement(lastComp, elementTagLower, classTokensLower, idLower, &clauseInfo)) {
          clauseInfo.firstLetter = clauseFirstLetter;
          clauseInfo.contextual = lastComp.size < trimCssWsView(clause).size;  // has an ancestor/sibling part
          return clauseInfo;
        }
      }
    }

    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }

  return result;
}

bool selectorTargetsPseudoElement(const std::string& selectorLower) {
  return selectorLower.find("::") != std::string::npos || selectorLower.find(":first-letter") != std::string::npos ||
         selectorLower.find(":first-line") != std::string::npos;
}

bool selectorTargetsFirstLetterPseudo(const std::string& selectorLower) {
  return selectorLower.find(":first-letter") != std::string::npos ||
         selectorLower.find("::first-letter") != std::string::npos;
}

bool sortedContains(const std::vector<std::string>& values, const TextSlice token) {
  if (token.empty()) {
    return false;
  }
  const std::string needle(token.data, token.size);
  return std::binary_search(values.begin(), values.end(), needle);
}

bool compoundMayMatchUsage(TextSlice compoundLower, const CssParser::UsageFilter& filter) {
  compoundLower = trimCssWsView(compoundLower);
  if (compoundLower.empty()) {
    return true;
  }

  bool selectorHasConcreteKey = false;
  size_t i = 0;
  if (compoundLower[0] != '.' && compoundLower[0] != '#' && compoundLower[0] != '[' && compoundLower[0] != '*' &&
      compoundLower[0] != ':' && compoundLower[0] != '>') {
    size_t typeEnd = 0;
    while (typeEnd < compoundLower.size) {
      const unsigned char uc = static_cast<unsigned char>(compoundLower.data[typeEnd]);
      if (std::isalnum(uc) != 0 || compoundLower.data[typeEnd] == '_' || compoundLower.data[typeEnd] == '-') {
        ++typeEnd;
        continue;
      }
      break;
    }
    if (typeEnd > 0) {
      selectorHasConcreteKey = true;
      const TextSlice tag{compoundLower.data, typeEnd};
      if (tag.size == 1 && tag.data[0] == '*') {
        return true;
      }
      if (sortedContains(filter.tags, tag)) {
        return true;
      }
      i = typeEnd;
    }
  }

  while (i < compoundLower.size) {
    const char c = compoundLower.data[i];
    if (c == ':') {
      break;
    }
    if (c == '.' || c == '#') {
      size_t j = i + 1;
      while (j < compoundLower.size && isIdentCont(static_cast<unsigned char>(compoundLower.data[j]))) {
        ++j;
      }
      const TextSlice token{compoundLower.data + i + 1, j - i - 1};
      if (!token.empty()) {
        selectorHasConcreteKey = true;
        if ((c == '.' && sortedContains(filter.classes, token)) || (c == '#' && sortedContains(filter.ids, token))) {
          return true;
        }
      }
      i = j;
      continue;
    }
    ++i;
  }

  return !selectorHasConcreteKey;
}

bool selectorMayMatchUsage(const std::string& selectorLower, const CssParser::UsageFilter* filter) {
  if (filter == nullptr || filter->empty()) {
    return true;
  }
  size_t start = 0;
  while (start < selectorLower.size()) {
    const size_t comma = selectorLower.find(',', start);
    const size_t clauseLen = comma == std::string::npos ? selectorLower.size() - start : comma - start;
    const TextSlice clause = trimCssWsView({selectorLower.data() + start, clauseLen});
    if (!clause.empty() && compoundMayMatchUsage(lastCompoundView(clause), *filter)) {
      return true;
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return false;
}

std::string extractCssUrl(const std::string& raw) {
  std::string lowered = raw;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const size_t urlPos = lowered.find("url(");
  if (urlPos == std::string::npos) {
    return {};
  }
  size_t start = urlPos + 4;
  while (start < raw.size() && std::isspace(static_cast<unsigned char>(raw[start])) != 0) {
    ++start;
  }
  const size_t end = raw.find(')', start);
  if (end == std::string::npos || end <= start) {
    return {};
  }
  std::string url = trimCssWs(raw.substr(start, end - start));
  if (url.size() >= 2 && ((url.front() == '"' && url.back() == '"') || (url.front() == '\'' && url.back() == '\''))) {
    url = url.substr(1, url.size() - 2);
  }
  if (url.rfind("blob:", 0) == 0 || url.rfind("data:", 0) == 0 || url.rfind("http:", 0) == 0 ||
      url.rfind("https:", 0) == 0) {
    return {};
  }
  return url;
}

bool parseCssColorLuma(std::string raw, int* outLuma) {
  if (!outLuma) return false;
  raw = trimCssWs(raw);
  std::transform(raw.begin(), raw.end(), raw.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (raw.empty() || raw == "transparent" || raw == "none") {
    return false;
  }
  auto lumaFromRgb = [&](const int r, const int g, const int b) {
    *outLuma = (r * 77 + g * 150 + b * 29) >> 8;
    return true;
  };
  if (raw[0] == '#') {
    const std::string hex = raw.substr(1);
    auto hexVal = [](const char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return -1;
    };
    if (hex.size() == 3 || hex.size() == 4) {
      const int r = hexVal(hex[0]);
      const int g = hexVal(hex[1]);
      const int b = hexVal(hex[2]);
      if (r >= 0 && g >= 0 && b >= 0) return lumaFromRgb(r * 17, g * 17, b * 17);
    } else if (hex.size() == 6 || hex.size() == 8) {
      const int r1 = hexVal(hex[0]), r2 = hexVal(hex[1]);
      const int g1 = hexVal(hex[2]), g2 = hexVal(hex[3]);
      const int b1 = hexVal(hex[4]), b2 = hexVal(hex[5]);
      if (r1 >= 0 && r2 >= 0 && g1 >= 0 && g2 >= 0 && b1 >= 0 && b2 >= 0) {
        return lumaFromRgb((r1 << 4) | r2, (g1 << 4) | g2, (b1 << 4) | b2);
      }
    }
    return false;
  }
  const size_t open = raw.find('(');
  const size_t close = raw.find(')', open == std::string::npos ? 0 : open + 1);
  if (raw.rfind("rgb", 0) == 0 && open != std::string::npos && close != std::string::npos) {
    std::string body = raw.substr(open + 1, close - open - 1);
    std::replace(body.begin(), body.end(), ',', ' ');
    const auto tokens = splitCssWhitespaceList(body);
    if (tokens.size() >= 3) {
      auto channel = [](std::string tok) -> int {
        tok = trimCssWs(tok);
        const bool pct = !tok.empty() && tok.back() == '%';
        const float value = std::strtof(tok.c_str(), nullptr);
        return pct ? static_cast<int>((std::max(0.0f, std::min(100.0f, value)) * 255.0f) / 100.0f + 0.5f)
                   : static_cast<int>(std::max(0.0f, std::min(255.0f, value)));
      };
      return lumaFromRgb(channel(tokens[0]), channel(tokens[1]), channel(tokens[2]));
    }
  }
  if (raw == "gray" || raw == "grey" || raw == "silver" || raw == "darkgray" || raw == "darkgrey" ||
      raw == "lightgray" || raw == "lightgrey") {
    *outLuma = raw.find("dark") != std::string::npos ? 96 : (raw.find("light") != std::string::npos ? 211 : 160);
    return true;
  }
  if (raw == "black") {
    *outLuma = 0;
    return true;
  }
  if (raw == "white") {
    *outLuma = 255;
    return true;
  }
  return false;
}

uint8_t toneFromCssColor(const std::string& raw, const uint8_t fallbackTone) {
  int luma = 0;
  if (!parseCssColorLuma(raw, &luma)) {
    return fallbackTone;
  }
  if (luma < 88) return 1;
  if (luma > 232) return 0;
  return 2;
}

}  // namespace

CssParser::CssParser() {}

CssParser::~CssParser() { clear(); }

void CssParser::clear() {
  rules.clear();
  properties_.clear();
  sourcePaths_.clear();
  bodyTextAlignRaw.clear();
  mcValid_ = false;
  mcMatched_.clear();
}

uint16_t CssParser::internSourcePath(const std::string& path) {
  for (size_t i = 0; i < sourcePaths_.size(); ++i) {
    if (sourcePaths_[i] == path) {
      return static_cast<uint16_t>(i);
    }
  }
  sourcePaths_.push_back(path);
  return static_cast<uint16_t>(sourcePaths_.size() - 1);
}

bool CssParser::saveBinary(FsFile& file) const {
  if (!file) {
    return false;
  }
  if (sourcePaths_.size() > kMaxCssSourcePaths || rules.size() > kMaxCssRules) {
    return false;
  }

  serialization::writePod(file, kCssParserCacheMagic);
  serialization::writePod(file, kCssParserCacheVersion);

  const uint16_t sourceCount = static_cast<uint16_t>(sourcePaths_.size());
  serialization::writePod(file, sourceCount);
  for (const auto& source : sourcePaths_) {
    if (!writeStringChecked(file, source)) return false;
  }

  if (!writeStringChecked(file, bodyTextAlignRaw)) return false;

  const uint16_t ruleCount = static_cast<uint16_t>(rules.size());
  serialization::writePod(file, ruleCount);
  for (const auto& rule : rules) {
    if (!writeStringChecked(file, rule.selectorLower)) return false;
    serialization::writePod(file, rule.sourcePathIndex);
    const uint8_t flags = (rule.isPseudoElement ? 0x01 : 0x00) | (rule.isFirstLetterPseudo ? 0x02 : 0x00);
    serialization::writePod(file, flags);
    if (rule.propertyCount > kMaxCssPropertiesPerRule ||
        static_cast<size_t>(rule.propertyStart) + rule.propertyCount > properties_.size()) {
      return false;
    }
    serialization::writePod(file, rule.propertyCount);
    for (uint16_t i = 0; i < rule.propertyCount; ++i) {
      const auto& prop = properties_[rule.propertyStart + i];
      if (!writeStringChecked(file, cssPropertyName(prop.id)) || !writeStringChecked(file, prop.value)) return false;
    }
  }

  return true;
}

bool CssParser::loadBinary(FsFile& file) {
  clear();
  if (!file) {
    return false;
  }

  uint32_t magic = 0;
  uint16_t version = 0;
  serialization::readPod(file, magic);
  serialization::readPod(file, version);
  if (magic != kCssParserCacheMagic || version != kCssParserCacheVersion) {
    clear();
    return false;
  }

  uint16_t sourceCount = 0;
  serialization::readPod(file, sourceCount);
  if (sourceCount > kMaxCssSourcePaths) {
    clear();
    return false;
  }
  sourcePaths_.reserve(sourceCount);
  for (uint16_t i = 0; i < sourceCount; ++i) {
    std::string source;
    if (!readBoundedString(file, source)) {
      clear();
      return false;
    }
    sourcePaths_.push_back(std::move(source));
  }

  if (!readBoundedString(file, bodyTextAlignRaw)) {
    clear();
    return false;
  }

  uint16_t ruleCount = 0;
  serialization::readPod(file, ruleCount);
  if (ruleCount > kMaxCssRules) {
    clear();
    return false;
  }
  rules.reserve(ruleCount);
  for (uint16_t i = 0; i < ruleCount; ++i) {
    CssRule rule;
    if (!readBoundedString(file, rule.selectorLower)) {
      clear();
      return false;
    }
    serialization::readPod(file, rule.sourcePathIndex);
    if (rule.sourcePathIndex >= sourcePaths_.size() && !sourcePaths_.empty()) {
      clear();
      return false;
    }
    uint8_t flags = 0;
    serialization::readPod(file, flags);
    rule.isPseudoElement = (flags & 0x01) != 0;
    rule.isFirstLetterPseudo = (flags & 0x02) != 0;

    uint16_t propertyCount = 0;
    serialization::readPod(file, propertyCount);
    if (propertyCount > kMaxCssPropertiesPerRule) {
      clear();
      return false;
    }
    rule.propertyStart = static_cast<uint16_t>(properties_.size());
    rule.propertyCount = propertyCount;
    for (uint16_t j = 0; j < propertyCount; ++j) {
      std::string name;
      std::string value;
      if (!readBoundedString(file, name) || !readBoundedString(file, value) || !isTrackedCssProperty(name)) {
        clear();
        return false;
      }
      const uint8_t id = cssPropertyId(name);
      if (id == kCssPropertyInvalid) {
        clear();
        return false;
      }
      CssProperty prop;
      prop.id = id;
      prop.value = std::move(value);
      properties_.push_back(std::move(prop));
    }
    rules.push_back(std::move(rule));
  }

  mcValid_ = false;
  mcMatched_.clear();
  return true;
}

void CssParser::parse(const std::string& cssContent, const std::string& sourcePath, uint32_t minFreeHeapBytes,
                      const UsageFilter* usageFilter) {
  uint16_t filteredRules = 0;

  if (cssContent.length() > 50 * 1024) {
    Serial.printf("[CSSP] Skipping large CSS content (%d bytes)\n", (int)cssContent.length());
    return;
  }

  size_t pos = 0;
  size_t len = cssContent.length();

  while (pos < len) {
    while (pos < len &&
           (cssContent[pos] == ' ' || cssContent[pos] == '\n' || cssContent[pos] == '\r' || cssContent[pos] == '\t')) {
      pos++;
    }

    if (pos >= len) break;

    if (pos + 1 < len && cssContent[pos] == '/' && cssContent[pos + 1] == '*') {
      pos += 2;
      while (pos + 1 < len && !(cssContent[pos] == '*' && cssContent[pos + 1] == '/')) {
        pos++;
      }
      pos += 2;
      continue;
    }

    size_t selectorStart = pos;
    bool inString = false;
    char stringChar = '\0';

    while (pos < len) {
      char c = cssContent[pos];

      if (!inString && c == '{') {
        break;
      }

      if (!inString && (c == '"' || c == '\'')) {
        inString = true;
        stringChar = c;
      } else if (inString && c == stringChar) {
        inString = false;
      }

      pos++;
    }

    if (pos >= len) break;

    std::string selector = cssContent.substr(selectorStart, pos - selectorStart);
    selector = trim(selector);

    if (selector.empty()) {
      pos++;
      continue;
    }

    pos++;
    size_t blockStart = pos;
    int braceCount = 1;
    inString = false;

    while (pos < len && braceCount > 0) {
      char c = cssContent[pos];

      if (!inString && c == '{') {
        braceCount++;
      } else if (!inString && c == '}') {
        braceCount--;
      } else if (!inString && (c == '"' || c == '\'')) {
        inString = true;
        stringChar = c;
      } else if (inString && c == stringChar) {
        inString = false;
      }

      pos++;
    }

    if (braceCount != 0) break;

#ifdef ARDUINO
    if (minFreeHeapBytes > 0 && ESP.getFreeHeap() < minFreeHeapBytes + 12 * 1024) {
      Serial.printf("[CSSP] Stopping CSS scan before rule allocation (free=%u, rules=%u)\n",
                    static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(rules.size()));
      break;
    }
#endif

    std::string propertiesStr = cssContent.substr(blockStart, pos - blockStart - 1);

    CssRule rule;
    rule.selectorLower = toLower(selector);
    if (!selectorMayMatchUsage(rule.selectorLower, usageFilter)) {
      ++filteredRules;
      continue;
    }

    CssProperties parsedProperties;
    parsePropertiesForDimensions(propertiesStr, parsedProperties);

    if (!parsedProperties.empty()) {
      noteBodyHtmlTextAlign(selector, parsedProperties);
#ifdef ARDUINO
      if (minFreeHeapBytes > 0 && ESP.getFreeHeap() < minFreeHeapBytes) {
        Serial.printf("[CSSP] Stopping CSS load to reserve heap (free=%u, rules=%u)\n",
                      static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(rules.size()));
        break;
      }
#else
      (void)minFreeHeapBytes;
#endif
      if (rules.size() < kMaxCssRules) {
        rule.sourcePathIndex = internSourcePath(sourcePath);
        rule.isPseudoElement = selectorTargetsPseudoElement(rule.selectorLower);
        rule.isFirstLetterPseudo = selectorTargetsFirstLetterPseudo(rule.selectorLower);
        rule.propertyStart = static_cast<uint16_t>(properties_.size());
        rule.propertyCount = static_cast<uint16_t>(parsedProperties.size());
        for (auto& prop : parsedProperties) {
          properties_.push_back(std::move(prop));
        }
        rules.push_back(std::move(rule));
      } else {
        Serial.printf("[CSSP] Reached max rules limit (%u)\n", static_cast<unsigned>(kMaxCssRules));
        break;
      }
    }
  }

  // Rule set (and thus rule pointers) changed; drop the per-element match cache.
  mcValid_ = false;
  mcMatched_.clear();

  Serial.printf("[CSSP] Parsed %zu CSS rules\n", rules.size());
  if (usageFilter != nullptr && !usageFilter->empty()) {
    Serial.printf("[CSSP] Chapter CSS filter skipped %u rules\n", static_cast<unsigned>(filteredRules));
  }
}

void CssParser::parsePropertiesForDimensions(const std::string& propertiesStr, CssProperties& properties) const {
  size_t pos = 0;
  size_t len = propertiesStr.length();

  while (pos < len) {
    while (pos < len && isspace(static_cast<unsigned char>(propertiesStr[pos]))) {
      pos++;
    }

    if (pos >= len) break;

    size_t nameStart = pos;
    while (pos < len && propertiesStr[pos] != ':') {
      pos++;
    }

    if (pos >= len) break;

    std::string propName = propertiesStr.substr(nameStart, pos - nameStart);
    propName = trim(toLower(propName));

    if (!isTrackedCssProperty(propName)) {
      while (pos < len && propertiesStr[pos] != ';') {
        pos++;
      }
      if (pos < len) pos++;
      continue;
    }

    pos++;

    while (pos < len && isspace(static_cast<unsigned char>(propertiesStr[pos]))) {
      pos++;
    }

    size_t valueStart = pos;
    while (pos < len && propertiesStr[pos] != ';') {
      pos++;
    }

    std::string propValue = propertiesStr.substr(valueStart, pos - valueStart);
    propValue = trim(propValue);

    size_t importantPos = propValue.find("!important");
    if (importantPos != std::string::npos) {
      propValue = propValue.substr(0, importantPos);
      propValue = trim(propValue);
    }

    if (!propName.empty() && !propValue.empty()) {
      if (propName == "inline-size") {
        setProperty(properties, "width", std::move(propValue));
      } else if (propName == "block-size") {
        setProperty(properties, "height", std::move(propValue));
      } else if (propName == "max-inline-size") {
        setProperty(properties, "max-width", std::move(propValue));
      } else if (propName == "min-inline-size") {
        setProperty(properties, "min-width", std::move(propValue));
      } else if (propName == "max-block-size") {
        setProperty(properties, "max-height", std::move(propValue));
      } else if (propName == "min-block-size") {
        setProperty(properties, "min-height", std::move(propValue));
      } else {
        setProperty(properties, std::move(propName), std::move(propValue));
      }
    }

    if (pos < len) pos++;
  }
}

void CssParser::parseInlineStyle(const std::string& styleAttr, std::map<std::string, std::string>& out) const {
  out.clear();
  if (styleAttr.empty()) return;

  size_t i = 0;
  const size_t n = styleAttr.size();
  while (i < n) {
    while (i < n && (styleAttr[i] == ';' || isspace(static_cast<unsigned char>(styleAttr[i])))) {
      i++;
    }
    if (i >= n) break;

    size_t nameStart = i;
    while (i < n && styleAttr[i] != ':') {
      i++;
    }
    if (i >= n) break;

    std::string name = trim(toLower(styleAttr.substr(nameStart, i - nameStart)));
    i++;
    while (i < n && isspace(static_cast<unsigned char>(styleAttr[i]))) {
      i++;
    }
    size_t valStart = i;
    while (i < n && styleAttr[i] != ';') {
      i++;
    }
    std::string val = trim(styleAttr.substr(valStart, i - valStart));
    size_t imp = val.find("!important");
    if (imp != std::string::npos) {
      val = trim(val.substr(0, imp));
    }
    if (!name.empty() && !val.empty()) {
      if (name == "inline-size") {
        out["width"] = val;
      } else if (name == "block-size") {
        out["height"] = val;
      } else if (name == "max-inline-size") {
        out["max-width"] = val;
      } else if (name == "min-inline-size") {
        out["min-width"] = val;
      } else if (name == "max-block-size") {
        out["max-height"] = val;
      } else if (name == "min-block-size") {
        out["min-height"] = val;
      } else {
        out[name] = val;
      }
    }
    if (i < n && styleAttr[i] == ';') i++;
  }
}

/** EPUB CSS can contain huge numbers; clamp so layout math cannot overflow or corrupt memory. */
static int clampCssPixels(const int v, const bool allowNegative = false) {
  constexpr int kMaxCssPx = 8192;
  if (v < -kMaxCssPx) return allowNegative ? -kMaxCssPx : 0;
  if (v < 0) return allowNegative ? v : 0;
  if (v > kMaxCssPx) return kMaxCssPx;
  return v;
}

int CssParser::parseCssLength(const std::string& value, int viewportWidth, int viewportHeight,
                              bool percentOfWidth) const {
  return parseDimensionValue(trim(value), viewportWidth, viewportHeight,
                             percentOfWidth ? PercentRefersTo::Width : PercentRefersTo::Height);
}

int CssParser::parseDimensionValue(const std::string& valueIn, int viewportWidth, int viewportHeight,
                                   PercentRefersTo percentAxis) const {
  std::string value = trim(valueIn);
  if (value.empty()) return 0;

  const std::string vchk = toLower(value);
  if (vchk == "auto" || vchk == "none" || vchk == "initial" || vchk == "inherit" || vchk == "unset") {
    return 0;
  }

  std::string numStr;
  std::string unit;
  bool foundDigit = false;

  for (char ch : value) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (std::isdigit(c) != 0 || ch == '.' || (ch == '-' && !foundDigit)) {
      numStr += ch;
      foundDigit = true;
    } else if (foundDigit && (std::isalpha(c) != 0 || ch == '%')) {
      unit += static_cast<char>(std::tolower(c));
    } else if (!foundDigit && !isspace(c)) {
      break;
    }
  }

  if (numStr.empty()) return 0;

  char* parseEnd = nullptr;
  const float num = std::strtof(numStr.c_str(), &parseEnd);
  if (parseEnd == numStr.c_str()) {
    return 0;
  }

  constexpr int BASE_FONT_SIZE = 16;
  constexpr float PX_PER_IN = 96.f;
  constexpr float PX_PER_PT = PX_PER_IN / 72.f;
  constexpr float PX_PER_CM = PX_PER_IN / 2.54f;
  constexpr float PX_PER_MM = PX_PER_IN / 25.4f;

  const bool allowNegative = percentAxis == PercentRefersTo::Width;
  if (unit == "px" || unit.empty()) {
    return clampCssPixels(static_cast<int>(num + (num >= 0 ? 0.5f : -0.5f)), allowNegative);
  }
  if (unit == "em" || unit == "rem") {
    return clampCssPixels(static_cast<int>(num * BASE_FONT_SIZE + (num >= 0 ? 0.5f : -0.5f)), allowNegative);
  }
  if (unit == "%") {
    if (percentAxis == PercentRefersTo::Width && viewportWidth > 0) {
      return clampCssPixels(static_cast<int>(num * viewportWidth / 100.0f + (num >= 0 ? 0.5f : -0.5f)),
                            allowNegative);
    }
    if (percentAxis == PercentRefersTo::Height && viewportHeight > 0) {
      return clampCssPixels(static_cast<int>(num * viewportHeight / 100.0f + (num >= 0 ? 0.5f : -0.5f)));
    }
    return 0;
  }
  if (unit == "vw" && viewportWidth > 0) {
    return clampCssPixels(static_cast<int>(num * viewportWidth / 100.0f + 0.5f));
  }
  if (unit == "vh" && viewportHeight > 0) {
    return clampCssPixels(static_cast<int>(num * viewportHeight / 100.0f + 0.5f));
  }
  if (unit == "vmin" && viewportWidth > 0 && viewportHeight > 0) {
    const int mn = std::min(viewportWidth, viewportHeight);
    return clampCssPixels(static_cast<int>(num * mn / 100.0f + 0.5f));
  }
  if (unit == "vmax" && viewportWidth > 0 && viewportHeight > 0) {
    const int mx = std::max(viewportWidth, viewportHeight);
    return clampCssPixels(static_cast<int>(num * mx / 100.0f + 0.5f));
  }
  if (unit == "pt") {
    return clampCssPixels(static_cast<int>(num * PX_PER_PT + (num >= 0 ? 0.5f : -0.5f)), allowNegative);
  }
  if (unit == "in") {
    return clampCssPixels(static_cast<int>(num * PX_PER_IN + (num >= 0 ? 0.5f : -0.5f)), allowNegative);
  }
  if (unit == "cm") {
    return clampCssPixels(static_cast<int>(num * PX_PER_CM + (num >= 0 ? 0.5f : -0.5f)), allowNegative);
  }
  if (unit == "mm") {
    return clampCssPixels(static_cast<int>(num * PX_PER_MM + (num >= 0 ? 0.5f : -0.5f)), allowNegative);
  }

  return clampCssPixels(static_cast<int>(num + (num >= 0 ? 0.5f : -0.5f)), allowNegative);
}

int CssParser::getInlineOrSheetLength(const std::string& propName, const std::string& className, const std::string& id,
                                      const std::string& styleAttr, int viewportWidth, int viewportHeight) const {
  const PercentRefersTo pct = (propName == "height" || propName == "min-height" || propName == "max-height")
                                  ? PercentRefersTo::Height
                                  : PercentRefersTo::Width;

  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  auto itIn = inlineMap.find(propName);
  if (itIn != inlineMap.end()) {
    return parseDimensionValue(itIn->second, viewportWidth, viewportHeight, pct);
  }

  std::string idLast;
  std::string clsLast;
  std::string typeLast;
  bool hasIdLast = false;
  bool hasClsLast = false;
  bool hasTypeLast = false;

  std::string idLower = toLower(trim(id));
  std::vector<std::string> classTokens;
  splitClassTokens(className, classTokens);
  for (auto& t : classTokens) {
    t = toLower(trim(t));
  }

  for (const auto& rule : rules) {
    if (rule.isPseudoElement) {
      continue;
    }
    const SelectorMatchInfo matchInfo = matchSelectorList(rule.selectorLower, "", classTokens, idLower, false);
    if (!matchInfo.matched) {
      continue;
    }

    const std::string* prop = rulePropertyValue(rule, propName);
    if (!prop) {
      continue;
    }

    if (matchInfo.hasId) {
      idLast = *prop;
      hasIdLast = true;
    } else if (matchInfo.hasClass) {
      clsLast = *prop;
      hasClsLast = true;
    } else if (matchInfo.hasType) {
      typeLast = *prop;
      hasTypeLast = true;
    }
  }

  if (hasIdLast) {
    return parseDimensionValue(idLast, viewportWidth, viewportHeight, pct);
  }
  if (hasClsLast) {
    return parseDimensionValue(clsLast, viewportWidth, viewportHeight, pct);
  }
  if (hasTypeLast) {
    return parseDimensionValue(typeLast, viewportWidth, viewportHeight, pct);
  }
  return 0;
}

int CssParser::getSpacingEdgePx(const std::string& propName, const std::string& shorthandName,
                                const std::string& className, const std::string& id, const std::string& styleAttr,
                                int viewportWidth, int viewportHeight, const std::string& elementTagLower) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  auto resolveEdge = [&](const std::string& raw) -> int {
    const auto tokens = splitCssWhitespaceList(trimCssWs(raw));
    if (tokens.empty()) {
      return 0;
    }
    int edgeIndex = 0;
    if (propName.find("-right") != std::string::npos) {
      edgeIndex = 1;
    } else if (propName.find("-bottom") != std::string::npos) {
      edgeIndex = 2;
    } else if (propName.find("-left") != std::string::npos) {
      edgeIndex = 3;
    }
    if (tokens.size() == 1) {
      return parseCssLength(tokens[0], viewportWidth, viewportHeight, true);
    }
    if (tokens.size() == 2) {
      return parseCssLength((edgeIndex == 1 || edgeIndex == 3) ? tokens[1] : tokens[0], viewportWidth, viewportHeight,
                            true);
    }
    if (tokens.size() == 3) {
      return parseCssLength(edgeIndex == 0 ? tokens[0] : (edgeIndex == 2 ? tokens[2] : tokens[1]), viewportWidth,
                            viewportHeight, true);
    }
    return parseCssLength(tokens[edgeIndex], viewportWidth, viewportHeight, true);
  };

  const auto itIn = inlineMap.find(propName);
  if (itIn != inlineMap.end()) {
    return resolveEdge(itIn->second);
  }

  const auto shorthandIn = inlineMap.find(shorthandName);
  if (shorthandIn != inlineMap.end()) {
    return resolveEdge(shorthandIn->second);
  }

  const std::string direct = getCascadedPropertyValue(propName, className, id, styleAttr, elementTagLower);
  if (!direct.empty()) {
    return resolveEdge(direct);
  }

  const std::string shorthand = getCascadedPropertyValue(shorthandName, className, id, styleAttr, elementTagLower);
  if (!shorthand.empty()) {
    return resolveEdge(shorthand);
  }

  return 0;
}

int CssParser::getBorderEdgePx(const std::string& edgePropName, const std::string& className, const std::string& id,
                               const std::string& styleAttr, int viewportWidth, int viewportHeight,
                               const std::string& elementTagLower) const {
  auto parseBorderWidthToken = [&](std::string raw) -> int {
    raw = trimCssWs(raw);
    if (raw.empty()) {
      return 0;
    }
    std::string lowered = raw;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowered.find("none") != std::string::npos) {
      return 0;
    }
    const auto tokens = splitCssWhitespaceList(lowered);
    for (const auto& tok : tokens) {
      if (tok == "thin") return 1;
      if (tok == "medium") return 2;
      if (tok == "thick") return 3;
      const int px = parseCssLength(tok, viewportWidth, viewportHeight, true);
      if (px > 0) {
        return px;
      }
    }
    return 0;
  };

  auto parseBorderWidthShorthand = [&](std::string raw, const std::string& edgeProp) -> int {
    raw = trimCssWs(raw);
    if (raw.empty()) {
      return 0;
    }
    const auto tokens = splitCssWhitespaceList(raw);
    if (tokens.empty()) {
      return 0;
    }
    auto tokenPx = [&](const std::string& tok) -> int {
      std::string lowered = tok;
      std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (lowered == "thin") return 1;
      if (lowered == "medium") return 2;
      if (lowered == "thick") return 3;
      return parseCssLength(lowered, viewportWidth, viewportHeight, true);
    };
    if (tokens.size() == 1) {
      return std::max(0, tokenPx(tokens[0]));
    }
    const bool topEdge = edgeProp.find("-top") != std::string::npos;
    const bool rightEdge = edgeProp.find("-right") != std::string::npos;
    const bool bottomEdge = edgeProp.find("-bottom") != std::string::npos;
    if (tokens.size() == 2) {
      return std::max(0, tokenPx((topEdge || bottomEdge) ? tokens[0] : tokens[1]));
    }
    if (tokens.size() == 3) {
      return std::max(0, tokenPx(topEdge ? tokens[0] : (bottomEdge ? tokens[2] : tokens[1])));
    }
    return std::max(0, tokenPx(topEdge ? tokens[0] : (rightEdge ? tokens[1] : (bottomEdge ? tokens[2] : tokens[3]))));
  };

  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  const auto edgeInlineIt = inlineMap.find(edgePropName);
  if (edgeInlineIt != inlineMap.end()) {
    return parseBorderWidthToken(edgeInlineIt->second);
  }
  const auto borderInlineIt = inlineMap.find("border");
  if (borderInlineIt != inlineMap.end()) {
    return parseBorderWidthToken(borderInlineIt->second);
  }
  const auto borderWidthInlineIt = inlineMap.find("border-width");
  if (borderWidthInlineIt != inlineMap.end()) {
    return parseBorderWidthShorthand(borderWidthInlineIt->second, edgePropName);
  }

  const std::string edgeSheet = getCascadedPropertyValue(edgePropName, className, id, styleAttr, elementTagLower);
  if (!edgeSheet.empty()) {
    return parseBorderWidthToken(edgeSheet);
  }
  const std::string borderSheet = getCascadedPropertyValue("border", className, id, styleAttr, elementTagLower);
  if (!borderSheet.empty()) {
    return parseBorderWidthToken(borderSheet);
  }
  const std::string borderWidthSheet =
      getCascadedPropertyValue("border-width", className, id, styleAttr, elementTagLower);
  if (!borderWidthSheet.empty()) {
    return parseBorderWidthShorthand(borderWidthSheet, edgePropName);
  }
  return 0;
}

std::string CssParser::getBorderStyleKeyword(const std::string& edge, const std::string& className,
                                             const std::string& id, const std::string& styleAttr,
                                             const std::string& elementTagLower) const {
  // Pull the first border-style keyword out of a value (handles shorthands like "currentColor double medium").
  auto extractStyle = [](const std::string& raw) -> std::string {
    for (const auto& tokRaw : splitCssWhitespaceList(trimCssWs(raw))) {
      std::string tok = tokRaw;
      std::transform(tok.begin(), tok.end(), tok.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (tok == "double" || tok == "dotted" || tok == "dashed" || tok == "solid") return tok;
      // Other CSS styles render as a plain solid rule on e-ink.
      if (tok == "groove" || tok == "ridge" || tok == "inset" || tok == "outset") return "solid";
    }
    return "";
  };

  const std::string edgeStyleProp = "border-" + edge + "-style";
  const std::string edgeProp = "border-" + edge;

  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  const char* props[] = {edgeStyleProp.c_str(), edgeProp.c_str(), "border-style", "border"};
  for (const char* prop : props) {
    const auto it = inlineMap.find(prop);
    if (it != inlineMap.end()) {
      const std::string s = extractStyle(it->second);
      if (!s.empty()) return s;
    }
    const std::string sheet = getCascadedPropertyValue(prop, className, id, styleAttr, elementTagLower);
    if (!sheet.empty()) {
      const std::string s = extractStyle(sheet);
      if (!s.empty()) return s;
    }
  }
  return "solid";
}

int CssParser::getBorderRadiusPx(const std::string& elementTagLower, const std::string& className,
                                 const std::string& id, const std::string& styleAttr, const int viewportWidth,
                                 const int viewportHeight) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  std::string raw;
  const auto inlineIt = inlineMap.find("border-radius");
  if (inlineIt != inlineMap.end()) {
    raw = inlineIt->second;
  } else {
    raw = getCascadedPropertyValue("border-radius", className, id, styleAttr, elementTagLower);
  }
  const auto tokens = splitCssWhitespaceList(trimCssWs(raw));
  if (tokens.empty()) {
    return 0;
  }
  return std::max(0, parseCssLength(tokens[0], viewportWidth, viewportHeight, true));
}

uint8_t CssParser::getBorderTone(const std::string& elementTagLower, const std::string& className,
                                 const std::string& id, const std::string& styleAttr) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  for (const char* prop : {"border-color", "border"}) {
    const auto inlineIt = inlineMap.find(prop);
    if (inlineIt != inlineMap.end()) {
      const uint8_t tone = toneFromCssColor(inlineIt->second, 1);
      if (tone != 1 || prop == std::string("border-color")) return tone;
    }
    const std::string sheet = getCascadedPropertyValue(prop, className, id, styleAttr, elementTagLower);
    if (!sheet.empty()) {
      const uint8_t tone = toneFromCssColor(sheet, 1);
      if (tone != 1 || prop == std::string("border-color")) return tone;
    }
  }
  return 1;
}

uint8_t CssParser::getBackgroundTone(const std::string& elementTagLower, const std::string& className,
                                     const std::string& id, const std::string& styleAttr) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  for (const char* prop : {"background-color", "background"}) {
    const auto inlineIt = inlineMap.find(prop);
    if (inlineIt != inlineMap.end()) {
      return toneFromCssColor(inlineIt->second, 0);
    }
    const std::string sheet = getCascadedPropertyValue(prop, className, id, styleAttr, elementTagLower);
    if (!sheet.empty()) {
      return toneFromCssColor(sheet, 0);
    }
  }
  return 0;
}

float CssParser::getFontSizeEm(const std::string& elementTagLower, const std::string& className, const std::string& id,
                               const std::string& styleAttr) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  std::string raw;
  const auto it = inlineMap.find("font-size");
  if (it != inlineMap.end()) {
    raw = it->second;
  } else {
    raw = getCascadedPropertyValue("font-size", className, id, styleAttr, elementTagLower);
  }
  raw = toLower(trim(raw));
  if (raw.empty()) return 1.0f;

  // Size keywords (approximate CSS scaling).
  if (raw == "medium") return 1.0f;
  if (raw == "small") return 0.83f;
  if (raw == "x-small") return 0.69f;
  if (raw == "xx-small") return 0.58f;
  if (raw == "large") return 1.2f;
  if (raw == "x-large") return 1.5f;
  if (raw == "xx-large") return 2.0f;
  if (raw == "larger") return 1.2f;
  if (raw == "smaller") return 0.83f;

  std::string numStr;
  std::string unit;
  bool foundDigit = false;
  for (char ch : raw) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isdigit(c) != 0 || ch == '.' || (ch == '-' && !foundDigit)) {
      numStr += ch;
      foundDigit = true;
    } else if (foundDigit && (std::isalpha(c) != 0 || ch == '%')) {
      unit += ch;
    } else if (!foundDigit && !std::isspace(c)) {
      break;
    }
  }
  if (numStr.empty()) return 1.0f;
  const float num = std::strtof(numStr.c_str(), nullptr);
  if (num <= 0.0f) return 1.0f;

  if (unit == "em" || unit == "rem") return num;
  if (unit == "%") return num / 100.0f;
  if (unit == "px") return num / 16.0f;  // relative to the 16px CSS base
  if (unit == "pt") return (num * 96.0f / 72.0f) / 16.0f;
  return num;  // unitless — treat like em
}

uint8_t CssParser::getVerticalAlign(const std::string& elementTagLower, const std::string& className,
                                    const std::string& id, const std::string& styleAttr) const {
  std::string raw = getCascadedPropertyValue("vertical-align", className, id, styleAttr, elementTagLower);
  raw = toLower(trim(raw));
  if (raw == "super") {
    return 1;
  }
  if (raw == "sub") {
    return 2;
  }
  return 0;
}

int CssParser::getWidth(const std::string& className, const std::string& id, const std::string& styleAttr,
                        int viewportWidth, int viewportHeight) const {
  return getInlineOrSheetLength("width", className, id, styleAttr, viewportWidth, viewportHeight);
}

int CssParser::getHeight(const std::string& className, const std::string& id, const std::string& styleAttr,
                         int viewportWidth, int viewportHeight) const {
  return getInlineOrSheetLength("height", className, id, styleAttr, viewportWidth, viewportHeight);
}

int CssParser::getMaxWidth(const std::string& className, const std::string& id, const std::string& styleAttr,
                           int viewportWidth, int viewportHeight) const {
  return getInlineOrSheetLength("max-width", className, id, styleAttr, viewportWidth, viewportHeight);
}

int CssParser::getMinWidth(const std::string& className, const std::string& id, const std::string& styleAttr,
                           int viewportWidth, int viewportHeight) const {
  return getInlineOrSheetLength("min-width", className, id, styleAttr, viewportWidth, viewportHeight);
}

int CssParser::getMaxHeight(const std::string& className, const std::string& id, const std::string& styleAttr,
                            int viewportWidth, int viewportHeight) const {
  return getInlineOrSheetLength("max-height", className, id, styleAttr, viewportWidth, viewportHeight);
}

int CssParser::getMinHeight(const std::string& className, const std::string& id, const std::string& styleAttr,
                            int viewportWidth, int viewportHeight) const {
  return getInlineOrSheetLength("min-height", className, id, styleAttr, viewportWidth, viewportHeight);
}

void CssParser::noteBodyHtmlTextAlign(const std::string& selectorRaw, const CssProperties& props) {
  const std::string* rawTextAlign = propertyValue(props, "text-align");
  if (!rawTextAlign) {
    return;
  }
  const std::string val = trim(*rawTextAlign);
  if (val.empty()) {
    return;
  }
  size_t start = 0;
  while (start < selectorRaw.size()) {
    const size_t comma = selectorRaw.find(',', start);
    const size_t clauseLen = comma == std::string::npos ? selectorRaw.size() - start : comma - start;
    std::string clause = toLower(trim(std::string(selectorRaw.data() + start, clauseLen)));
    const TextSlice compound = lastCompoundView(makeSlice(clause));
    bool matchesBodyHtml = clause == "body" || clause == "html";
    if (!matchesBodyHtml && !compound.empty()) {
      size_t typeEnd = 0;
      while (typeEnd < compound.size) {
        const unsigned char uc = static_cast<unsigned char>(compound.data[typeEnd]);
        if (std::isalnum(uc) != 0 || compound.data[typeEnd] == '_' || compound.data[typeEnd] == '-') {
          ++typeEnd;
          continue;
        }
        break;
      }
      if (typeEnd == 4) {
        matchesBodyHtml = std::equal(compound.data, compound.data + 4, "body") ||
                          std::equal(compound.data, compound.data + 4, "html");
      }
    }
    if (matchesBodyHtml) {
      bodyTextAlignRaw = val;
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
}

const std::vector<CssParser::MatchedRule>& CssParser::matchedRulesFor(const std::string& elementTagLower,
                                                                      const std::string& className,
                                                                      const std::string& id) const {
  if (mcValid_ && mcTag_ == elementTagLower && mcClass_ == className && mcId_ == id) {
    return mcMatched_;
  }
  mcTag_ = elementTagLower;
  mcClass_ = className;
  mcId_ = id;
  mcMatched_.clear();

  const std::string idLower = toLower(trim(id));
  std::vector<std::string> classTokens;
  splitClassTokens(className, classTokens);
  for (auto& t : classTokens) {
    t = toLower(trim(t));
  }

  for (const auto& rule : rules) {
    if (rule.isPseudoElement) {
      continue;
    }
    const SelectorMatchInfo matchInfo =
        matchSelectorList(rule.selectorLower, elementTagLower, classTokens, idLower, false);
    if (!matchInfo.matched) {
      continue;
    }
    // Universal-only matches (no id/class/type) never contributed a cascaded value, so skip them.
    if (matchInfo.hasId) {
      mcMatched_.push_back({&rule, 2, matchInfo.contextual});
    } else if (matchInfo.hasClass) {
      mcMatched_.push_back({&rule, 1, matchInfo.contextual});
    } else if (matchInfo.hasType) {
      mcMatched_.push_back({&rule, 0, matchInfo.contextual});
    }
  }
  mcValid_ = true;
  return mcMatched_;
}

const std::string* CssParser::rulePropertyValue(const CssRule& rule, const std::string& propName) const {
  const uint8_t id = cssPropertyId(propName);
  if (id == kCssPropertyInvalid) {
    return nullptr;
  }
  const size_t end = std::min(properties_.size(), static_cast<size_t>(rule.propertyStart) + rule.propertyCount);
  for (size_t i = rule.propertyStart; i < end; ++i) {
    if (properties_[i].id == id) {
      return &properties_[i].value;
    }
  }
  return nullptr;
}

bool CssParser::ruleHasProperty(const CssRule& rule, const std::string& propName) const {
  return rulePropertyValue(rule, propName) != nullptr;
}

const CssParser::CssRule* CssParser::winningRuleForProperty(const std::string& propName, const std::string& className,
                                                            const std::string& id, const std::string& elementTagLower,
                                                            const bool ignoreContextual) const {
  // Rank: id(2) > class(1) > type(0); within a tier a PLAIN selector beats an unverifiable combinator selector
  // (so an element's own ".p1" wins over a scoped ".box .p1" we can't verify). Highest rank wins; equal rank →
  // last match in source order wins (>= keeps the later one).
  const CssRule* best = nullptr;
  int bestPriority = -1;
  for (const auto& m : matchedRulesFor(elementTagLower, className, id)) {
    if (ignoreContextual && m.contextual) {
      continue;  // unverifiable combinator selector — don't let it decide this property
    }
    if (!ruleHasProperty(*m.rule, propName)) {
      continue;
    }
    const int priority = m.tier * 2 + (m.contextual ? 0 : 1);
    if (priority >= bestPriority) {
      bestPriority = priority;
      best = m.rule;
    }
  }
  return best;
}

std::string CssParser::getCascadedPropertyValue(const std::string& propName, const std::string& className,
                                                const std::string& id, const std::string& styleAttr,
                                                const std::string& elementTagLower) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  const auto itIn = inlineMap.find(propName);
  if (itIn != inlineMap.end()) {
    return itIn->second;  // inline style wins over the stylesheet
  }

  const CssRule* winner = winningRuleForProperty(propName, className, id, elementTagLower);
  if (!winner) {
    return std::string();
  }
  const std::string* value = rulePropertyValue(*winner, propName);
  return value ? *value : std::string();
}

bool CssParser::getCascadedPropertyValueAndSource(const std::string& propName, const std::string& className,
                                                  const std::string& id, const std::string& styleAttr,
                                                  const std::string& elementTagLower, std::string* outValue,
                                                  std::string* outSourcePath) const {
  if (outValue) *outValue = "";
  if (outSourcePath) *outSourcePath = "";

  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  const auto itIn = inlineMap.find(propName);
  if (itIn != inlineMap.end()) {
    if (outValue) *outValue = itIn->second;
    return true;
  }

  const CssRule* winner = winningRuleForProperty(propName, className, id, elementTagLower);
  if (!winner) {
    return false;
  }
  if (outValue) {
    const std::string* value = rulePropertyValue(*winner, propName);
    *outValue = value ? *value : std::string();
  }
  if (outSourcePath) {
    *outSourcePath =
        winner->sourcePathIndex < sourcePaths_.size() ? sourcePaths_[winner->sourcePathIndex] : std::string();
  }
  return true;
}

int CssParser::mapTextAlignToStyleIndex(const std::string& rawValue) const {
  std::string s = trim(rawValue);
  const size_t cut = s.find_first_of(" \t\r\n;");
  if (cut != std::string::npos) {
    s = trim(s.substr(0, cut));
  }
  s = toLower(s);
  if (s.empty() || s == "inherit" || s == "initial" || s == "unset" || s == "revert") {
    return -2;
  }
  if (s == "justify" || s == "inter-word" || s == "distribute") {
    return 0;
  }
  if (s == "left" || s == "start") {
    return 1;
  }
  if (s == "center") {
    return 2;
  }
  if (s == "right" || s == "end") {
    return 3;
  }
  return -1;
}

uint8_t CssParser::computeParagraphAlignment(const std::string& className, const std::string& id,
                                             const std::string& styleAttr, const std::string& elementTagLower) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  const auto inlineIt = inlineMap.find("text-align");
  if (inlineIt != inlineMap.end()) {
    const int m = mapTextAlignToStyleIndex(inlineIt->second);
    if (m >= 0 && m <= 3) {
      return static_cast<uint8_t>(m);
    }
  }

  // Only a plain selector (or inline, handled above) sets a block's own text-align — combinator selectors like
  // ".box p" are unverifiable here, so they are ignored and the block inherits its ancestor's alignment instead.
  const CssRule* w = winningRuleForProperty("text-align", className, id, elementTagLower, /*ignoreContextual=*/true);
  if (w) {
    const std::string* textAlign = rulePropertyValue(*w, "text-align");
    const int m = textAlign ? mapTextAlignToStyleIndex(*textAlign) : -1;
    if (m >= 0 && m <= 3) {
      return static_cast<uint8_t>(m);
    }
  }

  if (!bodyTextAlignRaw.empty()) {
    const int m = mapTextAlignToStyleIndex(bodyTextAlignRaw);
    if (m >= 0 && m <= 3) {
      return static_cast<uint8_t>(m);
    }
  }

  return 1;
}

bool CssParser::hasTextAlignSpecified(const std::string& elementTagLower, const std::string& className,
                                      const std::string& id, const std::string& styleAttr) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  if (inlineMap.find("text-align") != inlineMap.end()) {
    return true;
  }
  // Match computeParagraphAlignment: a combinator selector does not count as the element's own alignment.
  return winningRuleForProperty("text-align", className, id, elementTagLower, /*ignoreContextual=*/true) != nullptr;
}

bool CssParser::isDisplayBlock(const std::string& elementTagLower, const std::string& className, const std::string& id,
                               const std::string& styleAttr) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  std::string raw;
  const auto inlineDisplayIt = inlineMap.find("display");
  if (inlineDisplayIt != inlineMap.end()) {
    raw = inlineDisplayIt->second;
  } else {
    raw = getCascadedPropertyValue("display", className, id, styleAttr, elementTagLower);
  }

  raw = trimCssWs(raw);
  std::transform(raw.begin(), raw.end(), raw.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return raw == "block";
}

bool CssParser::isDisplayInlineBlock(const std::string& elementTagLower, const std::string& className,
                                     const std::string& id, const std::string& styleAttr) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  std::string raw;
  const auto inlineDisplayIt = inlineMap.find("display");
  if (inlineDisplayIt != inlineMap.end()) {
    raw = inlineDisplayIt->second;
  } else {
    raw = getCascadedPropertyValue("display", className, id, styleAttr, elementTagLower);
  }

  raw = trimCssWs(raw);
  std::transform(raw.begin(), raw.end(), raw.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return raw == "inline-block";
}

bool CssParser::isDisplayNone(const std::string& elementTagLower, const std::string& className, const std::string& id,
                              const std::string& styleAttr) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  std::string raw;
  const auto inlineDisplayIt = inlineMap.find("display");
  if (inlineDisplayIt != inlineMap.end()) {
    raw = inlineDisplayIt->second;
  } else {
    raw = getCascadedPropertyValue("display", className, id, styleAttr, elementTagLower);
  }

  raw = trimCssWs(raw);
  std::transform(raw.begin(), raw.end(), raw.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return raw == "none";
}

bool CssParser::hasPropertySpecified(const std::string& propName, const std::string& className, const std::string& id,
                                     const std::string& styleAttr, const std::string& elementTagLower) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  if (inlineMap.find(propName) != inlineMap.end()) {
    return true;
  }
  const std::string sheet = getCascadedPropertyValue(propName, className, id, styleAttr, elementTagLower);
  return !sheet.empty();
}

bool CssParser::resolveFontBold(const std::string& elementTagLower, const std::string& className, const std::string& id,
                                const std::string& styleAttr, const bool inheritedBold) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  auto mapWeight = [](std::string raw, bool inherited) {
    raw = trimCssWs(raw);
    const size_t cut = raw.find_first_of(" \t\r\n;");
    if (cut != std::string::npos) {
      raw = trimCssWs(raw.substr(0, cut));
    }
    std::transform(raw.begin(), raw.end(), raw.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (raw.empty() || raw == "inherit") {
      return inherited;
    }
    if (raw == "normal" || raw == "lighter" || raw == "400") {
      return false;
    }
    if (raw == "bold" || raw == "bolder") {
      return true;
    }
    const int weight = std::atoi(raw.c_str());
    if (weight > 0) {
      return weight >= 600;
    }
    return inherited;
  };

  const auto inlineIt = inlineMap.find("font-weight");
  if (inlineIt != inlineMap.end()) {
    return mapWeight(inlineIt->second, inheritedBold);
  }
  const std::string sheet = getCascadedPropertyValue("font-weight", className, id, styleAttr, elementTagLower);
  if (!sheet.empty()) {
    return mapWeight(sheet, inheritedBold);
  }
  return inheritedBold;
}

bool CssParser::resolveFontItalic(const std::string& elementTagLower, const std::string& className,
                                  const std::string& id, const std::string& styleAttr,
                                  const bool inheritedItalic) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  auto mapStyle = [](std::string raw, bool inherited) {
    raw = trimCssWs(raw);
    const size_t cut = raw.find_first_of(" \t\r\n;");
    if (cut != std::string::npos) {
      raw = trimCssWs(raw.substr(0, cut));
    }
    std::transform(raw.begin(), raw.end(), raw.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (raw.empty() || raw == "inherit") {
      return inherited;
    }
    if (raw == "italic" || raw == "oblique") {
      return true;
    }
    if (raw == "normal") {
      return false;
    }
    return inherited;
  };

  const auto inlineIt = inlineMap.find("font-style");
  if (inlineIt != inlineMap.end()) {
    return mapStyle(inlineIt->second, inheritedItalic);
  }
  const std::string sheet = getCascadedPropertyValue("font-style", className, id, styleAttr, elementTagLower);
  if (!sheet.empty()) {
    return mapStyle(sheet, inheritedItalic);
  }
  return inheritedItalic;
}

bool CssParser::resolveSmallCaps(const std::string& elementTagLower, const std::string& className,
                                 const std::string& id, const std::string& styleAttr,
                                 const bool inheritedSmallCaps) const {
  auto mapVariant = [](std::string raw, const bool inherited) {
    raw = trimCssWs(raw);
    std::transform(raw.begin(), raw.end(), raw.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (raw.empty() || raw == "inherit") {
      return inherited;
    }
    if (raw.find("small-caps") != std::string::npos) {
      return true;
    }
    if (raw == "normal" || raw == "initial" || raw == "unset" || raw == "revert" || raw == "revert-layer" ||
        raw.find(" normal") != std::string::npos || raw.find("normal ") != std::string::npos ||
        raw.find("all-normal") != std::string::npos) {
      return false;
    }
    return inherited;
  };

  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  const auto inlineCapsIt = inlineMap.find("font-variant-caps");
  if (inlineCapsIt != inlineMap.end()) {
    return mapVariant(inlineCapsIt->second, inheritedSmallCaps);
  }
  const auto inlineVariantIt = inlineMap.find("font-variant");
  if (inlineVariantIt != inlineMap.end()) {
    return mapVariant(inlineVariantIt->second, inheritedSmallCaps);
  }

  const std::string sheetCaps =
      getCascadedPropertyValue("font-variant-caps", className, id, styleAttr, elementTagLower);
  if (!sheetCaps.empty()) {
    return mapVariant(sheetCaps, inheritedSmallCaps);
  }
  const std::string sheetVariant = getCascadedPropertyValue("font-variant", className, id, styleAttr, elementTagLower);
  if (!sheetVariant.empty()) {
    return mapVariant(sheetVariant, inheritedSmallCaps);
  }

  return inheritedSmallCaps;
}

bool CssParser::hasFirstLetterDropCapHint(const std::string& elementTagLower, const std::string& className,
                                          const std::string& id, const std::string& styleAttr) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  const auto inlineInitialLetter = inlineMap.find("initial-letter");
  if (inlineInitialLetter != inlineMap.end() && !trimCssWs(inlineInitialLetter->second).empty()) {
    return true;
  }

  std::string idLower = toLower(trim(id));
  std::vector<std::string> classTokens;
  splitClassTokens(className, classTokens);
  for (auto& t : classTokens) {
    t = toLower(trim(t));
  }

  for (const auto& rule : rules) {
    if (!rule.isFirstLetterPseudo) {
      continue;
    }
    if (!matchSelectorList(rule.selectorLower, elementTagLower, classTokens, idLower, true).matched) {
      continue;
    }
    if (ruleHasProperty(rule, "initial-letter")) {
      return true;
    }
    if (ruleHasProperty(rule, "font-size")) {
      return true;
    }
    if (ruleHasProperty(rule, "line-height")) {
      return true;
    }
    if (ruleHasProperty(rule, "font-weight")) {
      return true;
    }
    if (ruleHasProperty(rule, "font-style")) {
      return true;
    }
  }
  return false;
}

uint8_t CssParser::getFirstLetterDropCapLineCount(const std::string& elementTagLower, const std::string& className,
                                                  const std::string& id, const std::string& styleAttr) const {
  auto parseInitialLetterValue = [](std::string raw) -> uint8_t {
    raw = trimCssWs(raw);
    if (raw.empty()) {
      return 0;
    }
    const size_t cut = raw.find_first_of(" \t\r\n/;");
    if (cut != std::string::npos) {
      raw = trimCssWs(raw.substr(0, cut));
    }
    char* end = nullptr;
    const long value = std::strtol(raw.c_str(), &end, 10);
    if (end == raw.c_str() || value < 1 || value > 9) {
      return 0;
    }
    return static_cast<uint8_t>(value);
  };

  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  const auto inlineInitialLetter = inlineMap.find("initial-letter");
  if (inlineInitialLetter != inlineMap.end()) {
    const uint8_t parsed = parseInitialLetterValue(inlineInitialLetter->second);
    if (parsed != 0) {
      return parsed;
    }
  }

  std::string idLower = toLower(trim(id));
  std::vector<std::string> classTokens;
  splitClassTokens(className, classTokens);
  for (auto& t : classTokens) {
    t = toLower(trim(t));
  }

  for (const auto& rule : rules) {
    if (!rule.isFirstLetterPseudo) {
      continue;
    }
    if (!matchSelectorList(rule.selectorLower, elementTagLower, classTokens, idLower, true).matched) {
      continue;
    }
    const std::string* initialLetter = rulePropertyValue(rule, "initial-letter");
    if (!initialLetter) {
      continue;
    }
    const uint8_t parsed = parseInitialLetterValue(*initialLetter);
    if (parsed != 0) {
      return parsed;
    }
  }

  return 3;
}

int CssParser::getTextIndentPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                               const std::string& styleAttr, int viewportWidth, int viewportHeight) const {
  auto firstToken = [](std::string v) {
    v = trimCssWs(v);
    const size_t sp = v.find_first_of(" \t");
    if (sp != std::string::npos) {
      v = trimCssWs(v.substr(0, sp));
    }
    return v;
  };

  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  const auto inlineIt = inlineMap.find("text-indent");
  if (inlineIt != inlineMap.end()) {
    const std::string tok = firstToken(inlineIt->second);
    return std::max(0, parseCssLength(tok, viewportWidth, viewportHeight, true));
  }

  const std::string sheet = getCascadedPropertyValue("text-indent", className, id, styleAttr, elementTagLower);
  if (!sheet.empty()) {
    const std::string tok = firstToken(sheet);
    const int px = parseCssLength(tok, viewportWidth, viewportHeight, true);
    return std::max(0, px);
  }
  return 0;
}

bool CssParser::hasTextIndentSpecified(const std::string& elementTagLower, const std::string& className,
                                       const std::string& id, const std::string& styleAttr) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  if (inlineMap.find("text-indent") != inlineMap.end()) {
    return true;
  }
  const std::string sheet = getCascadedPropertyValue("text-indent", className, id, styleAttr, elementTagLower);
  return !sheet.empty();
}

int CssParser::getParagraphSpacingTopPx(const std::string& elementTagLower, const std::string& className,
                                        const std::string& id, const std::string& styleAttr, const int viewportWidth,
                                        const int viewportHeight) const {
  (void)elementTagLower;
  const int marginTop =
      getSpacingEdgePx("margin-top", "margin", className, id, styleAttr, viewportWidth, viewportHeight);
  const int paddingTop =
      getSpacingEdgePx("padding-top", "padding", className, id, styleAttr, viewportWidth, viewportHeight);
  return marginTop + paddingTop;
}

int CssParser::getParagraphSpacingBottomPx(const std::string& elementTagLower, const std::string& className,
                                           const std::string& id, const std::string& styleAttr, const int viewportWidth,
                                           const int viewportHeight) const {
  (void)elementTagLower;
  const int marginBottom =
      getSpacingEdgePx("margin-bottom", "margin", className, id, styleAttr, viewportWidth, viewportHeight);
  const int paddingBottom =
      getSpacingEdgePx("padding-bottom", "padding", className, id, styleAttr, viewportWidth, viewportHeight);
  return marginBottom + paddingBottom;
}

int CssParser::getMarginTopPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                              const std::string& styleAttr, const int viewportWidth, const int viewportHeight) const {
  return getSpacingEdgePx("margin-top", "margin", className, id, styleAttr, viewportWidth, viewportHeight,
                          elementTagLower);
}

int CssParser::getMarginBottomPx(const std::string& elementTagLower, const std::string& className,
                                 const std::string& id, const std::string& styleAttr, const int viewportWidth,
                                 const int viewportHeight) const {
  return getSpacingEdgePx("margin-bottom", "margin", className, id, styleAttr, viewportWidth, viewportHeight,
                          elementTagLower);
}

int CssParser::getMarginLeftPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                               const std::string& styleAttr, const int viewportWidth, const int viewportHeight) const {
  return getSpacingEdgePx("margin-left", "margin", className, id, styleAttr, viewportWidth, viewportHeight,
                          elementTagLower);
}

int CssParser::getMarginRightPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                                const std::string& styleAttr, const int viewportWidth, const int viewportHeight) const {
  return getSpacingEdgePx("margin-right", "margin", className, id, styleAttr, viewportWidth, viewportHeight,
                          elementTagLower);
}

int CssParser::getPaddingTopPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                               const std::string& styleAttr, const int viewportWidth, const int viewportHeight) const {
  return std::max(0, getSpacingEdgePx("padding-top", "padding", className, id, styleAttr, viewportWidth, viewportHeight,
                                      elementTagLower));
}

int CssParser::getPaddingBottomPx(const std::string& elementTagLower, const std::string& className,
                                  const std::string& id, const std::string& styleAttr, const int viewportWidth,
                                  const int viewportHeight) const {
  return std::max(0, getSpacingEdgePx("padding-bottom", "padding", className, id, styleAttr, viewportWidth,
                                      viewportHeight, elementTagLower));
}

int CssParser::getPaddingLeftPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                                const std::string& styleAttr, const int viewportWidth, const int viewportHeight) const {
  return std::max(0, getSpacingEdgePx("padding-left", "padding", className, id, styleAttr, viewportWidth,
                                      viewportHeight, elementTagLower));
}

int CssParser::getPaddingRightPx(const std::string& elementTagLower, const std::string& className,
                                 const std::string& id, const std::string& styleAttr, const int viewportWidth,
                                 const int viewportHeight) const {
  return std::max(0, getSpacingEdgePx("padding-right", "padding", className, id, styleAttr, viewportWidth,
                                      viewportHeight, elementTagLower));
}

bool CssParser::isFloatLeft(const std::string& elementTagLower, const std::string& className, const std::string& id,
                            const std::string& styleAttr) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  std::string raw;
  const auto inlineFloatIt = inlineMap.find("float");
  if (inlineFloatIt != inlineMap.end()) {
    raw = inlineFloatIt->second;
  } else {
    raw = getCascadedPropertyValue("float", className, id, styleAttr, elementTagLower);
  }

  raw = trimCssWs(raw);
  std::transform(raw.begin(), raw.end(), raw.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return raw == "left";
}

bool CssParser::hasHorizontalSpacingSpecified(const std::string& elementTagLower, const std::string& className,
                                              const std::string& id, const std::string& styleAttr) const {
  return hasPropertySpecified("margin-left", className, id, styleAttr, elementTagLower) ||
         hasPropertySpecified("margin-right", className, id, styleAttr, elementTagLower) ||
         hasPropertySpecified("margin", className, id, styleAttr, elementTagLower) ||
         hasPropertySpecified("padding-left", className, id, styleAttr, elementTagLower) ||
         hasPropertySpecified("padding-right", className, id, styleAttr, elementTagLower) ||
         hasPropertySpecified("padding", className, id, styleAttr, elementTagLower);
}

int CssParser::getBorderTopPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                              const std::string& styleAttr, const int viewportWidth, const int viewportHeight) const {
  return getBorderEdgePx("border-top", className, id, styleAttr, viewportWidth, viewportHeight, elementTagLower);
}

int CssParser::getBorderRightPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                                const std::string& styleAttr, const int viewportWidth, const int viewportHeight) const {
  return getBorderEdgePx("border-right", className, id, styleAttr, viewportWidth, viewportHeight, elementTagLower);
}

int CssParser::getBorderBottomPx(const std::string& elementTagLower, const std::string& className,
                                 const std::string& id, const std::string& styleAttr, const int viewportWidth,
                                 const int viewportHeight) const {
  return getBorderEdgePx("border-bottom", className, id, styleAttr, viewportWidth, viewportHeight, elementTagLower);
}

int CssParser::getBorderLeftPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                               const std::string& styleAttr, const int viewportWidth, const int viewportHeight) const {
  return getBorderEdgePx("border-left", className, id, styleAttr, viewportWidth, viewportHeight, elementTagLower);
}

bool CssParser::hasParagraphSpacingSpecified(const std::string& elementTagLower, const std::string& className,
                                             const std::string& id, const std::string& styleAttr) const {
  (void)elementTagLower;
  return hasPropertySpecified("margin-top", className, id, styleAttr) ||
         hasPropertySpecified("margin-bottom", className, id, styleAttr) ||
         hasPropertySpecified("margin", className, id, styleAttr) ||
         hasPropertySpecified("padding-top", className, id, styleAttr) ||
         hasPropertySpecified("padding-bottom", className, id, styleAttr) ||
         hasPropertySpecified("padding", className, id, styleAttr);
}

bool CssParser::hasBorderSpecified(const std::string& elementTagLower, const std::string& className,
                                   const std::string& id, const std::string& styleAttr) const {
  return hasPropertySpecified("border", className, id, styleAttr, elementTagLower) ||
         hasPropertySpecified("border-top", className, id, styleAttr, elementTagLower) ||
         hasPropertySpecified("border-right", className, id, styleAttr, elementTagLower) ||
         hasPropertySpecified("border-bottom", className, id, styleAttr, elementTagLower) ||
         hasPropertySpecified("border-left", className, id, styleAttr, elementTagLower) ||
         hasPropertySpecified("border-width", className, id, styleAttr, elementTagLower) ||
         hasPropertySpecified("border-style", className, id, styleAttr, elementTagLower);
}

bool CssParser::hasListStyleSpecified(const std::string& elementTagLower, const std::string& className,
                                      const std::string& id, const std::string& styleAttr) const {
  return hasPropertySpecified("list-style-type", className, id, styleAttr, elementTagLower) ||
         hasPropertySpecified("list-style", className, id, styleAttr, elementTagLower);
}

bool CssParser::isListStyleNone(const std::string& elementTagLower, const std::string& className,
                                const std::string& id, const std::string& styleAttr) const {
  const std::string type = getCascadedPropertyValue("list-style-type", className, id, styleAttr, elementTagLower);
  if (!type.empty()) {
    return toLower(trimCssWs(type)) == "none";
  }
  // "list-style" is a shorthand for <type> <position> <image> in any order (e.g. "none" or "square inside").
  const std::string shorthand = getCascadedPropertyValue("list-style", className, id, styleAttr, elementTagLower);
  for (const auto& tok : splitCssWhitespaceList(trimCssWs(shorthand))) {
    if (toLower(tok) == "none") {
      return true;
    }
  }
  return false;
}

bool CssParser::isPageBreakBeforeAlways(const std::string& elementTagLower, const std::string& className,
                                        const std::string& id, const std::string& styleAttr) const {
  const std::string value = getCascadedPropertyValue("page-break-before", className, id, styleAttr, elementTagLower);
  return toLower(trimCssWs(value)) == "always";
}

bool CssParser::isPageBreakAfterAlways(const std::string& elementTagLower, const std::string& className,
                                       const std::string& id, const std::string& styleAttr) const {
  const std::string value = getCascadedPropertyValue("page-break-after", className, id, styleAttr, elementTagLower);
  return toLower(trimCssWs(value)) == "always";
}

std::string CssParser::getBackgroundImagePath(const std::string& elementTagLower, const std::string& className,
                                              const std::string& id, const std::string& styleAttr,
                                              const std::string& currentFilePath) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  auto resolveUrl = [&](const std::string& raw, const std::string& sourcePath) -> std::string {
    const std::string url = extractCssUrl(raw);
    if (url.empty()) {
      return {};
    }
    const std::string base = sourcePath.empty() ? currentFilePath : sourcePath;
    return FsHelpers::resolveRelativePath(base, url);
  };

  const auto inlineBgImage = inlineMap.find("background-image");
  if (inlineBgImage != inlineMap.end()) {
    return resolveUrl(inlineBgImage->second, currentFilePath);
  }
  const auto inlineBg = inlineMap.find("background");
  if (inlineBg != inlineMap.end()) {
    return resolveUrl(inlineBg->second, currentFilePath);
  }

  std::string raw;
  std::string sourcePath;
  if (getCascadedPropertyValueAndSource("background-image", className, id, styleAttr, elementTagLower, &raw,
                                        &sourcePath)) {
    const std::string resolved = resolveUrl(raw, sourcePath);
    if (!resolved.empty()) {
      return resolved;
    }
  }
  raw.clear();
  sourcePath.clear();
  if (getCascadedPropertyValueAndSource("background", className, id, styleAttr, elementTagLower, &raw, &sourcePath)) {
    return resolveUrl(raw, sourcePath);
  }
  return {};
}

std::string CssParser::trim(const std::string& str) const {
  size_t start = 0;
  while (start < str.length() && isspace(static_cast<unsigned char>(str[start]))) {
    start++;
  }

  size_t end = str.length();
  while (end > start && isspace(static_cast<unsigned char>(str[end - 1]))) {
    end--;
  }

  return str.substr(start, end - start);
}

std::string CssParser::toLower(const std::string& str) const {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}
