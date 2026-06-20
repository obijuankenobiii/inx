/**
 * @file CssParser.cpp
 * @brief Definitions for CssParser.
 */

#include "CssParser.h"

#include "CssTrackedProperties.h"

#include <FsHelpers.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace {

constexpr size_t kMaxCssRules = 200;

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
      const bool clauseFirstLetter = clauseStr.find(":first-letter") != std::string::npos ||
                                     clauseStr.find("::first-letter") != std::string::npos;
      if (!requireFirstLetterPseudo || clauseFirstLetter) {
        SelectorMatchInfo clauseInfo;
        if (compoundMatchesElement(lastCompoundView(clause), elementTagLower, classTokensLower, idLower, &clauseInfo)) {
          clauseInfo.firstLetter = clauseFirstLetter;
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

}  

CssParser::CssParser() {}

CssParser::~CssParser() { clear(); }

void CssParser::clear() {
  rules.clear();
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

void CssParser::parse(const std::string& cssContent, const std::string& sourcePath) {
  if (cssContent.length() > 50 * 1024) {
    Serial.printf("[CSSP] Skipping large CSS content (%d bytes)\n", (int)cssContent.length());
    return;
  }

  size_t pos = 0;
  size_t len = cssContent.length();

  while (pos < len) {
    while (pos < len && (cssContent[pos] == ' ' || cssContent[pos] == '\n' || cssContent[pos] == '\r' ||
                         cssContent[pos] == '\t')) {
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

    std::string propertiesStr = cssContent.substr(blockStart, pos - blockStart - 1);

    CssRule rule;
    rule.selectorLower = toLower(selector);

    parsePropertiesForDimensions(propertiesStr, rule.properties);

    if (!rule.properties.empty()) {
      noteBodyHtmlTextAlign(selector, rule.properties);
      if (rules.size() < kMaxCssRules) {
        rule.sourcePathIndex = internSourcePath(sourcePath);
        rule.isPseudoElement = selectorTargetsPseudoElement(rule.selectorLower);
        rule.isFirstLetterPseudo = selectorTargetsFirstLetterPseudo(rule.selectorLower);
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
}

void CssParser::parsePropertiesForDimensions(const std::string& propertiesStr,
                                             std::map<std::string, std::string>& properties) const {
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
        properties["width"] = propValue;
      } else if (propName == "block-size") {
        properties["height"] = propValue;
      } else if (propName == "max-inline-size") {
        properties["max-width"] = propValue;
      } else if (propName == "min-inline-size") {
        properties["min-width"] = propValue;
      } else if (propName == "max-block-size") {
        properties["max-height"] = propValue;
      } else if (propName == "min-block-size") {
        properties["min-height"] = propValue;
      } else {
        properties[propName] = propValue;
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
static int clampCssPixels(const int v) {
  constexpr int kMaxCssPx = 8192;
  if (v < 0) return 0;
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

  if (unit == "px" || unit.empty()) {
    return clampCssPixels(static_cast<int>(num + (num >= 0 ? 0.5f : -0.5f)));
  }
  if (unit == "em" || unit == "rem") {
    return clampCssPixels(static_cast<int>(num * BASE_FONT_SIZE + (num >= 0 ? 0.5f : -0.5f)));
  }
  if (unit == "%") {
    if (percentAxis == PercentRefersTo::Width && viewportWidth > 0) {
      return clampCssPixels(static_cast<int>(num * viewportWidth / 100.0f + (num >= 0 ? 0.5f : -0.5f)));
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
    return clampCssPixels(static_cast<int>(num * PX_PER_PT + (num >= 0 ? 0.5f : -0.5f)));
  }
  if (unit == "in") {
    return clampCssPixels(static_cast<int>(num * PX_PER_IN + (num >= 0 ? 0.5f : -0.5f)));
  }
  if (unit == "cm") {
    return clampCssPixels(static_cast<int>(num * PX_PER_CM + (num >= 0 ? 0.5f : -0.5f)));
  }
  if (unit == "mm") {
    return clampCssPixels(static_cast<int>(num * PX_PER_MM + (num >= 0 ? 0.5f : -0.5f)));
  }

  return clampCssPixels(static_cast<int>(num + (num >= 0 ? 0.5f : -0.5f)));
}

int CssParser::getInlineOrSheetLength(const std::string& propName, const std::string& className, const std::string& id,
                                      const std::string& styleAttr, int viewportWidth, int viewportHeight) const {
  const PercentRefersTo pct =
      (propName == "height" || propName == "min-height" || propName == "max-height") ? PercentRefersTo::Height
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
    const SelectorMatchInfo matchInfo =
        matchSelectorList(rule.selectorLower, "", classTokens, idLower, false);
    if (!matchInfo.matched) {
      continue;
    }

    const auto pit = rule.properties.find(propName);
    if (pit == rule.properties.end()) {
      continue;
    }

    if (matchInfo.hasId) {
      idLast = pit->second;
      hasIdLast = true;
    } else if (matchInfo.hasClass) {
      clsLast = pit->second;
      hasClsLast = true;
    } else if (matchInfo.hasType) {
      typeLast = pit->second;
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
                                int viewportWidth, int viewportHeight) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  auto resolveEdge = [&](const std::string& raw, bool topEdge) -> int {
    const auto tokens = splitCssWhitespaceList(trimCssWs(raw));
    if (tokens.empty()) {
      return 0;
    }
    if (tokens.size() == 1) {
      return parseCssLength(tokens[0], viewportWidth, viewportHeight, true);
    }
    if (tokens.size() == 2) {
      return parseCssLength(topEdge ? tokens[0] : tokens[0], viewportWidth, viewportHeight, true);
    }
    if (tokens.size() == 3) {
      return parseCssLength(topEdge ? tokens[0] : tokens[2], viewportWidth, viewportHeight, true);
    }
    return parseCssLength(topEdge ? tokens[0] : tokens[2], viewportWidth, viewportHeight, true);
  };

  const auto itIn = inlineMap.find(propName);
  if (itIn != inlineMap.end()) {
    return std::max(0, resolveEdge(itIn->second, propName.find("-top") != std::string::npos));
  }

  const auto shorthandIn = inlineMap.find(shorthandName);
  if (shorthandIn != inlineMap.end()) {
    return std::max(0, resolveEdge(shorthandIn->second, propName.find("-top") != std::string::npos));
  }

  const std::string direct = getCascadedPropertyValue(propName, className, id, styleAttr, "");
  if (!direct.empty()) {
    return std::max(0, resolveEdge(direct, propName.find("-top") != std::string::npos));
  }

  const std::string shorthand = getCascadedPropertyValue(shorthandName, className, id, styleAttr, "");
  if (!shorthand.empty()) {
    return std::max(0, resolveEdge(shorthand, propName.find("-top") != std::string::npos));
  }

  return 0;
}

int CssParser::getBorderEdgePx(const std::string& edgePropName, const std::string& className, const std::string& id,
                               const std::string& styleAttr, int viewportWidth, int viewportHeight) const {
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

  auto parseBorderWidthShorthand = [&](std::string raw, bool topEdge) -> int {
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
    if (tokens.size() == 2) {
      return std::max(0, tokenPx(tokens[0]));
    }
    if (tokens.size() == 3) {
      return std::max(0, tokenPx(topEdge ? tokens[0] : tokens[2]));
    }
    return std::max(0, tokenPx(topEdge ? tokens[0] : tokens[2]));
  };

  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  const bool topEdge = edgePropName.find("-top") != std::string::npos;

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
    return parseBorderWidthShorthand(borderWidthInlineIt->second, topEdge);
  }

  const std::string edgeSheet = getCascadedPropertyValue(edgePropName, className, id, styleAttr, "");
  if (!edgeSheet.empty()) {
    return parseBorderWidthToken(edgeSheet);
  }
  const std::string borderSheet = getCascadedPropertyValue("border", className, id, styleAttr, "");
  if (!borderSheet.empty()) {
    return parseBorderWidthToken(borderSheet);
  }
  const std::string borderWidthSheet = getCascadedPropertyValue("border-width", className, id, styleAttr, "");
  if (!borderWidthSheet.empty()) {
    return parseBorderWidthShorthand(borderWidthSheet, topEdge);
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

void CssParser::noteBodyHtmlTextAlign(const std::string& selectorRaw,
                                      const std::map<std::string, std::string>& props) {
  const auto it = props.find("text-align");
  if (it == props.end()) {
    return;
  }
  const std::string val = trim(it->second);
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
    const SelectorMatchInfo matchInfo = matchSelectorList(rule.selectorLower, elementTagLower, classTokens, idLower, false);
    if (!matchInfo.matched) {
      continue;
    }
    // Universal-only matches (no id/class/type) never contributed a cascaded value, so skip them.
    if (matchInfo.hasId) {
      mcMatched_.push_back({&rule, 2});
    } else if (matchInfo.hasClass) {
      mcMatched_.push_back({&rule, 1});
    } else if (matchInfo.hasType) {
      mcMatched_.push_back({&rule, 0});
    }
  }
  mcValid_ = true;
  return mcMatched_;
}

std::string CssParser::getCascadedPropertyValue(const std::string& propName, const std::string& className,
                                                const std::string& id, const std::string& styleAttr,
                                                const std::string& elementTagLower) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);

  const auto itIn = inlineMap.find(propName);
  if (itIn != inlineMap.end()) {
    return itIn->second;
  }

  std::string idLast;
  std::string clsLast;
  std::string typeLast;
  bool hasIdLast = false;
  bool hasClsLast = false;
  bool hasTypeLast = false;

  for (const auto& m : matchedRulesFor(elementTagLower, className, id)) {
    const auto pit = m.rule->properties.find(propName);
    if (pit == m.rule->properties.end()) {
      continue;
    }
    if (m.tier == 2) {
      idLast = pit->second;
      hasIdLast = true;
    } else if (m.tier == 1) {
      clsLast = pit->second;
      hasClsLast = true;
    } else {
      typeLast = pit->second;
      hasTypeLast = true;
    }
  }

  if (hasIdLast) {
    return idLast;
  }
  if (hasClsLast) {
    return clsLast;
  }
  if (hasTypeLast) {
    return typeLast;
  }
  return "";
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

  const CssRule* idRule = nullptr;
  const CssRule* clsRule = nullptr;
  const CssRule* typeRule = nullptr;

  std::string idLower = toLower(trim(id));
  std::vector<std::string> classTokens;
  splitClassTokens(className, classTokens);
  for (auto& t : classTokens) {
    t = toLower(trim(t));
  }

  for (const auto& rule : rules) {
    const std::string& selLower = rule.selectorLower;
    if (rule.isPseudoElement) {
      continue;
    }
    const auto pit = rule.properties.find(propName);
    if (pit == rule.properties.end()) {
      continue;
    }

    const SelectorMatchInfo matchInfo =
        matchSelectorList(selLower, elementTagLower, classTokens, idLower, false);
    if (!matchInfo.matched) {
      continue;
    }

    if (matchInfo.hasId) {
      idRule = &rule;
    } else if (matchInfo.hasClass) {
      clsRule = &rule;
    } else if (matchInfo.hasType) {
      typeRule = &rule;
    }
  }

  const CssRule* winner = idRule ? idRule : (clsRule ? clsRule : typeRule);
  if (!winner) {
    return false;
  }
  const auto it = winner->properties.find(propName);
  if (it == winner->properties.end()) {
    return false;
  }
  if (outValue) *outValue = it->second;
  if (outSourcePath) {
    *outSourcePath = winner->sourcePathIndex < sourcePaths_.size() ? sourcePaths_[winner->sourcePathIndex]
                                                                   : std::string();
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
                                             const std::string& styleAttr,
                                             const std::string& elementTagLower) const {
  std::map<std::string, std::string> inlineMap;
  parseInlineStyle(styleAttr, inlineMap);
  const auto inlineIt = inlineMap.find("text-align");
  if (inlineIt != inlineMap.end()) {
    const int m = mapTextAlignToStyleIndex(inlineIt->second);
    if (m >= 0 && m <= 3) {
      return static_cast<uint8_t>(m);
    }
  }

  const std::string sheet =
      getCascadedPropertyValue("text-align", className, id, styleAttr, elementTagLower);
  if (!sheet.empty()) {
    const int m = mapTextAlignToStyleIndex(sheet);
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
  const std::string sheet = getCascadedPropertyValue("text-align", className, id, styleAttr, elementTagLower);
  return !sheet.empty();
}

bool CssParser::isDisplayBlock(const std::string& elementTagLower, const std::string& className,
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
  return raw == "block";
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

  const std::string sheetCaps = getCascadedPropertyValue("font-variant-caps", className, id, styleAttr, elementTagLower);
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
    if (rule.properties.find("initial-letter") != rule.properties.end()) {
      return true;
    }
    if (rule.properties.find("font-size") != rule.properties.end()) {
      return true;
    }
    if (rule.properties.find("line-height") != rule.properties.end()) {
      return true;
    }
    if (rule.properties.find("font-weight") != rule.properties.end()) {
      return true;
    }
    if (rule.properties.find("font-style") != rule.properties.end()) {
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
    const auto it = rule.properties.find("initial-letter");
    if (it == rule.properties.end()) {
      continue;
    }
    const uint8_t parsed = parseInitialLetterValue(it->second);
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
  const int marginTop = getSpacingEdgePx("margin-top", "margin", className, id, styleAttr, viewportWidth, viewportHeight);
  const int paddingTop =
      getSpacingEdgePx("padding-top", "padding", className, id, styleAttr, viewportWidth, viewportHeight);
  return marginTop + paddingTop;
}

int CssParser::getParagraphSpacingBottomPx(const std::string& elementTagLower, const std::string& className,
                                           const std::string& id, const std::string& styleAttr,
                                           const int viewportWidth, const int viewportHeight) const {
  (void)elementTagLower;
  const int marginBottom =
      getSpacingEdgePx("margin-bottom", "margin", className, id, styleAttr, viewportWidth, viewportHeight);
  const int paddingBottom =
      getSpacingEdgePx("padding-bottom", "padding", className, id, styleAttr, viewportWidth, viewportHeight);
  return marginBottom + paddingBottom;
}

int CssParser::getMarginTopPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                              const std::string& styleAttr, const int viewportWidth,
                              const int viewportHeight) const {
  (void)elementTagLower;
  return getSpacingEdgePx("margin-top", "margin", className, id, styleAttr, viewportWidth, viewportHeight);
}

int CssParser::getMarginBottomPx(const std::string& elementTagLower, const std::string& className,
                                 const std::string& id, const std::string& styleAttr, const int viewportWidth,
                                 const int viewportHeight) const {
  (void)elementTagLower;
  return getSpacingEdgePx("margin-bottom", "margin", className, id, styleAttr, viewportWidth, viewportHeight);
}

int CssParser::getPaddingTopPx(const std::string& elementTagLower, const std::string& className,
                               const std::string& id, const std::string& styleAttr, const int viewportWidth,
                               const int viewportHeight) const {
  (void)elementTagLower;
  return getSpacingEdgePx("padding-top", "padding", className, id, styleAttr, viewportWidth, viewportHeight);
}

int CssParser::getPaddingBottomPx(const std::string& elementTagLower, const std::string& className,
                                  const std::string& id, const std::string& styleAttr, const int viewportWidth,
                                  const int viewportHeight) const {
  (void)elementTagLower;
  return getSpacingEdgePx("padding-bottom", "padding", className, id, styleAttr, viewportWidth, viewportHeight);
}

int CssParser::getBorderTopPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                              const std::string& styleAttr, const int viewportWidth,
                              const int viewportHeight) const {
  (void)elementTagLower;
  return getBorderEdgePx("border-top", className, id, styleAttr, viewportWidth, viewportHeight);
}

int CssParser::getBorderBottomPx(const std::string& elementTagLower, const std::string& className,
                                 const std::string& id, const std::string& styleAttr, const int viewportWidth,
                                 const int viewportHeight) const {
  (void)elementTagLower;
  return getBorderEdgePx("border-bottom", className, id, styleAttr, viewportWidth, viewportHeight);
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
  if (getCascadedPropertyValueAndSource("background", className, id, styleAttr, elementTagLower, &raw,
                                        &sourcePath)) {
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
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}
