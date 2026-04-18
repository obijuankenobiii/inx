#include "CssParser.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace {

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

/** Last compound selector in a clause (e.g. "div > p" -> "p"). Input should be lowercased. */
std::string lastCompoundInClauseLower(const std::string& clauseLower) {
  std::string s = trimCssWs(clauseLower);
  if (s.empty()) {
    return s;
  }
  const size_t pseudoPos = s.find("::");
  if (pseudoPos != std::string::npos) {
    s = trimCssWs(s.substr(0, pseudoPos));
  }
  const size_t pos = s.find_last_of(">+~\t\n\r ");
  if (pos == std::string::npos || pos + 1 >= s.size()) {
    return s;
  }
  return trimCssWs(s.substr(pos + 1));
}

/** Type selector prefix of a compound (e.g. "p.foo" -> "p"). Empty if universal/class/id only. */
std::string typeNameFromCompoundLower(const std::string& compoundLower) {
  std::string s = trimCssWs(compoundLower);
  if (s.empty()) {
    return {};
  }
  const char c0 = s[0];
  if (c0 == '.' || c0 == '#' || c0 == '[' || c0 == '*' || c0 == '+' || c0 == '~' || c0 == '>') {
    return {};
  }
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char uc = static_cast<unsigned char>(s[i]);
    if (std::isalpha(uc) != 0 || s[i] == '_' || s[i] == '-' || std::isdigit(uc) != 0) {
      ++i;
      continue;
    }
    break;
  }
  if (i == 0) {
    return {};
  }
  return s.substr(0, i);
}

bool selectorListMatchesElementType(const std::string& fullSelectorLower, const std::string& elementTagLower) {
  if (elementTagLower.empty()) {
    return false;
  }
  size_t start = 0;
  while (start < fullSelectorLower.size()) {
    const size_t comma = fullSelectorLower.find(',', start);
    const std::string clause = trimCssWs(fullSelectorLower.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start));
    const std::string last = lastCompoundInClauseLower(clause);
    const std::string tag = typeNameFromCompoundLower(last);
    if (!tag.empty() && tag == elementTagLower) {
      return true;
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return false;
}

}  // namespace

/** Parsed rules kept for EPUB image width/height only (~300KB SRAM). */
static constexpr size_t kMaxParsedCssRules = 16;

CssParser::CssParser() {}

CssParser::~CssParser() { clear(); }

void CssParser::clear() {
  rules.clear();
  bodyTextAlignRaw.clear();
}

void CssParser::shrinkStorage() {
  for (CssRule& r : rules) {
    r.selector.shrink_to_fit();
    for (auto& kv : r.properties) {
      kv.second.shrink_to_fit();
    }
  }
  rules.shrink_to_fit();
  bodyTextAlignRaw.shrink_to_fit();
}

void CssParser::parse(const std::string& cssContent) {
  if (cssContent.length() > 16 * 1024) {
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

    if (selector[0] == '@') {
      if (pos < len && cssContent[pos] == '{') {
        pos++;
        int braceDepth = 1;
        inString = false;
        while (pos < len && braceDepth > 0) {
          const char c = cssContent[pos];
          if (!inString && (c == '"' || c == '\'')) {
            inString = true;
            stringChar = c;
          } else if (inString && c == stringChar) {
            inString = false;
          } else if (!inString) {
            if (c == '{') {
              braceDepth++;
            } else if (c == '}') {
              braceDepth--;
            }
          }
          pos++;
        }
      } else {
        while (pos < len && cssContent[pos] != ';') {
          pos++;
        }
        if (pos < len) {
          pos++;
        }
      }
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
    rule.selector = selector;

    parsePropertiesForDimensions(propertiesStr, rule.properties);

    if (!rule.properties.empty()) {
      noteBodyHtmlTextAlign(selector, rule.properties);
      if (rules.size() < kMaxParsedCssRules) {
        rules.push_back(std::move(rule));
      } else {
        Serial.printf("[CSSP] Reached max rules limit (%zu)\n", kMaxParsedCssRules);
        break;
      }
    }
  }

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

    static const char* kSheetProps[] = {"width", "height"};
    bool wanted = false;
    for (const char* p : kSheetProps) {
      if (propName == p) {
        wanted = true;
        break;
      }
    }
    if (!wanted) {
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
      properties[propName] = propValue;
    }

    if (pos < len) pos++;
  }
}

bool CssParser::selectorHasIdToken(const std::string& selectorLower, const std::string& idLower) {
  if (idLower.empty()) return false;
  const std::string needle = "#" + idLower;
  size_t pos = 0;
  while ((pos = selectorLower.find(needle, pos)) != std::string::npos) {
    size_t after = pos + needle.size();
    if (after >= selectorLower.size()) return true;
    unsigned char c = static_cast<unsigned char>(selectorLower[after]);
    if (!isIdentCont(c)) return true;
    pos = after;
  }
  return false;
}

bool CssParser::selectorHasClassToken(const std::string& selectorLower, const std::string& classTokenLower) {
  if (classTokenLower.empty()) return false;
  const std::string needle = "." + classTokenLower;
  size_t pos = 0;
  while ((pos = selectorLower.find(needle, pos)) != std::string::npos) {
    size_t after = pos + needle.size();
    if (after >= selectorLower.size()) return true;
    unsigned char c = static_cast<unsigned char>(selectorLower[after]);
    if (!isIdentCont(c)) return true;
    pos = after;
  }
  return false;
}

bool CssParser::ruleMatchesElement(const CssRule& rule, const std::string& classAttr, const std::string& idAttr) const {
  std::string selLower = rule.selector;
  std::transform(selLower.begin(), selLower.end(), selLower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  std::string idKey = trim(idAttr);
  std::transform(idKey.begin(), idKey.end(), idKey.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (!idKey.empty() && selectorHasIdToken(selLower, idKey)) {
    return true;
  }

  std::vector<std::string> tokens;
  splitClassTokens(classAttr, tokens);
  for (const auto& tok : tokens) {
    std::string tl = trim(tok);
    std::transform(tl.begin(), tl.end(), tl.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!tl.empty() && selectorHasClassToken(selLower, tl)) {
      return true;
    }
  }
  return false;
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

  float num = 0.f;
  try {
    num = std::stof(numStr);
  } catch (...) {
    return 0;
  }

  constexpr int BASE_FONT_SIZE = 16;
  constexpr float PX_PER_IN = 96.f;
  constexpr float PX_PER_PT = PX_PER_IN / 72.f;
  constexpr float PX_PER_CM = PX_PER_IN / 2.54f;
  constexpr float PX_PER_MM = PX_PER_IN / 25.4f;

  if (unit == "px" || unit.empty()) {
    return static_cast<int>(num + (num >= 0 ? 0.5f : -0.5f));
  }
  if (unit == "em" || unit == "rem") {
    return static_cast<int>(num * BASE_FONT_SIZE + (num >= 0 ? 0.5f : -0.5f));
  }
  if (unit == "%") {
    if (percentAxis == PercentRefersTo::Width && viewportWidth > 0) {
      return static_cast<int>(num * viewportWidth / 100.0f + (num >= 0 ? 0.5f : -0.5f));
    }
    if (percentAxis == PercentRefersTo::Height && viewportHeight > 0) {
      return static_cast<int>(num * viewportHeight / 100.0f + (num >= 0 ? 0.5f : -0.5f));
    }
    return 0;
  }
  if (unit == "vw" && viewportWidth > 0) {
    return static_cast<int>(num * viewportWidth / 100.0f + 0.5f);
  }
  if (unit == "vh" && viewportHeight > 0) {
    return static_cast<int>(num * viewportHeight / 100.0f + 0.5f);
  }
  if (unit == "vmin" && viewportWidth > 0 && viewportHeight > 0) {
    const int mn = std::min(viewportWidth, viewportHeight);
    return static_cast<int>(num * mn / 100.0f + 0.5f);
  }
  if (unit == "vmax" && viewportWidth > 0 && viewportHeight > 0) {
    const int mx = std::max(viewportWidth, viewportHeight);
    return static_cast<int>(num * mx / 100.0f + 0.5f);
  }
  if (unit == "pt") {
    return static_cast<int>(num * PX_PER_PT + (num >= 0 ? 0.5f : -0.5f));
  }
  if (unit == "in") {
    return static_cast<int>(num * PX_PER_IN + (num >= 0 ? 0.5f : -0.5f));
  }
  if (unit == "cm") {
    return static_cast<int>(num * PX_PER_CM + (num >= 0 ? 0.5f : -0.5f));
  }
  if (unit == "mm") {
    return static_cast<int>(num * PX_PER_MM + (num >= 0 ? 0.5f : -0.5f));
  }

  return static_cast<int>(num + (num >= 0 ? 0.5f : -0.5f));
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

  std::map<std::string, std::string> idLast;
  std::map<std::string, std::string> clsLast;

  std::string idLower = toLower(trim(id));
  std::vector<std::string> classTokens;
  splitClassTokens(className, classTokens);
  for (auto& t : classTokens) {
    t = toLower(trim(t));
  }

  for (const auto& rule : rules) {
    std::string selLower = rule.selector;
    std::transform(selLower.begin(), selLower.end(), selLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const bool idMatch = !idLower.empty() && selectorHasIdToken(selLower, idLower);
    bool classMatch = false;
    if (!idMatch) {
      for (const auto& tok : classTokens) {
        if (!tok.empty() && selectorHasClassToken(selLower, tok)) {
          classMatch = true;
          break;
        }
      }
    }

    auto pit = rule.properties.find(propName);
    if (pit == rule.properties.end()) {
      continue;
    }

    if (idMatch) {
      idLast[propName] = pit->second;
    } else if (classMatch) {
      clsLast[propName] = pit->second;
    }
  }

  auto jt = idLast.find(propName);
  if (jt != idLast.end()) {
    return parseDimensionValue(jt->second, viewportWidth, viewportHeight, pct);
  }
  auto kt = clsLast.find(propName);
  if (kt != clsLast.end()) {
    return parseDimensionValue(kt->second, viewportWidth, viewportHeight, pct);
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
    const std::string part =
        trim(selectorRaw.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
    const std::string pl = toLower(part);
    const std::string last = lastCompoundInClauseLower(pl);
    const std::string tag = typeNameFromCompoundLower(last);
    if (pl == "body" || pl == "html" || tag == "body" || tag == "html") {
      bodyTextAlignRaw = val;
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
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

  std::map<std::string, std::string> idLast;
  std::map<std::string, std::string> clsLast;
  std::map<std::string, std::string> typeLast;

  std::string idLower = toLower(trim(id));
  std::vector<std::string> classTokens;
  splitClassTokens(className, classTokens);
  for (auto& t : classTokens) {
    t = toLower(trim(t));
  }

  for (const auto& rule : rules) {
    std::string selLower = rule.selector;
    std::transform(selLower.begin(), selLower.end(), selLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const bool idMatch = !idLower.empty() && selectorHasIdToken(selLower, idLower);
    bool classMatch = false;
    if (!idMatch) {
      for (const auto& tok : classTokens) {
        if (!tok.empty() && selectorHasClassToken(selLower, tok)) {
          classMatch = true;
          break;
        }
      }
    }

    const bool tagMatch = !elementTagLower.empty() && selectorListMatchesElementType(selLower, elementTagLower);

    const auto pit = rule.properties.find(propName);
    if (pit == rule.properties.end()) {
      continue;
    }

    if (idMatch) {
      idLast[propName] = pit->second;
    } else if (classMatch) {
      clsLast[propName] = pit->second;
    } else if (tagMatch) {
      typeLast[propName] = pit->second;
    }
  }

  const auto jt = idLast.find(propName);
  if (jt != idLast.end()) {
    return jt->second;
  }
  const auto kt = clsLast.find(propName);
  if (kt != clsLast.end()) {
    return kt->second;
  }
  const auto tt = typeLast.find(propName);
  if (tt != typeLast.end()) {
    return tt->second;
  }
  return "";
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

  return 1;  // LEFT when CSS gives no usable hint
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

void CssParser::parseFile(const std::string& filepath) { (void)filepath; }

std::map<std::string, std::string> CssParser::getCombinedPropertiesForElement(const std::string& elementName,
                                                                              const std::string& className,
                                                                              const std::string& id) const {
  std::map<std::string, std::string> result;
  (void)elementName;
  if (className.empty() && id.empty()) return result;

  for (const auto& rule : rules) {
    if (ruleMatchesElement(rule, className, id)) {
      for (const auto& prop : rule.properties) {
        result[prop.first] = prop.second;
      }
    }
  }
  return result;
}
