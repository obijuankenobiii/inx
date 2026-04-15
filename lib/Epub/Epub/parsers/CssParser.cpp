#include "CssParser.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <sstream>

/**
 * Constructor
 */
CssParser::CssParser() {}

/**
 * Destructor
 */
CssParser::~CssParser() { clear(); }

/**
 * Clears all parsed rules
 */
void CssParser::clear() { rules.clear(); }

/**
 * Parses CSS content - simplified version that only looks for width/height
 */
void CssParser::parse(const std::string& cssContent) {
  // Skip if content is too large
  if (cssContent.length() > 50 * 1024) {
    Serial.printf("[CSSP] Skipping large CSS content (%d bytes)\n", (int)cssContent.length());
    return;
  }

  size_t pos = 0;
  size_t len = cssContent.length();

  while (pos < len) {
    // Skip whitespace
    while (pos < len &&
           (cssContent[pos] == ' ' || cssContent[pos] == '\n' || cssContent[pos] == '\r' || cssContent[pos] == '\t')) {
      pos++;
    }

    if (pos >= len) break;

    // Skip comments
    if (pos + 1 < len && cssContent[pos] == '/' && cssContent[pos + 1] == '*') {
      pos += 2;
      while (pos + 1 < len && !(cssContent[pos] == '*' && cssContent[pos + 1] == '/')) {
        pos++;
      }
      pos += 2;
      continue;
    }

    // Find selector
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

    // Skip empty selectors
    if (selector.empty()) {
      pos++;
      continue;
    }

    // Find properties block
    pos++;  // Skip '{'
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

    // Only store this rule if it has width or height properties
    CssRule rule;
    rule.selector = selector;

    // Parse properties - only care about width/height/max-width/max-height
    parsePropertiesForDimensions(propertiesStr, rule.properties);

    // Only add rule if it has relevant properties
    if (!rule.properties.empty()) {
      // Limit number of rules to prevent memory issues
      if (rules.size() < 100) {
        rules.push_back(rule);
      } else {
        Serial.printf("[CSSP] Reached max rules limit (100)\n");
        break;
      }
    }
  }

  Serial.printf("[CSSP] Parsed %zu CSS rules\n", rules.size());
}

/**
 * Parse properties but only extract width/height related ones
 */
void CssParser::parsePropertiesForDimensions(const std::string& propertiesStr,
                                             std::map<std::string, std::string>& properties) const {
  size_t pos = 0;
  size_t len = propertiesStr.length();

  while (pos < len) {
    // Skip whitespace
    while (pos < len && isspace(propertiesStr[pos])) {
      pos++;
    }

    if (pos >= len) break;

    // Find property name
    size_t nameStart = pos;
    while (pos < len && propertiesStr[pos] != ':') {
      pos++;
    }

    if (pos >= len) break;

    std::string propName = propertiesStr.substr(nameStart, pos - nameStart);
    propName = trim(toLower(propName));

    // Only care about width/height related properties
    if (propName != "width" && propName != "height" && propName != "max-width" && propName != "max-height") {
      // Skip to next property
      while (pos < len && propertiesStr[pos] != ';') {
        pos++;
      }
      pos++;  // Skip ';'
      continue;
    }

    pos++;  // Skip ':'

    // Skip whitespace after colon
    while (pos < len && isspace(propertiesStr[pos])) {
      pos++;
    }

    // Find property value
    size_t valueStart = pos;
    while (pos < len && propertiesStr[pos] != ';') {
      pos++;
    }

    std::string propValue = propertiesStr.substr(valueStart, pos - valueStart);
    propValue = trim(propValue);

    // Remove !important
    size_t importantPos = propValue.find("!important");
    if (importantPos != std::string::npos) {
      propValue = propValue.substr(0, importantPos);
      propValue = trim(propValue);
    }

    if (!propName.empty() && !propValue.empty()) {
      properties[propName] = propValue;
    }

    pos++;  // Skip ';'
  }
}

int CssParser::getWidth(const std::string& className, const std::string& id, 
                        const std::string& styleAttr, int viewportWidth) const {
  // Check inline style first
  if (!styleAttr.empty()) {
    int width = extractWidthFromInlineStyle(styleAttr);
    if (width > 0) return width;
    // Check if it's a percentage
    if (styleAttr.find("width:") != std::string::npos && styleAttr.find("%") != std::string::npos) {
      return 0; // Return 0 for percentage, let caller handle
    }
  }

  // Check CSS rules for class
  if (!className.empty()) {
    for (const auto& rule : rules) {
      if (rule.selector.find("." + className) != std::string::npos) {
        auto it = rule.properties.find("width");
        if (it != rule.properties.end()) {
          // Check if value contains %
          if (it->second.find("%") != std::string::npos) {
            return 0; // Percentage - return 0 to trigger aspect ratio
          }
          return parseDimensionValue(it->second, viewportWidth, 0);
        }
        it = rule.properties.find("max-width");
        if (it != rule.properties.end()) {
          if (it->second.find("%") != std::string::npos) {
            return 0;
          }
          return parseDimensionValue(it->second, viewportWidth, 0);
        }
      }
    }
  }

  return 0;
}

int CssParser::getHeight(const std::string& className, const std::string& id, 
                         const std::string& styleAttr, int viewportHeight) const {
  // Check inline style first
  if (!styleAttr.empty()) {
    int height = extractHeightFromInlineStyle(styleAttr);
    if (height > 0) return height;
    // Check if it's a percentage
    if (styleAttr.find("height:") != std::string::npos && styleAttr.find("%") != std::string::npos) {
      return 0; // Return 0 for percentage, let caller handle
    }
  }

  // Check CSS rules for class
  if (!className.empty()) {
    for (const auto& rule : rules) {
      if (rule.selector.find("." + className) != std::string::npos) {
        auto it = rule.properties.find("height");
        if (it != rule.properties.end()) {
          if (it->second.find("%") != std::string::npos) {
            return 0;
          }
          return parseDimensionValue(it->second, 0, viewportHeight);
        }
        it = rule.properties.find("max-height");
        if (it != rule.properties.end()) {
          if (it->second.find("%") != std::string::npos) {
            return 0;
          }
          return parseDimensionValue(it->second, 0, viewportHeight);
        }
      }
    }
  }

  return 0;
}

/**
 * Extract width from inline style
 */
int CssParser::extractWidthFromInlineStyle(const std::string& styleAttr) const {
  size_t widthPos = styleAttr.find("width:");
  if (widthPos != std::string::npos) {
    size_t start = widthPos + 6;
    size_t end = styleAttr.find(";", start);
    if (end == std::string::npos) end = styleAttr.length();
    std::string widthStr = styleAttr.substr(start, end - start);
    return parseDimensionValue(widthStr, 0, 0);
  }
  return 0;
}

/**
 * Extract height from inline style
 */
int CssParser::extractHeightFromInlineStyle(const std::string& styleAttr) const {
  size_t heightPos = styleAttr.find("height:");
  if (heightPos != std::string::npos) {
    size_t start = heightPos + 7;
    size_t end = styleAttr.find(";", start);
    if (end == std::string::npos) end = styleAttr.length();
    std::string heightStr = styleAttr.substr(start, end - start);
    return parseDimensionValue(heightStr, 0, 0);
  }
  return 0;
}

/**
 * Parse dimension value (e.g., "100px", "50%")
 */
int CssParser::parseDimensionValue(const std::string& value, int viewportWidth, int viewportHeight) const {
  if (value.empty()) return 0;

  std::string numStr;
  std::string unit;
  bool foundDigit = false;

  for (char c : value) {
    if (std::isdigit(c) || c == '.' || (c == '-' && !foundDigit)) {
      numStr += c;
      foundDigit = true;
    } else if (foundDigit && (std::isalpha(c) || c == '%')) {
      unit += c;
    } else {
      break;
    }
  }

  if (numStr.empty()) return 0;

  float num = std::stof(numStr);

  // Default base font size for em/rem conversion (typically 16px)
  const int BASE_FONT_SIZE = 16;

  if (unit == "px" || unit.empty()) {
    return static_cast<int>(num);
  } else if (unit == "em" || unit == "rem") {
    return static_cast<int>(num * BASE_FONT_SIZE);
  } else if (unit == "%") {
    return 0;
  } else if (unit == "vw" && viewportWidth > 0) {
    return static_cast<int>(num * viewportWidth / 100.0f);
  } else if (unit == "vh" && viewportHeight > 0) {
    return static_cast<int>(num * viewportHeight / 100.0f);
  }

  return static_cast<int>(num);
}

/**
 * Trims whitespace
 */
std::string CssParser::trim(const std::string& str) const {
  size_t start = 0;
  while (start < str.length() && isspace(str[start])) {
    start++;
  }

  size_t end = str.length();
  while (end > start && isspace(str[end - 1])) {
    end--;
  }

  return str.substr(start, end - start);
}

/**
 * Converts to lowercase
 */
std::string CssParser::toLower(const std::string& str) const {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}

/**
 * Parse CSS file (stub)
 */
void CssParser::parseFile(const std::string& filepath) {
  // Not implemented - files are parsed through parse() method
}

/**
 * Get combined properties (simplified)
 */
std::map<std::string, std::string> CssParser::getCombinedPropertiesForElement(const std::string& elementName,
                                                                              const std::string& className,
                                                                              const std::string& id) const {
  std::map<std::string, std::string> result;

  if (!className.empty()) {
    for (const auto& rule : rules) {
      if (rule.selector.find("." + className) != std::string::npos) {
        for (const auto& prop : rule.properties) {
          result[prop.first] = prop.second;
        }
      }
    }
  }

  return result;
}