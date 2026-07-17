/**
 * @file ChapterHtmlSlimParser.cpp
 * @brief Definitions for ChapterHtmlSlimParser.
 */

#include "ChapterHtmlSlimParser.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <ImageRender.h>
#include <SDCardManager.h>
#include <Utf8.h>
#include <expat.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <exception>
#include <vector>

#include "../../../../src/util/StringUtils.h"
#include "../../../KOReaderSync/htmlEntities.h"
#include "../Page.h"
#include "JpegToBmpConverter.h"

const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr int NUM_HEADER_TAGS = sizeof(HEADER_TAGS) / sizeof(HEADER_TAGS[0]);

constexpr size_t MIN_SIZE_FOR_POPUP = 30 * 1024;
constexpr size_t STREAMING_TEXTBLOCK_WORD_LIMIT = 80;

namespace {

bool hasJpegExt(const std::string& path) {
  return StringUtils::checkFileExtension(path, ".jpg") || StringUtils::checkFileExtension(path, ".jpeg");
}

bool hasPngExt(const std::string& path) { return StringUtils::checkFileExtension(path, ".png"); }

bool containsAsciiInsensitive(const std::string& haystack, const char* needle) {
  if (needle == nullptr || *needle == '\0') {
    return true;
  }
  const size_t needleLen = std::strlen(needle);
  if (haystack.size() < needleLen) {
    return false;
  }
  for (size_t i = 0; i + needleLen <= haystack.size(); ++i) {
    size_t j = 0;
    while (j < needleLen) {
      const unsigned char hc = static_cast<unsigned char>(haystack[i + j]);
      const unsigned char nc = static_cast<unsigned char>(needle[j]);
      if (std::tolower(hc) != std::tolower(nc)) {
        break;
      }
      ++j;
    }
    if (j == needleLen) {
      return true;
    }
  }
  return false;
}

bool equalsAsciiInsensitive(const char* lhs, const char* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return lhs == rhs;
  }
  while (*lhs != '\0' && *rhs != '\0') {
    const unsigned char l = static_cast<unsigned char>(*lhs);
    const unsigned char r = static_cast<unsigned char>(*rhs);
    if (std::tolower(l) != std::tolower(r)) {
      return false;
    }
    ++lhs;
    ++rhs;
  }
  return *lhs == '\0' && *rhs == '\0';
}

bool startsWithAsciiInsensitive(const std::string& value, const char* prefix) {
  if (prefix == nullptr) {
    return true;
  }
  const size_t n = std::strlen(prefix);
  if (value.size() < n) {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    const unsigned char v = static_cast<unsigned char>(value[i]);
    const unsigned char p = static_cast<unsigned char>(prefix[i]);
    if (std::tolower(v) != std::tolower(p)) {
      return false;
    }
  }
  return true;
}

bool hasClassToken(const std::string& classAttr, const char* token) {
  size_t i = 0;
  while (i < classAttr.size()) {
    while (i < classAttr.size() && std::isspace(static_cast<unsigned char>(classAttr[i])) != 0) {
      ++i;
    }
    const size_t start = i;
    while (i < classAttr.size() && std::isspace(static_cast<unsigned char>(classAttr[i])) == 0) {
      ++i;
    }
    if (start < i && equalsAsciiInsensitive(classAttr.substr(start, i - start).c_str(), token)) {
      return true;
    }
  }
  return false;
}

bool hasClassTokenPrefix(const std::string& classAttr, const char* prefix) {
  size_t i = 0;
  while (i < classAttr.size()) {
    while (i < classAttr.size() && std::isspace(static_cast<unsigned char>(classAttr[i])) != 0) {
      ++i;
    }
    const size_t start = i;
    while (i < classAttr.size() && std::isspace(static_cast<unsigned char>(classAttr[i])) == 0) {
      ++i;
    }
    if (start < i && startsWithAsciiInsensitive(classAttr.substr(start, i - start), prefix)) {
      return true;
    }
  }
  return false;
}

bool classAttrsShareToken(const std::string& a, const std::string& b) {
  size_t i = 0;
  while (i < a.size()) {
    while (i < a.size() && std::isspace(static_cast<unsigned char>(a[i])) != 0) {
      ++i;
    }
    const size_t start = i;
    while (i < a.size() && std::isspace(static_cast<unsigned char>(a[i])) == 0) {
      ++i;
    }
    if (start >= i) {
      continue;
    }
    const std::string token = a.substr(start, i - start);
    size_t j = 0;
    while (j < b.size()) {
      while (j < b.size() && std::isspace(static_cast<unsigned char>(b[j])) != 0) {
        ++j;
      }
      const size_t bStart = j;
      while (j < b.size() && std::isspace(static_cast<unsigned char>(b[j])) == 0) {
        ++j;
      }
      if (bStart < j && equalsAsciiInsensitive(b.substr(bStart, j - bStart).c_str(), token.c_str())) {
        return true;
      }
    }
  }
  return false;
}

bool isAsciiLower(const uint32_t cp) { return cp >= 'a' && cp <= 'z'; }

bool isAsciiAlpha(const uint32_t cp) { return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'); }

uint32_t toAsciiUpper(const uint32_t cp) { return isAsciiLower(cp) ? (cp - ('a' - 'A')) : cp; }

void appendUtf8Codepoint(std::string& out, const uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

int utf8CodepointByteLength(const unsigned char lead) {
  if ((lead & 0x80) == 0x00) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

bool endsWithCompleteUtf8Codepoint(const char* s, int byteLen) {
  if (s == nullptr || byteLen <= 0) {
    return false;
  }
  int start = byteLen - 1;
  while (start > 0 && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) {
    --start;
  }
  const int expected = utf8CodepointByteLength(static_cast<unsigned char>(s[start]));
  return (byteLen - start) >= expected;
}

bool hasDropCapHint(const std::string& classAttr, const std::string& idAttr, const std::string& styleAttr) {
  return containsAsciiInsensitive(idAttr, "drop") || containsAsciiInsensitive(classAttr, "drop") || containsAsciiInsensitive(classAttr, "dropcap") || containsAsciiInsensitive(classAttr, "drop-cap") ||
         containsAsciiInsensitive(classAttr, "initial-letter") || containsAsciiInsensitive(idAttr, "dropcap") ||
         containsAsciiInsensitive(idAttr, "drop-cap") || containsAsciiInsensitive(idAttr, "initial-letter") ||
         containsAsciiInsensitive(styleAttr, "initial-letter");
}

bool hasExplicitSmallCapsHint(const char* tagName, const std::string& classAttr, const std::string& idAttr,
                              const std::string& styleAttr) {
  if (tagName == nullptr || std::strcmp(tagName, "span") != 0) {
    return false;
  }
  return containsAsciiInsensitive(classAttr, "small") || containsAsciiInsensitive(idAttr, "small") || 
          containsAsciiInsensitive(classAttr, "small_caps") || containsAsciiInsensitive(idAttr, "small_caps") || 
          containsAsciiInsensitive(classAttr, "smallcaps") || containsAsciiInsensitive(classAttr, "small-caps") ||
         containsAsciiInsensitive(idAttr, "smallcaps") || containsAsciiInsensitive(idAttr, "small-caps") ||
         containsAsciiInsensitive(styleAttr, "small-caps");
}

uint8_t detectDropCapLineCount(const std::string& classAttr, const std::string& idAttr, const std::string& styleAttr) {
  auto parseSource = [](const std::string& src) -> uint8_t {
    for (size_t i = 0; i < src.size(); ++i) {
      if (std::isdigit(static_cast<unsigned char>(src[i])) == 0) {
        continue;
      }
      size_t j = i;
      while (j < src.size() && std::isdigit(static_cast<unsigned char>(src[j])) != 0) {
        ++j;
      }
      if (j < src.size() && containsAsciiInsensitive(src.substr(j), "line")) {
        const int value = std::atoi(src.substr(i, j - i).c_str());
        if (value >= 1 && value <= 9) {
          return static_cast<uint8_t>(value);
        }
      }
      if (i >= 4 && containsAsciiInsensitive(src.substr(i - 4, 4), "line")) {
        const int value = std::atoi(src.substr(i, j - i).c_str());
        if (value >= 1 && value <= 9) {
          return static_cast<uint8_t>(value);
        }
      }
    }
    return 0;
  };

  uint8_t v = parseSource(classAttr);
  if (v != 0) return v;
  v = parseSource(idAttr);
  if (v != 0) return v;
  v = parseSource(styleAttr);
  if (v != 0) return v;
  return 3;
}

int countUtf8Codepoints(const char* s, int byteLen) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  const unsigned char* const end = p + static_cast<size_t>(byteLen);
  int n = 0;
  while (p < end) {
    utf8NextCodepoint(&p);
    ++n;
  }
  return n;
}

bool isLeadingDropCapPunctuation(const uint32_t cp) {
  switch (cp) {
    case '"':
    case '\'':
    case '(':
    case '[':
    case '{':
    case 0x00AB:
    case 0x00BB:
    case 0x2018:
    case 0x2019:
    case 0x201A:
    case 0x201B:
    case 0x201C:
    case 0x201D:
    case 0x201E:
    case 0x201F:
      return true;
    default:
      return false;
  }
}

int desiredDropCapCodepoints(const char* s, int byteLen, const bool consumeWholeContainer) {
  if (consumeWholeContainer) {
    return countUtf8Codepoints(s, byteLen);
  }
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  const unsigned char* const end = p + static_cast<size_t>(byteLen);
  if (p >= end) {
    return 1;
  }
  const uint32_t first = utf8NextCodepoint(&p);
  return isLeadingDropCapPunctuation(first) ? 2 : 1;
}

std::string uppercaseSingleLetterDropCap(const char* s, const int byteLen) {
  std::string out;
  out.reserve(static_cast<size_t>(byteLen));
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  const unsigned char* const end = p + static_cast<size_t>(byteLen);
  int alphaCount = 0;
  while (p < end) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (isAsciiAlpha(cp)) {
      ++alphaCount;
    }
  }

  p = reinterpret_cast<const unsigned char*>(s);
  while (p < end) {
    const uint32_t cp = utf8NextCodepoint(&p);
    appendUtf8Codepoint(out, alphaCount == 1 ? toAsciiUpper(cp) : cp);
  }
  return out;
}

}  // namespace

const char* BLOCK_TAGS[] = {"p", "li", "ol", "ul", "div", "section", "nav", "br", "blockquote", "tr", "table"};
constexpr int NUM_BLOCK_TAGS = sizeof(BLOCK_TAGS) / sizeof(BLOCK_TAGS[0]);

// <a> links render bold (and underlined, see underlineUntilDepth).
const char* BOLD_TAGS[] = {"b", "strong"};
constexpr int NUM_BOLD_TAGS = sizeof(BOLD_TAGS) / sizeof(BOLD_TAGS[0]);

const char* ITALIC_TAGS[] = {"i", "em"};
constexpr int NUM_ITALIC_TAGS = sizeof(ITALIC_TAGS) / sizeof(ITALIC_TAGS[0]);

const char* IMAGE_TAGS[] = {"img"};
constexpr int NUM_IMAGE_TAGS = sizeof(IMAGE_TAGS) / sizeof(IMAGE_TAGS[0]);

const char* SKIP_TAGS[] = {"head"};
constexpr int NUM_SKIP_TAGS = sizeof(SKIP_TAGS) / sizeof(SKIP_TAGS[0]);

/**
 * Determines if a character is whitespace.
 *
 * @param c The character to check
 * @return true if the character is space, carriage return, newline, or tab
 */
bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

/**
 * Checks if a tag name matches any tag in a list of possible tags.
 *
 * @param tag_name The tag name to check
 * @param possible_tags Array of possible tag names
 * @param possible_tag_count Number of tags in the array
 * @return true if the tag name matches any tag in the list
 */
bool matches(const char* tag_name, const char* possible_tags[], const int possible_tag_count) {
  for (int i = 0; i < possible_tag_count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) return true;
  }
  return false;
}

std::string trimAsciiWs(const std::string& in) {
  size_t start = 0;
  while (start < in.size() && std::isspace(static_cast<unsigned char>(in[start]))) {
    ++start;
  }
  size_t end = in.size();
  while (end > start && std::isspace(static_cast<unsigned char>(in[end - 1]))) {
    --end;
  }
  return in.substr(start, end - start);
}

// Lower-cases the element tag and pulls out the class/id/style attribute values used for CSS matching.
void extractSelectorAttributes(const XML_Char* name, const XML_Char** atts, std::string& tagLower,
                               std::string& classAttr, std::string& idAttr, std::string& styleAttr) {
  tagLower.clear();
  classAttr.clear();
  idAttr.clear();
  styleAttr.clear();
  if (name != nullptr) {
    for (const XML_Char* p = name; *p; ++p) {
      tagLower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
  }
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "class") == 0) {
        classAttr = atts[i + 1];
      } else if (strcmp(atts[i], "id") == 0) {
        idAttr = atts[i + 1];
      } else if (strcmp(atts[i], "style") == 0) {
        styleAttr = atts[i + 1];
      }
    }
  }
}

bool hasAmazonRemovedFallbackAttr(const XML_Char** atts) {
  if (atts == nullptr) {
    return false;
  }
  for (int i = 0; atts[i]; i += 2) {
    if (containsAsciiInsensitive(atts[i], "amznremoved")) {
      return true;
    }
  }
  return false;
}

bool isKnownHiddenFallbackImageClass(const std::string& classAttr) {
  return hasClassToken(classAttr, "imagefix") || hasClassToken(classAttr, "imagefix-arabic") ||
         hasClassTokenPrefix(classAttr, "imagefix-greek") || hasClassTokenPrefix(classAttr, "imagefix-japanese");
}

/**
 * Loads all CSS rules from the EPUB cache using CssParser
 */
void ChapterHtmlSlimParser::resetStructuralStateForParsePass() {
  depth = 0;
  skipUntilDepth = INT_MAX;
  boldUntilDepth = INT_MAX;
  italicUntilDepth = INT_MAX;
  underlineUntilDepth = INT_MAX;
  superscriptUntilDepth = INT_MAX;
  subscriptUntilDepth = INT_MAX;
  inHeader = false;
  inDropCap = false;
  dropCapDepth = INT_MAX;
  dropCapConsumeWholeContainer = false;
  dropCapLineCount = 3;
  partWordBufferIndex = 0;
  nextWordJoinsPrevious = false;
  currentTextBlock.reset();
  currentPage.reset();
  currentPageNextY = 0;
  currentTextBlockContentX = 0;
  currentTextBlockContentWidth = std::max(1, static_cast<int>(viewportWidth));
  cssAlignmentStack.clear();
  cssAlignmentDepths.clear();
  cssFontStyleStack.clear();
  smallCapsStack.clear();
  smallCapsDepths.clear();
  cssHorizontalInsetStack.clear();
  cssBorderBoxStack.clear();
  currentCssInsetLeftPx = 0;
  currentCssInsetRightPx = 0;
  currentBlockBottomSpacingPx = 0;
  currentBlockSpacingFromCss = false;
  currentBlockMarginBottomPx = 0;
  currentBlockPaddingBottomPx = 0;
  currentBlockBorderTopPx = 0;
  currentBlockBorderBottomPx = 0;
  currentBlockBorderLeftPx = 0;
  currentBlockBorderRightPx = 0;
  currentBlockBorderTopStyle = 0;
  currentBlockBorderBottomStyle = 0;
  currentBlockBorderLeftStyle = 0;
  currentBlockBorderRightStyle = 0;
  currentBlockUsesBorderBox = false;
  currentBlockBorderBoxX = 0;
  currentBlockBorderBoxY = 0;
  currentBlockBorderBoxW = 0;
  pendingTopBorderElem_.reset();
  pendingBorderBoxElem_.reset();
  inTable_ = false;
  tableShowBorders_ = false;
  tableDepth_ = INT_MAX;
  tableRowDepth_ = INT_MAX;
  tableCellDepth_ = INT_MAX;
  tableLastWasSpace_ = true;
  tableRows_.clear();
  currentTableRow_.clear();
  currentTableCell_.reset();
}

void ChapterHtmlSlimParser::prefetchImageFromImgAttributes(const XML_Char** atts) {
  if (hasAmazonRemovedFallbackAttr(atts)) {
    return;
  }

  std::string src;
  std::string classAttr;
  std::string idAttr;
  std::string styleAttr;
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      std::string attrName = atts[i];
      if (attrName == "src" || attrName == "href" || attrName == "xlink:href") {
        src = atts[i + 1];
      } else if (attrName == "class") {
        classAttr = atts[i + 1];
      } else if (attrName == "id") {
        idAttr = atts[i + 1];
      } else if (attrName == "style") {
        styleAttr = atts[i + 1];
      }
    }
  }
  if (src.empty()) {
    return;
  }
  if (isKnownHiddenFallbackImageClass(classAttr)) {
    return;
  }
  loadCssRules();
  if (css().isDisplayNone("img", classAttr, idAttr, styleAttr)) {
    return;
  }
  const std::string& base = internalPath.empty() ? filepath : internalPath;
  const std::string fullInternalPath = FsHelpers::resolveRelativePath(base, src);
  const std::string cacheImgPath = epub.getCacheImgPath(fullInternalPath);
  int w = 0;
  int h = 0;
  ensureImageCached(fullInternalPath, cacheImgPath, &w, &h);
}

bool ChapterHtmlSlimParser::parseHtmlThroughExpat(const bool callProgressPopup) {
  const XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    Serial.printf("[%lu] [SCT] Expat parser allocation failed chapter=%s internal=%s heap=%u\n", millis(),
                  filepath.c_str(), internalPath.c_str(), static_cast<unsigned>(ESP.getFreeHeap()));
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForRead("EHP", filepath, file)) {
    Serial.printf("[%lu] [SCT] Failed to open chapter temp HTML path=%s internal=%s heap=%u\n", millis(),
                  filepath.c_str(), internalPath.c_str(), static_cast<unsigned>(ESP.getFreeHeap()));
    XML_ParserFree(parser);
    return false;
  }

  const uint32_t fileSize = file.size();
  if (callProgressPopup && popupFn && fileSize >= MIN_SIZE_FOR_POPUP) {
    popupFn();
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  XML_SetDefaultHandlerExpand(parser, defaultHandlerExpand);

  bool parseOk = true;
  const char* failReason = nullptr;
  enum XML_Error xmlError = XML_ERROR_NONE;
  XML_Size failLine = 0;
  XML_Size failColumn = 0;
  XML_Index failByte = 0;
  int done = 0;
  do {
    void* const buf = XML_GetBuffer(parser, 1024);
    if (!buf) {
      parseOk = false;
      failReason = "buffer";
      failLine = XML_GetCurrentLineNumber(parser);
      failColumn = XML_GetCurrentColumnNumber(parser);
      failByte = XML_GetCurrentByteIndex(parser);
      break;
    }
    const size_t len = file.read(buf, 1024);
    done = (len == 0);
    if (XML_ParseBuffer(parser, static_cast<int>(len), done) == XML_STATUS_ERROR) {
      parseOk = false;
      failReason = "xml";
      xmlError = XML_GetErrorCode(parser);
      failLine = XML_GetCurrentLineNumber(parser);
      failColumn = XML_GetCurrentColumnNumber(parser);
      failByte = XML_GetCurrentByteIndex(parser);
      break;
    }
  } while (!done);

  XML_ParserFree(parser);
  file.close();
  if (!parseOk) {
    const char* pass = imagePrefetchPassOnly_ ? "image-prefetch" : "layout";
    const char* errorText = xmlError == XML_ERROR_NONE ? "" : XML_ErrorString(xmlError);
    Serial.printf(
        "[%lu] [SCT] parseHtmlThroughExpat failed pass=%s reason=%s xml=%d %s line=%lu col=%lu byte=%ld size=%lu "
        "chapter=%s internal=%s heap=%u\n",
        millis(), pass, failReason ? failReason : "unknown", static_cast<int>(xmlError), errorText ? errorText : "",
        static_cast<unsigned long>(failLine), static_cast<unsigned long>(failColumn), static_cast<long>(failByte),
        static_cast<unsigned long>(fileSize), filepath.c_str(), internalPath.c_str(), static_cast<unsigned>(ESP.getFreeHeap()));
  }
  return parseOk;
}

void ChapterHtmlSlimParser::loadCssRules() {
  if (cssLoaded) return;

  sharedCssParser = epub.getParsedCssParser();
  if (sharedCssParser) {
    Serial.printf("[EHP] Using shared CSS dictionary (%zu rules)\n", sharedCssParser->getRuleCount());
  } else {
    cssParser_.clear();
    Serial.printf("[EHP] Shared CSS unavailable; using inline styles only\n");
  }

  cssLoaded = true;
}

/**
 * Processes an img element with CSS class support
 */
void ChapterHtmlSlimParser::processImageElement(const char** atts) {
  if (hasAmazonRemovedFallbackAttr(atts)) {
    return;
  }

  std::string src = "";
  std::string classAttr = "";
  std::string styleAttr = "";
  std::string idAttr = "";
  int explicitWidth = 0;
  int explicitHeight = 0;

  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      std::string attrName = atts[i];
      std::string attrValue = atts[i + 1];

      if (attrName == "src" || attrName == "href" || attrName == "xlink:href") {
        src = attrValue;
      } else if (attrName == "class") {
        classAttr = attrValue;
      } else if (attrName == "style") {
        styleAttr = attrValue;
      } else if (attrName == "id") {
        idAttr = attrValue;
      } else if (attrName == "width") {
        explicitWidth = css().parseCssLength(attrValue, viewportWidth, viewportHeight, true);
      } else if (attrName == "height") {
        explicitHeight = css().parseCssLength(attrValue, viewportWidth, viewportHeight, false);
      }
    }
  }

  if (src.empty()) return;
  if (isKnownHiddenFallbackImageClass(classAttr)) {
    return;
  }

  loadCssRules();
  if (css().isDisplayNone("img", classAttr, idAttr, styleAttr)) {
    return;
  }

  int imgWidth = explicitWidth;
  int imgHeight = explicitHeight;

  bool widthIsPercentage = false;
  bool heightIsPercentage = false;
  const bool followCssParagraphLayout = (paragraphAlignment == EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS);

  if (imgWidth == 0 || imgHeight == 0) {
    if (!styleAttr.empty()) {
      size_t widthPos = styleAttr.find("width:");
      if (widthPos != std::string::npos) {
        size_t percentPos = styleAttr.find("%", widthPos);
        if (percentPos != std::string::npos) {
          widthIsPercentage = true;
        }
      }

      size_t heightPos = styleAttr.find("height:");
      if (heightPos != std::string::npos) {
        size_t percentPos = styleAttr.find("%", heightPos);
        if (percentPos != std::string::npos) {
          heightIsPercentage = true;
        }
      }
    }

    if (imgWidth == 0) {
      int cssWidth = css().getWidth(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);

      if (cssWidth == 0 && !widthIsPercentage) {
        imgWidth = cssWidth;
      } else if (cssWidth > 0) {
        imgWidth = cssWidth;
      }
    }

    if (imgHeight == 0) {
      int cssHeight = css().getHeight(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
      if (cssHeight == 0 && !heightIsPercentage) {
        imgHeight = cssHeight;
      } else if (cssHeight > 0) {
        imgHeight = cssHeight;
      }
    }
  }

  std::string base = internalPath.empty() ? filepath : internalPath;
  std::string fullInternalPath = FsHelpers::resolveRelativePath(base, src);
  std::string cacheImgPath = epub.getCacheImgPath(fullInternalPath);

  int actualW = 0, actualH = 0;
  if (ensureImageCached(fullInternalPath, cacheImgPath, &actualW, &actualH)) {
    const int cssMaxW = css().getMaxWidth(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
    const int cssMinW = css().getMinWidth(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
    const int cssMaxH = css().getMaxHeight(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
    const int cssMinH = css().getMinHeight(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);

    if (imgWidth == 0 && cssMaxW > 0) {
      imgWidth = std::min(actualW, cssMaxW);
      imgHeight = (actualH * imgWidth) / std::max(1, actualW);
    }
    if (imgHeight == 0 && cssMaxH > 0) {
      imgHeight = std::min(actualH, cssMaxH);
      imgWidth = (actualW * imgHeight) / std::max(1, actualH);
    }

    // getWidth()/getHeight() above already resolve a percentage (e.g. "width:100%") to a concrete pixel
    // value using viewportWidth/viewportHeight, so imgWidth/imgHeight reaching here are either a real,
    // final value or genuinely unset (0) - regardless of whether a percentage was involved. Fill in
    // whichever dimension is still 0 from the actual image's aspect ratio.
    if (imgWidth > 0 && imgHeight == 0) {
      imgHeight = (actualH * imgWidth) / std::max(1, actualW);
    } else if (imgHeight > 0 && imgWidth == 0) {
      imgWidth = (actualW * imgHeight) / std::max(1, actualH);
    } else if (imgWidth == 0 && imgHeight == 0) {
      imgWidth = actualW;
      imgHeight = actualH;
    }

    if (cssMaxW > 0 && imgWidth > cssMaxW) {
      imgHeight = (imgHeight * cssMaxW) / std::max(1, imgWidth);
      imgWidth = cssMaxW;
    }

    if (cssMaxH > 0 && imgHeight > cssMaxH) {
      imgWidth = (imgWidth * cssMaxH) / std::max(1, imgHeight);
      imgHeight = cssMaxH;
    }
    if (cssMinW > 0 && imgWidth < cssMinW) {
      imgHeight = (imgHeight * cssMinW) / std::max(1, imgWidth);
      imgWidth = cssMinW;
    }
    if (cssMinH > 0 && imgHeight < cssMinH) {
      imgWidth = (imgWidth * cssMinH) / std::max(1, imgHeight);
      imgHeight = cssMinH;
    }

    if (imgWidth > viewportWidth) {
      imgHeight = (imgHeight * viewportWidth) / imgWidth;
      imgWidth = viewportWidth;
    }

    if (imgHeight > viewportHeight) {
      imgWidth = (imgWidth * viewportHeight) / imgHeight;
      imgHeight = viewportHeight;
    }

    if (imgWidth < 1) imgWidth = 1;
    if (imgHeight < 1) imgHeight = 1;

    Serial.printf("[EHP] Image %s - CSS: %s, Final: %dx%d (actual: %dx%d, percent: w=%d h=%d)\n", src.c_str(),
                  styleAttr.c_str(), imgWidth, imgHeight, actualW, actualH, widthIsPercentage, heightIsPercentage);

    // An image flows inline (as an atomic "word" on a text line) only when it is BOTH in a text context
    // (heading, or mid-paragraph with words already placed) AND small enough to be an ornament — roughly the
    // text line height. Larger images (e.g. a chapter decoration in an <h1> before a <br>) stay block-level
    // at full size, otherwise they'd be shrunk to the line height.
    const int activeFontId = inHeader ? headerFontId : fontId;
    const int lineH = std::max(1, renderer.text.getLineHeight(activeFontId));
    const bool ornamentSized = (imgHeight <= lineH * 2);
    const bool inlineImage = ornamentSized && (inHeader || (currentTextBlock && !currentTextBlock->isEmpty()));
    if (inlineImage) {
      int dispW = imgWidth;
      int dispH = imgHeight;
      // Scale to fit the text line height (keep aspect) so line height stays uniform; cap width to viewport.
      if (dispH > lineH) {
        dispW = std::max(1, dispW * lineH / std::max(1, dispH));
        dispH = lineH;
      }
      if (dispW > viewportWidth) {
        dispH = std::max(1, dispH * viewportWidth / std::max(1, dispW));
        dispW = viewportWidth;
      }
      flushPartWordBuffer();
      if (!currentTextBlock) {
        startNewTextBlock(inHeader ? TextBlock::CENTER_ALIGN : TextBlock::JUSTIFIED);
      }
      currentTextBlock->addImage(cacheImgPath, static_cast<uint16_t>(dispW), static_cast<uint16_t>(dispH));
    } else {
      if (followCssParagraphLayout && css().hasParagraphSpacingSpecified("img", classAttr, idAttr, styleAttr)) {
        if (currentPageNextY > 0) {
          applyVerticalSpacing(
              css().getParagraphSpacingTopPx("img", classAttr, idAttr, styleAttr, viewportWidth, viewportHeight));
        }
      }
      addImageToPage(cacheImgPath, imgWidth, imgHeight);
      if (followCssParagraphLayout && css().hasParagraphSpacingSpecified("img", classAttr, idAttr, styleAttr)) {
        const int defaultGap = renderer.text.getLineHeight(fontId) / 2;
        const int cssBottom =
            css().getParagraphSpacingBottomPx("img", classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
        if (cssBottom > defaultGap) {
          applyVerticalSpacing(cssBottom - defaultGap);
        }
      }
    }
  } else {
    Serial.printf("[%lu] [EBP-IMG] <img> not placed src=%s resolved=%s cache=%s skipImages=%d\n",
                  static_cast<unsigned long>(millis()), src.c_str(), fullInternalPath.c_str(), cacheImgPath.c_str(),
                  skipImages ? 1 : 0);
  }
}

/**
 * Flushes the current word buffer to the active text block.
 * Determines the appropriate font style based on current bold/italic state.
 */
void ChapterHtmlSlimParser::flushPartWordBuffer() {
  if (partWordBufferIndex == 0) return;
  partWordBuffer[partWordBufferIndex] = '\0';

  if (inDropCap) {
    if (!currentPage) currentPage.reset(new Page());

    const std::string dropCapText = uppercaseSingleLetterDropCap(partWordBuffer, partWordBufferIndex);
    const bool inlineFirstLine = dropCapLineCount <= 1;
    auto dropCapElem = std::make_shared<PageDropCap>(dropCapText, 0, currentPageNextY, maxFontId, inlineFirstLine);
    currentPage->elements.push_back(dropCapElem);

    int dropCapWidth = renderer.text.getWidth(maxFontId, dropCapText.c_str(), EpdFontFamily::BOLD) + 3;

    if (currentTextBlock) {
      currentTextBlock->setLeftIndent(dropCapWidth, inlineFirstLine ? 1 : dropCapLineCount);
    }

    partWordBufferIndex = 0;
    inDropCap = false;
    dropCapConsumeWholeContainer = false;
    dropCapLineCount = 3;
    return;
  }

  const bool cssBoldActive = !cssFontStyleStack.empty() && cssFontStyleStack.back().bold;
  const bool cssItalicActive = !cssFontStyleStack.empty() && cssFontStyleStack.back().italic;
  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if ((boldUntilDepth < depth || cssBoldActive) && (italicUntilDepth < depth || cssItalicActive)) {
    fontStyle = EpdFontFamily::BOLD_ITALIC;
  } else if (boldUntilDepth < depth || cssBoldActive) {
    fontStyle = EpdFontFamily::BOLD;
  } else if (italicUntilDepth < depth || cssItalicActive) {
    fontStyle = EpdFontFamily::ITALIC;
  }

  if (inHeader) {
    if (fontStyle == EpdFontFamily::REGULAR) {
      fontStyle = EpdFontFamily::BOLD;
    } else if (fontStyle == EpdFontFamily::ITALIC) {
      fontStyle = EpdFontFamily::BOLD_ITALIC;
    }
  }

  const bool smallCapsActive = !smallCapsStack.empty() && smallCapsStack.back();
  const bool underlineActive = underlineUntilDepth < depth;
  uint8_t verticalAlign = TextBlock::BASELINE;
  const bool superscriptActive = superscriptUntilDepth < depth;
  const bool subscriptActive = subscriptUntilDepth < depth;
  if (superscriptActive || subscriptActive) {
    verticalAlign = superscriptActive && (!subscriptActive || superscriptUntilDepth >= subscriptUntilDepth)
                        ? TextBlock::SUPERSCRIPT
                        : TextBlock::SUBSCRIPT;
  }
  if (currentTextBlock && currentTextBlock->size() >= STREAMING_TEXTBLOCK_WORD_LIMIT) {
    currentTextBlock->layoutAndExtractLines(
        renderer, activeBlockFontId(), static_cast<uint16_t>(std::max(1, currentTextBlockContentWidth)),
        [this](const std::shared_ptr<TextBlock>& textBlock) { addLineToPage(textBlock); }, false);
  }
  currentTextBlock->addWord(partWordBuffer, fontStyle, smallCapsActive, underlineActive, nextWordJoinsPrevious,
                            verticalAlign);
  nextWordJoinsPrevious = false;
  partWordBufferIndex = 0;
}

void ChapterHtmlSlimParser::applyVerticalSpacing(const int px) {
  if (px <= 0) {
    return;
  }
  if (currentPageNextY + px > viewportHeight) {
    if (currentPage && !currentPage->elements.empty()) {
      completePageFn(std::move(currentPage));
    }
    currentPage.reset(new Page());
    currentPageNextY = 0;
    return;
  }
  currentPageNextY += px;
}

void ChapterHtmlSlimParser::flushCurrentTableCell() {
  if (!currentTableCell_) {
    return;
  }
  currentTableCell_->text = trimAsciiWs(currentTableCell_->text);
  currentTableRow_.push_back(std::move(*currentTableCell_));
  currentTableCell_.reset();
  tableLastWasSpace_ = true;
}

void ChapterHtmlSlimParser::flushCurrentTableRow() {
  flushCurrentTableCell();
  if (!currentTableRow_.empty()) {
    tableRows_.push_back(std::move(currentTableRow_));
    currentTableRow_.clear();
  }
}

void ChapterHtmlSlimParser::appendTableText(const XML_Char* s, const int len) {
  if (!currentTableCell_ || s == nullptr || len <= 0) {
    return;
  }
  for (int i = 0; i < len; ++i) {
    const unsigned char ch = static_cast<unsigned char>(s[i]);
    if (std::isspace(ch)) {
      if (!tableLastWasSpace_ && !currentTableCell_->text.empty()) {
        currentTableCell_->text.push_back(' ');
      }
      tableLastWasSpace_ = true;
      continue;
    }
    currentTableCell_->text.push_back(static_cast<char>(ch));
    tableLastWasSpace_ = false;
  }
}

void ChapterHtmlSlimParser::addTableToPage() {
  flushCurrentTableRow();
  if (tableRows_.empty()) {
    return;
  }

  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    makePages();
  }

  size_t columnCount = 0;
  for (const auto& row : tableRows_) {
    size_t spanSum = 0;
    for (const auto& cell : row) {
      spanSum += std::max(1, cell.colspan);
    }
    columnCount = std::max(columnCount, spanSum);
  }
  if (columnCount == 0) {
    tableRows_.clear();
    return;
  }

  constexpr int kCellPadX = 4;
  constexpr int kCellPadY = 3;
  // Respect the reader's line-spacing setting for table contents (was using the raw font line height,
  // which made rows too tight relative to body text).
  const int lineHeight = std::max(1, static_cast<int>(renderer.text.getLineHeight(fontId) * lineCompression));
  const int tableWidth = viewportWidth;

  std::vector<uint16_t> columnWidths(columnCount, 0);
  if (columnCount == 2) {
    // Two columns: size the first column to its content (text + padding) so short labels (e.g. a TOC
    // number) sit close to the second column instead of taking up half the row.
    int col0 = 24;
    for (const auto& row : tableRows_) {
      if (row.empty()) {
        continue;
      }
      const auto& first = row.front();
      if (std::max(1, first.colspan) != 1) {
        continue;  // a full-width spanned row doesn't define the first column
      }
      const auto style = first.header ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      const int w = renderer.text.getWidth(fontId, first.text.c_str(), style) + 2 * kCellPadX;
      col0 = std::max(col0, w);
    }
    const int maxCol0 = std::max(24, (tableWidth * 2) / 5);  // cap so the second column keeps room
    col0 = std::min(col0, std::min(maxCol0, tableWidth - 24));
    columnWidths[0] = static_cast<uint16_t>(std::max(1, col0));
    columnWidths[1] = static_cast<uint16_t>(std::max(1, tableWidth - col0));
  } else {
    // 1 or 3+ columns: split evenly (last column absorbs rounding).
    const int each = std::max<int>(24, tableWidth / static_cast<int>(columnCount));
    int assignedWidth = 0;
    for (size_t i = 0; i < columnCount; ++i) {
      columnWidths[i] = (i + 1 == columnCount) ? static_cast<uint16_t>(std::max(1, tableWidth - assignedWidth))
                                               : static_cast<uint16_t>(each);
      assignedWidth += columnWidths[i];
    }
  }

  auto wrapCell = [&](const TableCellCapture& cell, const int colWidth) {
    std::vector<std::string> lines;
    const int maxTextWidth = std::max(8, colWidth - 2 * kCellPadX);
    const auto style = cell.header ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const std::string text = cell.text.empty() ? " " : cell.text;
    std::string current;
    size_t pos = 0;
    while (pos < text.size()) {
      while (pos < text.size() && text[pos] == ' ') {
        ++pos;
      }
      if (pos >= text.size()) {
        break;
      }
      const size_t next = text.find(' ', pos);
      const std::string word = text.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
      const std::string candidate = current.empty() ? word : current + " " + word;
      if (!current.empty() && renderer.text.getWidth(fontId, candidate.c_str(), style) > maxTextWidth) {
        lines.push_back(current);
        current = word;
      } else if (current.empty() && renderer.text.getWidth(fontId, word.c_str(), style) > maxTextWidth) {
        std::string chunk;
        for (const char c : word) {
          std::string tryChunk = chunk;
          tryChunk.push_back(c);
          if (!chunk.empty() && renderer.text.getWidth(fontId, tryChunk.c_str(), style) > maxTextWidth) {
            lines.push_back(chunk);
            chunk.assign(1, c);
          } else {
            chunk = tryChunk;
          }
        }
        current = chunk;
      } else {
        current = candidate;
      }
      pos = (next == std::string::npos) ? text.size() : next + 1;
    }
    if (!current.empty()) {
      lines.push_back(current);
    }
    if (lines.empty()) {
      lines.push_back(" ");
    }
    return lines;
  };

  std::vector<std::vector<PageTable::Cell>> pageRows;
  std::vector<uint16_t> pageRowHeights;
  int pageTableHeight = 1;

  auto emitCurrentPageTable = [&]() {
    if (pageRows.empty()) {
      return;
    }
    if (!currentPage) {
      currentPage.reset(new Page());
    }
    if (currentPageNextY + pageTableHeight > viewportHeight && currentPage && !currentPage->elements.empty()) {
      completePageFn(std::move(currentPage));
      currentPage.reset(new Page());
      currentPageNextY = 0;
    }
    currentPage->elements.push_back(std::make_shared<PageTable>(
        std::move(pageRows), columnWidths, pageRowHeights, tableShowBorders_, static_cast<int16_t>(tableWidth),
        static_cast<int16_t>(pageTableHeight), static_cast<int16_t>(lineHeight), 0, currentPageNextY));
    currentPageNextY += pageTableHeight + lineHeight / 2;
    pageRows.clear();
    pageRowHeights.clear();
    pageTableHeight = 1;
  };

  for (const auto& row : tableRows_) {
    std::vector<PageTable::Cell> renderedRow;
    renderedRow.reserve(row.size());
    int rowHeight = lineHeight + 2 * kCellPadY;
    size_t gridCol = 0;
    for (const auto& source : row) {
      if (gridCol >= columnCount) {
        break;
      }
      int span = std::max(1, source.colspan);
      if (gridCol + static_cast<size_t>(span) > columnCount) {
        span = static_cast<int>(columnCount - gridCol);
      }
      int spannedWidth = 0;
      for (int s = 0; s < span; ++s) {
        spannedWidth += columnWidths[gridCol + s];
      }
      PageTable::Cell cell;
      cell.header = source.header;
      cell.colspan = static_cast<uint16_t>(span);
      cell.lines = wrapCell(source, spannedWidth);
      rowHeight = std::max(rowHeight, static_cast<int>(cell.lines.size()) * lineHeight + 2 * kCellPadY);
      renderedRow.push_back(std::move(cell));
      gridCol += span;
    }

    const int projectedHeight = pageTableHeight + rowHeight + (pageRows.empty() ? 0 : 1);
    // Break before any row that would run past the content area (which already excludes the status bar).
    // This applies to the first row too, so a table following text doesn't spill into the status bar.
    if (currentPageNextY + projectedHeight > viewportHeight) {
      emitCurrentPageTable();  // flush rows already accumulated on this page (no-op if none)
      if (currentPage && !currentPage->elements.empty()) {
        completePageFn(std::move(currentPage));
        currentPage.reset(new Page());
        currentPageNextY = 0;
      }
    }

    pageTableHeight += rowHeight + (pageRows.empty() ? 0 : 1);
    pageRows.push_back(std::move(renderedRow));
    pageRowHeights.push_back(static_cast<uint16_t>(rowHeight));
  }

  emitCurrentPageTable();
  tableRows_.clear();
  currentTableRow_.clear();
  currentTableCell_.reset();
  tableShowBorders_ = false;
}

void ChapterHtmlSlimParser::handlePrefetchPassElement(const XML_Char* name, const XML_Char** atts) {
  if (matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
    prefetchImageFromImgAttributes(atts);
  } else if (skipUntilDepth >= depth && matches(name, SKIP_TAGS, NUM_SKIP_TAGS)) {
    // Not already inside a skipped subtree: start skipping this one (script/style/etc.).
    skipUntilDepth = depth;
  }
  depth += 1;
}

bool ChapterHtmlSlimParser::handleTableStartElement(const XML_Char* name, const XML_Char** atts,
                                                    const std::string& tagLower, const std::string& classAttr,
                                                    const std::string& idAttr, const std::string& styleAttr) {
  const auto attrTurnsOnBorders = [&](const char* key) {
    if (atts == nullptr) return false;
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], key) == 0 && atts[i + 1] != nullptr && atts[i + 1][0] != '\0' &&
          strcmp(atts[i + 1], "0") != 0) {
        return true;
      }
    }
    return false;
  };

  if (strcmp(name, "table") == 0) {
    flushPartWordBuffer();
    if (currentTextBlock && !currentTextBlock->isEmpty()) {
      makePages();
    }
    inTable_ = true;
    tableDepth_ = depth;
    tableRowDepth_ = INT_MAX;
    tableCellDepth_ = INT_MAX;
    tableLastWasSpace_ = true;
    tableShowBorders_ =
        css().hasBorderSpecified("table", classAttr, idAttr, styleAttr) || attrTurnsOnBorders("border");
    tableRows_.clear();
    currentTableRow_.clear();
    currentTableCell_.reset();
    depth += 1;
    return true;
  }

  if (!inTable_) {
    return false;
  }

  if (strcmp(name, "tr") == 0) {
    flushCurrentTableRow();
    tableRowDepth_ = depth;
  } else if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
    flushCurrentTableCell();
    currentTableCell_.reset(new TableCellCapture());
    currentTableCell_->header = (strcmp(name, "th") == 0);
    if (css().hasBorderSpecified(tagLower, classAttr, idAttr, styleAttr) || attrTurnsOnBorders("border")) {
      tableShowBorders_ = true;
    }
    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "colspan") == 0 && atts[i + 1] != nullptr) {
          const int span = atoi(atts[i + 1]);
          currentTableCell_->colspan = (span > 1) ? std::min(span, 64) : 1;
        }
      }
    }
    tableCellDepth_ = depth;
    tableLastWasSpace_ = true;
  } else if (strcmp(name, "br") == 0 && currentTableCell_) {
    if (!currentTableCell_->text.empty() && currentTableCell_->text.back() != ' ') {
      currentTableCell_->text.push_back(' ');
    }
    tableLastWasSpace_ = true;
  }
  depth += 1;
  return true;
}

void ChapterHtmlSlimParser::applyDropCapHint(const XML_Char* name, const std::string& tagLower,
                                             const std::string& classAttr, const std::string& idAttr,
                                             const std::string& styleAttr) {
  const bool attrHint = hasDropCapHint(classAttr, idAttr, styleAttr);
  const bool pseudoHint = css().hasFirstLetterDropCapHint(tagLower, classAttr, idAttr, styleAttr);
  if (!attrHint && !pseudoHint) {
    return;
  }
  flushPartWordBuffer();
  inDropCap = true;
  dropCapDepth = depth;
  dropCapConsumeWholeContainer = (strcmp(name, "span") == 0);
  dropCapLineCount = attrHint ? detectDropCapLineCount(classAttr, idAttr, styleAttr)
                              : css().getFirstLetterDropCapLineCount(tagLower, classAttr, idAttr, styleAttr);
}

void ChapterHtmlSlimParser::applyInlineFormattingTags(const XML_Char* name) {
  if (matches(name, BOLD_TAGS, NUM_BOLD_TAGS)) {
    boldUntilDepth = depth;
  }
  if (strcmp(name, "a") == 0) {
    underlineUntilDepth = depth;
  }
  if (matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS)) {
    italicUntilDepth = depth;
  }
}

/**
 * Creates a new text block with the specified style.
 * If there is an existing non-empty text block, it is first converted to pages.
 *
 * @param style The alignment style for the new text block
 */
TextBlock::Style ChapterHtmlSlimParser::resolveTextAlignFromAttributes(const XML_Char* elementName,
                                                                       const XML_Char** atts,
                                                                       const TextBlock::Style inheritedStyle) const {
  std::string tagLower;
  std::string classAttr;
  std::string idAttr;
  std::string styleAttr;
  extractSelectorAttributes(elementName, atts, tagLower, classAttr, idAttr, styleAttr);
  if (!css().hasTextAlignSpecified(tagLower, classAttr, idAttr, styleAttr)) {
    return inheritedStyle;
  }
  return static_cast<TextBlock::Style>(css().computeParagraphAlignment(classAttr, idAttr, styleAttr, tagLower));
}

TextBlock::Style ChapterHtmlSlimParser::resolveBlockStyle(const XML_Char* elementName, const XML_Char** atts,
                                                          const bool elementHasExplicitTextAlign,
                                                          const TextBlock::Style elementCssStyle,
                                                          const TextBlock::Style inheritedCssStyle) const {
  if (paragraphAlignment == EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS) {
    // Own text-align wins; otherwise inherit the ancestor's alignment (text-align is an inherited property).
    return elementHasExplicitTextAlign ? elementCssStyle : inheritedCssStyle;
  }
  if (elementHasExplicitTextAlign) {
    return resolveTextAlignFromAttributes(elementName, atts, inheritedCssStyle);
  }
  return static_cast<TextBlock::Style>(paragraphAlignment);
}

void ChapterHtmlSlimParser::captureCurrentTextBlockBox() {
  currentTextBlockContentX = activeBlockContentX();
  currentTextBlockContentWidth = activeBlockContentWidth();
}

void ChapterHtmlSlimParser::startNewTextBlock(TextBlock::Style style) {
  nextWordJoinsPrevious = false;
  if (currentTextBlock) {
    if (currentTextBlock->isEmpty()) {
      currentTextBlock->resetParagraphLayoutHints();
      currentTextBlock->setStyle(style);
      currentTextBlock->setRespectParagraphIndent(respectCssParagraphIndent);
      captureCurrentTextBlockBox();
      return;
    }
    makePages();
  }
  currentTextBlock.reset(new ParsedText(style, extraParagraphSpacing, hyphenationEnabled, respectCssParagraphIndent,
                                        bionicReadingEnabled, wordSpacingFactor));
  captureCurrentTextBlockBox();
}

static uint8_t borderStyleCodeFromKeyword(const std::string& kw) {
  if (kw == "double") return PageCssBorderLine::DOUBLE;
  if (kw == "dotted") return PageCssBorderLine::DOTTED;
  if (kw == "dashed") return PageCssBorderLine::DASHED;
  return PageCssBorderLine::SOLID;
}

static int reservedBorderThickness(const int thicknessPx, const uint8_t style) {
  if (thicknessPx <= 0) return 0;
  return style == PageCssBorderLine::DOUBLE ? std::max(3, thicknessPx) : thicknessPx;
}

int ChapterHtmlSlimParser::cssBorderInnerGapPx() const {
  return std::max(2, renderer.text.getLineHeight(headerFontId) / 4);
}

void ChapterHtmlSlimParser::applyMinHeightPadding() {
  if (currentBlockMinHeightPx <= 0) return;
  // Skip if the block wrapped onto a new page (content start Y no longer comparable to the current cursor).
  if (currentPageNextY < currentBlockContentStartY) return;
  const int contentHeight = currentPageNextY - currentBlockContentStartY;
  if (contentHeight < currentBlockMinHeightPx) {
    applyVerticalSpacing(currentBlockMinHeightPx - contentHeight);
  }
}

void ChapterHtmlSlimParser::tightenAfterTopBorder(const int borderTop, const int paddingTop) {
  if (borderTop <= 0 || paddingTop <= 0) return;
  const int activeFontId = inHeader ? headerFontId : fontId;
  const int inset = renderer.text.getGlyphTopInset(activeFontId, 'H', EpdFontFamily::REGULAR);
  const int reduce = std::min(inset, paddingTop);  // never pull the text above the padded box
  if (reduce > 0) {
    currentPageNextY = static_cast<int16_t>(std::max(0, static_cast<int>(currentPageNextY) - reduce));
  }
}

int ChapterHtmlSlimParser::activeBlockContentX() const { return std::max(0, currentCssInsetLeftPx); }

int ChapterHtmlSlimParser::activeBlockContentWidth() const {
  return std::max(1, static_cast<int>(viewportWidth) - std::max(0, currentCssInsetLeftPx) -
                         std::max(0, currentCssInsetRightPx));
}

void ChapterHtmlSlimParser::beginCssBlockBox(const std::string& tagLower, const std::string& classAttr,
                                             const std::string& idAttr, const std::string& styleAttr) {
  const int marginTop = css().getMarginTopPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  const int paddingTop =
      css().getPaddingTopPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  const int marginLeft = css().getMarginLeftPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  const int marginRight = css().getMarginRightPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  const int paddingLeft = css().getPaddingLeftPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  const int paddingRight =
      css().getPaddingRightPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  const int borderTop = css().getBorderTopPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  const int borderRight =
      css().getBorderRightPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  const int borderBottom =
      css().getBorderBottomPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  const int borderLeft = css().getBorderLeftPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  int horizontalLeft = marginLeft + borderLeft + paddingLeft;
  int horizontalRight = marginRight + borderRight + paddingRight;
  const bool horizontalSpacingSpecified = css().hasHorizontalSpacingSpecified(tagLower, classAttr, idAttr, styleAttr);
  if (horizontalLeft == 0 && horizontalRight == 0 && !classAttr.empty() && !horizontalSpacingSpecified) {
    const CssHorizontalInsetScope* sameClassAncestor = nullptr;
    for (auto it = cssHorizontalInsetStack.rbegin(); it != cssHorizontalInsetStack.rend(); ++it) {
      if ((it->left > 0 || it->right > 0) && classAttrsShareToken(classAttr, it->classAttr)) {
        sameClassAncestor = &(*it);
        break;
      }
    }
    if (sameClassAncestor != nullptr) {
      if (tagLower != "p") {
        horizontalLeft = sameClassAncestor->left;
        horizontalRight = sameClassAncestor->right;
      }
    } else {
      const char* FALLBACK_CLASS_TAGS[] = {"div", "section", "nav", "ol", "ul", "li"};
      constexpr int NUM_FALLBACK_CLASS_TAGS = sizeof(FALLBACK_CLASS_TAGS) / sizeof(FALLBACK_CLASS_TAGS[0]);
      for (int i = 0; i < NUM_FALLBACK_CLASS_TAGS; ++i) {
        if (tagLower == FALLBACK_CLASS_TAGS[i]) {
          continue;
        }
        const int fallbackLeft =
            css().getMarginLeftPx(FALLBACK_CLASS_TAGS[i], classAttr, idAttr, styleAttr, viewportWidth, viewportHeight) +
            css().getPaddingLeftPx(FALLBACK_CLASS_TAGS[i], classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
        const int fallbackRight =
            css().getMarginRightPx(FALLBACK_CLASS_TAGS[i], classAttr, idAttr, styleAttr, viewportWidth,
                                   viewportHeight) +
            css().getPaddingRightPx(FALLBACK_CLASS_TAGS[i], classAttr, idAttr, styleAttr, viewportWidth,
                                    viewportHeight);
        if (fallbackLeft > 0 || fallbackRight > 0) {
          horizontalLeft = fallbackLeft;
          horizontalRight = fallbackRight;
          break;
        }
      }
    }
  }
  const int minHeight = css().getMinHeight(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  CssHorizontalInsetScope insetScope;
  insetScope.depth = depth;
  insetScope.left = horizontalLeft;
  insetScope.right = horizontalRight;
  insetScope.classAttr = classAttr;
  cssHorizontalInsetStack.push_back(insetScope);
  currentCssInsetLeftPx += horizontalLeft;
  currentCssInsetRightPx += horizontalRight;
  if (currentTextBlock && currentTextBlock->isEmpty()) {
    captureCurrentTextBlockBox();
  }
  currentBlockMarginBottomPx =
      css().getMarginBottomPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  currentBlockPaddingBottomPx =
      css().getPaddingBottomPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  currentBlockBorderTopPx = borderTop;
  currentBlockBorderBottomPx = borderBottom;
  currentBlockBorderLeftPx = borderLeft;
  currentBlockBorderRightPx = borderRight;
  currentBlockBorderTopStyle =
      borderStyleCodeFromKeyword(css().getBorderStyleKeyword("top", classAttr, idAttr, styleAttr, tagLower));
  currentBlockBorderBottomStyle =
      borderStyleCodeFromKeyword(css().getBorderStyleKeyword("bottom", classAttr, idAttr, styleAttr, tagLower));
  currentBlockBorderLeftStyle =
      borderStyleCodeFromKeyword(css().getBorderStyleKeyword("left", classAttr, idAttr, styleAttr, tagLower));
  currentBlockBorderRightStyle =
      borderStyleCodeFromKeyword(css().getBorderStyleKeyword("right", classAttr, idAttr, styleAttr, tagLower));
  currentBlockUsesBorderBox = borderLeft > 0 || borderRight > 0;
  currentBlockSpacingFromCss = marginTop > 0 || paddingTop > 0 || borderTop > 0 || currentBlockMarginBottomPx > 0 ||
                               currentBlockPaddingBottomPx > 0 || currentBlockBorderBottomPx > 0 ||
                               currentBlockUsesBorderBox || minHeight > 0;
  currentBlockBottomSpacingPx = 0;
  currentBlockMinHeightPx = currentBlockSpacingFromCss ? minHeight : 0;

  if (!currentBlockSpacingFromCss) {
    currentBlockMarginBottomPx = 0;
    currentBlockPaddingBottomPx = 0;
    currentBlockBorderTopPx = 0;
    currentBlockBorderBottomPx = 0;
    currentBlockBorderLeftPx = 0;
    currentBlockBorderRightPx = 0;
    currentBlockBorderTopStyle = 0;
    currentBlockBorderBottomStyle = 0;
    currentBlockBorderLeftStyle = 0;
    currentBlockBorderRightStyle = 0;
    currentBlockUsesBorderBox = false;
    currentBlockContentStartY = currentPageNextY;
    pendingTopBorderElem_.reset();
    return;
  }

  if (currentPageNextY > 0 && marginTop > 0) {
    applyVerticalSpacing(marginTop);
  }
  if (currentBlockUsesBorderBox) {
    if (!currentPage) {
      currentPage.reset(new Page());
    }
    const int inheritedLeft = std::max(0, currentCssInsetLeftPx - horizontalLeft);
    const int inheritedRight = std::max(0, currentCssInsetRightPx - horizontalRight);
    currentBlockBorderBoxX = static_cast<int16_t>(std::max(0, inheritedLeft + marginLeft));
    currentBlockBorderBoxY = currentPageNextY;
    currentBlockBorderBoxW = static_cast<int16_t>(
        std::max(1, static_cast<int>(viewportWidth) - inheritedLeft - inheritedRight - marginLeft - marginRight));
    pendingBorderBoxElem_ = std::make_shared<PageCssBorderBox>(
        currentBlockBorderBoxX, currentBlockBorderBoxY, currentBlockBorderBoxW, 1,
        static_cast<int16_t>(currentBlockBorderTopPx), static_cast<int16_t>(currentBlockBorderRightPx),
        static_cast<int16_t>(currentBlockBorderBottomPx), static_cast<int16_t>(currentBlockBorderLeftPx),
        currentBlockBorderTopStyle, currentBlockBorderRightStyle, currentBlockBorderBottomStyle,
        currentBlockBorderLeftStyle);
    currentPage->elements.push_back(pendingBorderBoxElem_);
    CssBorderBoxScope boxScope;
    boxScope.depth = depth;
    boxScope.elem = pendingBorderBoxElem_;
    boxScope.x = currentBlockBorderBoxX;
    boxScope.y = currentBlockBorderBoxY;
    boxScope.width = currentBlockBorderBoxW;
    boxScope.paddingBottom = currentBlockPaddingBottomPx;
    boxScope.borderBottom = currentBlockBorderBottomPx;
    boxScope.marginBottom = currentBlockMarginBottomPx;
    boxScope.borderBottomStyle = currentBlockBorderBottomStyle;
    cssBorderBoxStack.push_back(boxScope);
    applyVerticalSpacing(reservedBorderThickness(currentBlockBorderTopPx, currentBlockBorderTopStyle));
  } else if (borderTop > 0) {
    pendingTopBorderElem_ = addCssBorderLine(borderTop, currentBlockBorderTopStyle);
  }
  if (paddingTop > 0) {
    applyVerticalSpacing(paddingTop);
  }
  tightenAfterTopBorder(borderTop, paddingTop);
  currentBlockContentStartY = currentPageNextY;
}

std::shared_ptr<PageCssBorderLine> ChapterHtmlSlimParser::addCssBorderLine(const int thicknessPx, const uint8_t style) {
  if (thicknessPx <= 0) {
    return nullptr;
  }
  if (!currentPage) {
    currentPage.reset(new Page());
  }
  // Border lines render at xPos + xOffset (xOffset is already the content left margin, like text lines). Start
  // as a full content-width placeholder; finalizeBorderWidth() narrows it to the text width once known.
  // A "double" rule needs at least 3px of vertical space to show its gap; reserve that in the layout.
  const int reserved = (style == PageCssBorderLine::DOUBLE) ? std::max(3, thicknessPx) : thicknessPx;
  auto elem = std::make_shared<PageCssBorderLine>(static_cast<int16_t>(0), static_cast<int16_t>(currentPageNextY),
                                                  static_cast<int16_t>(std::max<int>(1, viewportWidth)),
                                                  static_cast<int16_t>(thicknessPx), style);
  currentPage->elements.push_back(elem);
  currentPageNextY += reserved;
  return elem;
}

void ChapterHtmlSlimParser::finalizeBorderWidth(const std::shared_ptr<PageCssBorderLine>& elem, const int contentWidth,
                                                const bool center) const {
  if (!elem) return;
  if (contentWidth <= 0) return;                    // unknown width — leave the full-content-width placeholder
  int w = contentWidth + (contentWidth * 2) / 100;  // text width + 2%
  if (w > static_cast<int>(viewportWidth)) w = viewportWidth;
  if (w < 1) w = 1;
  const int x = center ? (static_cast<int>(viewportWidth) - w) / 2 : 0;
  elem->setGeometry(static_cast<int16_t>(x), static_cast<int16_t>(w));
}

/**
 * XML parser callback for opening element tags.
 * @param userData Pointer to the parser instance
 * @param name Element name
 * @param atts Element attributes
 */
void XMLCALL ChapterHtmlSlimParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  const bool followCssParagraphLayout = (self->paragraphAlignment == EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS);
  const TextBlock::Style inheritedCssStyle =
      self->cssAlignmentStack.empty() ? TextBlock::LEFT_ALIGN : self->cssAlignmentStack.back();
  TextBlock::Style elementCssStyle = inheritedCssStyle;
  bool elementHasExplicitTextAlign = false;
  std::string classAttr;
  std::string idAttr;
  std::string styleAttr;
  std::string tagLower;
  extractSelectorAttributes(name, atts, tagLower, classAttr, idAttr, styleAttr);
  const bool isHeaderTag = matches(name, HEADER_TAGS, NUM_HEADER_TAGS);
  const bool isBlockTag = matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS);
  const bool hasSelectorAttrs = !classAttr.empty() || !idAttr.empty() || !styleAttr.empty();
  const bool isCustomDisplayBlock =
      hasSelectorAttrs && !isBlockTag && !isHeaderTag &&
      (self->css().isDisplayBlock(tagLower, classAttr, idAttr, styleAttr) ||
       // A span (or other inline element) with a real top/bottom border is laid out block-like so the rule(s)
       // can be drawn — same mechanism as headings. Gated on a visible border (not border:0/none resets).
       self->css().getBorderTopPx(tagLower, classAttr, idAttr, styleAttr, self->viewportWidth,
                                      self->viewportHeight) > 0 ||
       self->css().getBorderBottomPx(tagLower, classAttr, idAttr, styleAttr, self->viewportWidth,
                                         self->viewportHeight) > 0 ||
       self->css().getBorderLeftPx(tagLower, classAttr, idAttr, styleAttr, self->viewportWidth,
                                      self->viewportHeight) > 0 ||
       self->css().getBorderRightPx(tagLower, classAttr, idAttr, styleAttr, self->viewportWidth,
                                       self->viewportHeight) > 0);
  const bool isBlockLikeElement = isHeaderTag || isBlockTag || isCustomDisplayBlock;
  if (self->paragraphAlignment == EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS && isBlockLikeElement) {
    elementHasExplicitTextAlign = self->css().hasTextAlignSpecified(tagLower, classAttr, idAttr, styleAttr);
  }
  if (self->paragraphAlignment == EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS && elementHasExplicitTextAlign) {
    elementCssStyle = self->resolveTextAlignFromAttributes(name, atts, inheritedCssStyle);
  }
  if (self->imagePrefetchPassOnly_) {
    self->handlePrefetchPassElement(name, atts);
    return;
  }

  if (self->handleTableStartElement(name, atts, tagLower, classAttr, idAttr, styleAttr)) {
    return;
  }

  const bool inheritedSmallCaps = !self->smallCapsStack.empty() && self->smallCapsStack.back();
  const bool resolvedSmallCaps = inheritedSmallCaps || hasExplicitSmallCapsHint(name, classAttr, idAttr, styleAttr);
  // Flush any pending word before the small-caps state changes so the preceding text keeps its flag.
  if (resolvedSmallCaps != inheritedSmallCaps && self->partWordBufferIndex > 0) {
    self->flushPartWordBuffer();
    self->nextWordJoinsPrevious = true;
  }
  self->smallCapsStack.push_back(resolvedSmallCaps);
  self->smallCapsDepths.push_back(self->depth);

  const bool inheritedCssBold = !self->cssFontStyleStack.empty() && self->cssFontStyleStack.back().bold;
  const bool inheritedCssItalic = !self->cssFontStyleStack.empty() && self->cssFontStyleStack.back().italic;
  const bool resolvedCssBold = self->css().resolveFontBold(tagLower, classAttr, idAttr, styleAttr, inheritedCssBold);
  const bool resolvedCssItalic =
      self->css().resolveFontItalic(tagLower, classAttr, idAttr, styleAttr, inheritedCssItalic);
  if ((resolvedCssBold != inheritedCssBold || resolvedCssItalic != inheritedCssItalic) &&
      self->partWordBufferIndex > 0) {
    self->flushPartWordBuffer();
    self->nextWordJoinsPrevious = true;
  }
  CssFontStyleScope fontScope;
  fontScope.depth = self->depth;
  fontScope.bold = resolvedCssBold;
  fontScope.italic = resolvedCssItalic;
  self->cssFontStyleStack.push_back(fontScope);

  uint8_t elementVerticalAlign = TextBlock::BASELINE;
  if (tagLower == "sup") {
    elementVerticalAlign = TextBlock::SUPERSCRIPT;
  } else if (tagLower == "sub") {
    elementVerticalAlign = TextBlock::SUBSCRIPT;
  } else {
    elementVerticalAlign = self->css().getVerticalAlign(tagLower, classAttr, idAttr, styleAttr);
  }
  if (elementVerticalAlign != TextBlock::BASELINE) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordJoinsPrevious = true;
    }
    if (elementVerticalAlign == TextBlock::SUPERSCRIPT) {
      self->superscriptUntilDepth = self->depth;
    } else {
      self->subscriptUntilDepth = self->depth;
    }
  }

  if (isCustomDisplayBlock) {
    self->flushPartWordBuffer();
    // Lay out the previous block (applying ITS bottom margin/padding) before this block overwrites the
    // shared currentBlock* spacing fields — otherwise the previous block's margin-bottom is lost.
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      self->makePages();
    }
    self->beginCssBlockBox(tagLower, classAttr, idAttr, styleAttr);
    self->currentBlockFontId =
        self->blockFontIdForEm(self->css().getFontSizeEm(tagLower, classAttr, idAttr, styleAttr));
    TextBlock::Style blockStyle =
        self->resolveBlockStyle(name, atts, elementHasExplicitTextAlign, elementCssStyle, inheritedCssStyle);
    if (self->inHeader) {
      blockStyle = TextBlock::CENTER_ALIGN;
    }
    self->startNewTextBlock(blockStyle);
  }

  self->applyDropCapHint(name, tagLower, classAttr, idAttr, styleAttr);

  if (matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
    self->processImageElement(atts);
    self->depth += 1;
    return;
  }

  if (strcmp(name, "hr") == 0) {
    self->flushPartWordBuffer();
    self->addHorizontalRule(tagLower, classAttr, idAttr, styleAttr);
    self->depth += 1;
    return;
  }

  if (self->skipUntilDepth < self->depth) {
    self->depth += 1;
    return;
  }

  self->applyInlineFormattingTags(name);

  if (isHeaderTag) {
    self->flushPartWordBuffer();
    // Lay out the previous (non-header) block with its own context/spacing before switching to header mode,
    // so its margin-bottom isn't overwritten by the header's spacing fields.
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      self->makePages();
    }
    self->inHeader = true;
    self->beginCssBlockBox(tagLower, classAttr, idAttr, styleAttr);
    // Headers default to centered, but follow an explicit CSS text-align (e.g. .h2 { text-align: right }).
    TextBlock::Style headerStyle = TextBlock::CENTER_ALIGN;
    if (self->css().hasTextAlignSpecified(tagLower, classAttr, idAttr, styleAttr)) {
      headerStyle = self->resolveTextAlignFromAttributes(name, atts, inheritedCssStyle);
    }
    self->startNewTextBlock(headerStyle);
  } else if (isBlockTag) {
    if (strcmp(name, "br") == 0) {
      self->flushPartWordBuffer();
      if (self->currentTextBlock) self->startNewTextBlock(self->currentTextBlock->getStyle());
    } else {
      const TextBlock::Style blockStyle =
          self->resolveBlockStyle(name, atts, elementHasExplicitTextAlign, elementCssStyle, inheritedCssStyle);
      self->startNewTextBlock(blockStyle);
      self->beginCssBlockBox(tagLower, classAttr, idAttr, styleAttr);
      // Large CSS font-size on a block (e.g. a big centered title <p>) renders with a bigger reader font.
      self->currentBlockFontId =
          self->blockFontIdForEm(self->css().getFontSizeEm(tagLower, classAttr, idAttr, styleAttr));
      if (self->currentTextBlock && (followCssParagraphLayout || self->respectCssParagraphIndent) &&
          self->css().hasTextIndentSpecified(tagLower, classAttr, idAttr, styleAttr)) {
        const int px = self->css().getTextIndentPx(tagLower, classAttr, idAttr, styleAttr, self->viewportWidth,
                                                       self->viewportHeight);
        self->currentTextBlock->setCssTextIndentFromCascade(px);
      }
    }
  }

  if (matches(name, SKIP_TAGS, NUM_SKIP_TAGS)) {
    self->skipUntilDepth = self->depth;
  }

  if (self->paragraphAlignment == EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS) {
    // text-align is inherited: an element with its own text-align defines the alignment for its subtree;
    // otherwise the subtree keeps inheriting the ancestor's alignment.
    const TextBlock::Style pushedCssStyle = elementHasExplicitTextAlign ? elementCssStyle : inheritedCssStyle;
    self->cssAlignmentStack.push_back(pushedCssStyle);
    self->cssAlignmentDepths.push_back(self->depth);
  }
  self->depth += 1;
}

/**
 * Expat default-handler runs for entity references and other non–CDATA segments.
 * Only known `&...;` entities are expanded into layout text; everything else is ignored so markup/DOCTYPE noise
 * and unknown entity spellings do not appear on the page.
 */
void XMLCALL ChapterHtmlSlimParser::defaultHandlerExpand(void* userData, const XML_Char* s, int len) {
  if (s == nullptr || len <= 0) {
    return;
  }
  if (len >= 3 && s[0] == static_cast<XML_Char>('&') && s[len - 1] == static_cast<XML_Char>(';')) {
    const char* const entity = reinterpret_cast<const char*>(s);
    const char* const utf8 = lookupHtmlEntity(entity, static_cast<size_t>(len));
    if (utf8 != nullptr) {
      characterData(userData, reinterpret_cast<const XML_Char*>(utf8), static_cast<int>(strlen(utf8)));
    }
    return;
  }
}

/**
 * XML parser callback for character data.
 * @param userData Pointer to the parser instance
 * @param s Character data
 * @param len Length of character data
 */
void XMLCALL ChapterHtmlSlimParser::characterData(void* userData, const XML_Char* s, int len) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  if (self->imagePrefetchPassOnly_) {
    return;
  }
  if (self->inTable_) {
    self->appendTableText(s, len);
    return;
  }
  if (self->skipUntilDepth < self->depth) return;

  for (int i = 0; i < len; i++) {
    if (isWhitespace(s[i])) {
      if (!self->inDropCap) {
        self->flushPartWordBuffer();
        self->nextWordJoinsPrevious = false;
      }
      continue;
    }

    if (s[i] == (XML_Char)0xEF && i + 2 < len && s[i + 1] == (XML_Char)0xBB && s[i + 2] == (XML_Char)0xBF) {
      i += 2;
      continue;
    }

    if (!self->inDropCap && self->partWordBufferIndex >= MAX_WORD_SIZE) {
      self->flushPartWordBuffer();
    }
    if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
      continue;
    }
    self->partWordBuffer[self->partWordBufferIndex++] = s[i];

    if (self->inDropCap && endsWithCompleteUtf8Codepoint(self->partWordBuffer, self->partWordBufferIndex) &&
        countUtf8Codepoints(self->partWordBuffer, self->partWordBufferIndex) >=
            desiredDropCapCodepoints(self->partWordBuffer, self->partWordBufferIndex,
                                     self->dropCapConsumeWholeContainer)) {
      self->flushPartWordBuffer();
    }
  }

  if (self->currentTextBlock && self->currentTextBlock->size() > STREAMING_TEXTBLOCK_WORD_LIMIT) {
    self->currentTextBlock->layoutAndExtractLines(
        self->renderer, self->activeBlockFontId(), static_cast<uint16_t>(std::max(1, self->currentTextBlockContentWidth)),
        [self](const std::shared_ptr<TextBlock>& textBlock) { self->addLineToPage(textBlock); }, false);
  }
}

/**
 * XML parser callback for closing element tags.
 * @param userData Pointer to the parser instance
 * @param name Element name
 */
void XMLCALL ChapterHtmlSlimParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  if (self->imagePrefetchPassOnly_) {
    (void)name;
    self->depth -= 1;
    if (self->skipUntilDepth == self->depth) {
      self->skipUntilDepth = INT_MAX;
    }
    return;
  }

  if (self->inTable_) {
    if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
      self->flushCurrentTableCell();
      self->tableCellDepth_ = INT_MAX;
    } else if (strcmp(name, "tr") == 0) {
      self->flushCurrentTableRow();
      self->tableRowDepth_ = INT_MAX;
    }
    self->depth -= 1;
    if (strcmp(name, "table") == 0 && self->tableDepth_ == self->depth) {
      self->inTable_ = false;
      self->tableDepth_ = INT_MAX;
      self->tableRowDepth_ = INT_MAX;
      self->tableCellDepth_ = INT_MAX;
      self->addTableToPage();
    }
    return;
  }

  if (self->partWordBufferIndex > 0) {
    if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS) || matches(name, HEADER_TAGS, NUM_HEADER_TAGS) ||
        matches(name, BOLD_TAGS, NUM_BOLD_TAGS) || matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS) || self->depth == 1) {
      self->flushPartWordBuffer();
    }
  }
  const int closingDepth = self->depth - 1;
  if (self->partWordBufferIndex > 0 && !self->cssFontStyleStack.empty() &&
      self->cssFontStyleStack.back().depth == closingDepth) {
    const bool closingBold = self->cssFontStyleStack.back().bold;
    const bool closingItalic = self->cssFontStyleStack.back().italic;
    const bool parentBold = self->cssFontStyleStack.size() >= 2
                                ? self->cssFontStyleStack[self->cssFontStyleStack.size() - 2].bold
                                : false;
    const bool parentItalic = self->cssFontStyleStack.size() >= 2
                                  ? self->cssFontStyleStack[self->cssFontStyleStack.size() - 2].italic
                                  : false;
    if (closingBold != parentBold || closingItalic != parentItalic) {
      self->flushPartWordBuffer();
      self->nextWordJoinsPrevious = true;
    }
  }
  if (self->partWordBufferIndex > 0 &&
      (self->superscriptUntilDepth == closingDepth || self->subscriptUntilDepth == closingDepth)) {
    self->flushPartWordBuffer();
    self->nextWordJoinsPrevious = true;
  }

  if (matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      self->makePages();
    } else if (self->currentBlockSpacingFromCss) {
      self->applyMinHeightPadding();  // empty block still honors its min-height
      if (!self->currentBlockUsesBorderBox) {
        if (self->currentBlockPaddingBottomPx > 0) {
          self->applyVerticalSpacing(self->currentBlockPaddingBottomPx);
        }
        if (self->currentBlockBorderBottomPx > 0) {
          self->addCssBorderLine(self->currentBlockBorderBottomPx, self->currentBlockBorderBottomStyle);
        }
        if (self->currentBlockMarginBottomPx > 0) {
          self->applyVerticalSpacing(self->currentBlockMarginBottomPx);
        }
      }
      self->currentBlockMarginBottomPx = 0;
      self->currentBlockPaddingBottomPx = 0;
      self->currentBlockBorderTopPx = 0;
      self->currentBlockBorderBottomPx = 0;
      self->currentBlockBorderLeftPx = 0;
      self->currentBlockBorderRightPx = 0;
      self->currentBlockBorderTopStyle = 0;
      self->currentBlockBorderBottomStyle = 0;
      self->currentBlockBorderLeftStyle = 0;
      self->currentBlockBorderRightStyle = 0;
      self->currentBlockUsesBorderBox = false;
      self->currentBlockMinHeightPx = 0;
      self->currentBlockFontId = -1;
      self->currentBlockBottomSpacingPx = 0;
      self->currentBlockSpacingFromCss = false;
      // Empty block: no text to measure, so leave the top rule at its full-content-width placeholder.
      self->pendingTopBorderElem_.reset();
    }
    self->inHeader = false;
  }

  self->depth -= 1;

  if (!self->cssBorderBoxStack.empty() && self->cssBorderBoxStack.back().depth == self->depth) {
    if (!self->cssBorderBoxStack.back().finalized && self->currentBlockUsesBorderBox && self->currentTextBlock &&
        !self->currentTextBlock->isEmpty()) {
      self->makePages();
    }
    auto scope = self->cssBorderBoxStack.back();
    if (!scope.finalized) {
      if (scope.paddingBottom > 0) {
        self->applyVerticalSpacing(scope.paddingBottom);
      } else if (scope.borderBottom > 0) {
        self->applyVerticalSpacing(self->cssBorderInnerGapPx());
      }
      self->applyVerticalSpacing(reservedBorderThickness(scope.borderBottom, scope.borderBottomStyle));
      if (scope.elem && self->currentPageNextY >= scope.y) {
        scope.elem->setGeometry(scope.x, scope.y, scope.width,
                                static_cast<int16_t>(std::max<int>(
                                    1, static_cast<int>(self->currentPageNextY) - static_cast<int>(scope.y))));
      }
      if (scope.marginBottom > 0) {
        self->applyVerticalSpacing(scope.marginBottom);
      }
    }
    self->cssBorderBoxStack.pop_back();
  }

  if (!self->cssHorizontalInsetStack.empty() && self->cssHorizontalInsetStack.back().depth == self->depth) {
    const auto scope = self->cssHorizontalInsetStack.back();
    self->currentCssInsetLeftPx = std::max(0, self->currentCssInsetLeftPx - scope.left);
    self->currentCssInsetRightPx = std::max(0, self->currentCssInsetRightPx - scope.right);
    self->cssHorizontalInsetStack.pop_back();
  }

  // Pop only the alignment level this element actually pushed (see cssAlignmentDepths) — tags that early-return
  // in startElement never pushed, so an unconditional pop here would corrupt inherited alignment.
  if (self->paragraphAlignment == EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS && !self->cssAlignmentDepths.empty() &&
      self->cssAlignmentDepths.back() == self->depth) {
    self->cssAlignmentStack.pop_back();
    self->cssAlignmentDepths.pop_back();
  }
  if (!self->cssFontStyleStack.empty() && self->cssFontStyleStack.back().depth == self->depth) {
    self->cssFontStyleStack.pop_back();
  }
  if (!self->smallCapsDepths.empty() && self->smallCapsDepths.back() == self->depth) {
    const bool wasSmallCaps = self->smallCapsStack.back();
    const size_t depthCount = self->smallCapsStack.size();
    const bool nowSmallCaps = depthCount >= 2 && self->smallCapsStack[depthCount - 2];
    // Flush the trailing word (e.g. "note" in "<span class=smallcaps>author's note</span>") while the
    // small-caps flag is still active, before the scope is popped.
    if (wasSmallCaps != nowSmallCaps && self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordJoinsPrevious = true;
    }
    self->smallCapsDepths.pop_back();
    self->smallCapsStack.pop_back();
  }

  if (self->skipUntilDepth == self->depth) self->skipUntilDepth = INT_MAX;
  if (self->boldUntilDepth == self->depth) self->boldUntilDepth = INT_MAX;
  if (self->italicUntilDepth == self->depth) self->italicUntilDepth = INT_MAX;
  if (self->underlineUntilDepth == self->depth) self->underlineUntilDepth = INT_MAX;
  if (self->superscriptUntilDepth == self->depth) self->superscriptUntilDepth = INT_MAX;
  if (self->subscriptUntilDepth == self->depth) self->subscriptUntilDepth = INT_MAX;

  if (self->dropCapDepth == self->depth) {
    if (self->inDropCap && self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->inDropCap = false;
    self->dropCapDepth = INT_MAX;
    self->dropCapConsumeWholeContainer = false;
    self->dropCapLineCount = 3;
  }
}

/**
 * Reads dimensions from a cached image file.
 * @param path Path to the image file
 * @param w Output parameter for width
 * @param h Output parameter for height
 * @return true if dimensions were successfully read
 */
bool ChapterHtmlSlimParser::getImageDimensions(const std::string& path, int* w, int* h) {
  *w = 0;
  *h = 0;
  return ImageRender::getDimensions(path, w, h) && (*w > 0) && (*h > 0);
}

/**
 * Adds a single text line to the current page.
 * Handles page breaking when the line exceeds available space.
 * @param line The text block line to add
 */
void ChapterHtmlSlimParser::addLineToPage(std::shared_ptr<TextBlock> line) {
  const int activeFontId = activeBlockFontId();
  const int lineHeight = renderer.text.getLineHeight(activeFontId) * lineCompression;

  if (!line || line->isEmpty()) return;

  if (currentPageNextY + lineHeight > viewportHeight) {
    if (currentPage && !currentPage->elements.empty()) completePageFn(std::move(currentPage));
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  if (!currentPage) currentPage.reset(new Page());

  // A header, or a block with a large-font override, renders as a PageHeader carrying its own font id.
  if (inHeader || currentBlockFontId >= 0) {
    const int feId = currentBlockFontId >= 0 ? currentBlockFontId : headerFontId;
    currentPage->elements.push_back(
        std::make_shared<PageHeader>(line, currentTextBlockContentX, currentPageNextY, feId));
  } else if (line->hasSmallCaps()) {
    currentPage->elements.push_back(std::make_shared<PageSmallCaps>(line, currentTextBlockContentX, currentPageNextY,
                                                                    fontId));
  } else {
    currentPage->elements.push_back(std::make_shared<PageLine>(line, currentTextBlockContentX, currentPageNextY));
  }

  currentPageNextY += lineHeight;
}

void ChapterHtmlSlimParser::addCenteredDivider(const char* text) {
  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    makePages();
  }

  const int activeFontId = inHeader ? headerFontId : fontId;
  const int lineHeight = renderer.text.getLineHeight(activeFontId) * lineCompression;
  const int spacer = std::max(6, lineHeight / 2);
  applyVerticalSpacing(spacer);

  auto divider = std::make_shared<ParsedText>(TextBlock::CENTER_ALIGN, false, false, false, false);
  divider->addWord(text, EpdFontFamily::BOLD, false);
  divider->layoutAndExtractLines(renderer, activeFontId, viewportWidth,
                                 [this](const std::shared_ptr<TextBlock>& textBlock) { addLineToPage(textBlock); });

  applyVerticalSpacing(spacer);
}

void ChapterHtmlSlimParser::addHorizontalRule(const std::string& tagLower, const std::string& classAttr,
                                              const std::string& idAttr, const std::string& styleAttr) {
  if (css().isDisplayNone(tagLower, classAttr, idAttr, styleAttr)) {
    return;
  }

  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    makePages();
  }

  const int spacingTop =
      css().getParagraphSpacingTopPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  const int spacingBottom =
      css().getParagraphSpacingBottomPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
  const int defaultHrGap = (renderer.text.getLineHeight(fontId) / 2) + 5;
  auto applyHrTopSpacing = [&]() {
    if (spacingTop > 0 && currentPageNextY > 0) {
      applyVerticalSpacing(spacingTop);
    }
  };

  bool renderedRule = false;
  const std::string bgInternalPath =
      css().getBackgroundImagePath(tagLower, classAttr, idAttr, styleAttr, internalPath);
  if (!bgInternalPath.empty()) {
    const std::string cacheImgPath = epub.getCacheImgPath(bgInternalPath);
    int imgW = 0;
    int imgH = 0;
    if (ensureImageCached(bgInternalPath, cacheImgPath, &imgW, &imgH) && imgW > 0 && imgH > 0) {
      const int cssWidth = css().getWidth(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
      const int cssHeight = css().getHeight(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
      if (cssWidth > 0 && cssHeight > 0) {
        imgW = cssWidth;
        imgH = cssHeight;
      } else if (cssWidth > 0) {
        imgH = std::max(1, static_cast<int>((static_cast<int64_t>(imgH) * cssWidth) / std::max(1, imgW)));
        imgW = cssWidth;
      } else if (cssHeight > 0) {
        imgW = std::max(1, static_cast<int>((static_cast<int64_t>(imgW) * cssHeight) / std::max(1, imgH)));
        imgH = cssHeight;
      } else {
        imgH = std::max(1, static_cast<int>((static_cast<int64_t>(imgH) * viewportWidth) / std::max(1, imgW)));
        imgW = viewportWidth * .4;
      }
      // Keep it within the page.
      if (imgW > viewportWidth) {
        imgH = std::max(1, static_cast<int>((static_cast<int64_t>(imgH) * viewportWidth) / std::max(1, imgW)));
        imgW = viewportWidth;
      }
      if (imgH > viewportHeight) {
        imgW = std::max(1, static_cast<int>((static_cast<int64_t>(imgW) * viewportHeight) / std::max(1, imgH)));
        imgH = viewportHeight;
      }
      // Default top margin when CSS didn't specify one (addImageToPage already adds a gap below).
      applyHrTopSpacing();
      if (spacingTop <= 0 && currentPageNextY > 0) {
        applyVerticalSpacing(defaultHrGap);
      }
      addImageToPage(cacheImgPath, imgW, imgH);
      renderedRule = true;
    }
  }

  if (!renderedRule) {
    const int borderTop =
        css().getBorderTopPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
    const int borderBottom =
        css().getBorderBottomPx(tagLower, classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
    if (borderTop > 0 || borderBottom > 0) {
      const int cssHeight = css().getHeight(classAttr, idAttr, styleAttr, viewportWidth, viewportHeight);
      applyHrTopSpacing();
      if (borderTop > 0) {
        addCssBorderLine(borderTop, borderStyleCodeFromKeyword(css().getBorderStyleKeyword("top", classAttr, idAttr,
                                                                                               styleAttr, tagLower)));
      }
      if (cssHeight > borderTop + borderBottom) {
        applyVerticalSpacing(cssHeight - borderTop - borderBottom);
      }
      if (borderBottom > 0) {
        addCssBorderLine(borderBottom, borderStyleCodeFromKeyword(css().getBorderStyleKeyword(
                                           "bottom", classAttr, idAttr, styleAttr, tagLower)));
      }
      renderedRule = true;
    }
  }

  if (!renderedRule) {
    return;
  }

  if (spacingBottom > 0) {
    applyVerticalSpacing(spacingBottom);
  }
}

/**
 * Converts the current text block into page lines.
 * Extracts lines based on viewport width and adds them to the current page.
 */
void ChapterHtmlSlimParser::makePages() {
  if (!currentTextBlock) return;

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  const int lineHeight = renderer.text.getLineHeight(fontId) * lineCompression;
  const bool centerBorder = (currentTextBlock->getStyle() == TextBlock::CENTER_ALIGN);
  const int readerParagraphGap = extraParagraphSpacing ? lineHeight / 2 : 0;

  currentTextBlock->layoutAndExtractLines(
      renderer, activeBlockFontId(), static_cast<uint16_t>(std::max(1, currentTextBlockContentWidth)),
      [this](const std::shared_ptr<TextBlock>& textBlock) { addLineToPage(textBlock); });

  // Now the block is laid out, size its border rule(s) to the actual text width (+2%) instead of the page.
  const int contentBorderWidth = static_cast<int>(currentTextBlock->maxLineContentWidth());
  if (pendingTopBorderElem_) {
    finalizeBorderWidth(pendingTopBorderElem_, contentBorderWidth, centerBorder);
    pendingTopBorderElem_.reset();
  }

  if (currentBlockSpacingFromCss) {
    applyMinHeightPadding();  // grow short content to the block's min-height before the bottom box spacing
    if (currentBlockPaddingBottomPx > 0) {
      applyVerticalSpacing(currentBlockPaddingBottomPx);
    } else if (currentBlockBorderBottomPx > 0) {
      // No explicit padding: keep the text off the bottom rule (mirrors the top border gap).
      applyVerticalSpacing(cssBorderInnerGapPx());
    }
    if (currentBlockUsesBorderBox) {
      applyVerticalSpacing(reservedBorderThickness(currentBlockBorderBottomPx, currentBlockBorderBottomStyle));
      if (pendingBorderBoxElem_ && currentPageNextY >= currentBlockBorderBoxY) {
        pendingBorderBoxElem_->setGeometry(currentBlockBorderBoxX, currentBlockBorderBoxY, currentBlockBorderBoxW,
                                           static_cast<int16_t>(std::max<int>(
                                               1, static_cast<int>(currentPageNextY) - currentBlockBorderBoxY)));
        for (auto it = cssBorderBoxStack.rbegin(); it != cssBorderBoxStack.rend(); ++it) {
          if (it->elem == pendingBorderBoxElem_) {
            it->finalized = true;
            break;
          }
        }
      }
      pendingBorderBoxElem_.reset();
    } else if (currentBlockBorderBottomPx > 0) {
      auto bottomElem = addCssBorderLine(currentBlockBorderBottomPx, currentBlockBorderBottomStyle);
      finalizeBorderWidth(bottomElem, contentBorderWidth, centerBorder);
    }
    int outsideBottomGap = 0;
    if (currentBlockMarginBottomPx > 0) {
      applyVerticalSpacing(currentBlockMarginBottomPx);
      outsideBottomGap += currentBlockMarginBottomPx;
    }
    if (currentBlockBottomSpacingPx > 0) {
      applyVerticalSpacing(currentBlockBottomSpacingPx);
      outsideBottomGap += currentBlockBottomSpacingPx;
    }
    if (readerParagraphGap > outsideBottomGap) {
      applyVerticalSpacing(readerParagraphGap - outsideBottomGap);
    }
  } else if (readerParagraphGap > 0) {
    applyVerticalSpacing(readerParagraphGap);
  }
  currentBlockMarginBottomPx = 0;
  currentBlockPaddingBottomPx = 0;
  currentBlockBorderTopPx = 0;
  currentBlockBorderBottomPx = 0;
  currentBlockBorderLeftPx = 0;
  currentBlockBorderRightPx = 0;
  currentBlockBorderTopStyle = 0;
  currentBlockBorderBottomStyle = 0;
  currentBlockBorderLeftStyle = 0;
  currentBlockBorderRightStyle = 0;
  currentBlockUsesBorderBox = false;
  currentBlockBorderBoxX = 0;
  currentBlockBorderBoxY = 0;
  currentBlockBorderBoxW = 0;
  pendingBorderBoxElem_.reset();
  currentBlockMinHeightPx = 0;
  currentBlockFontId = -1;
}

/**
 * Ensures an image is cached as BMP format.
 * If skipImages is true, only returns true for already-cached images.
 * @param internalPath Original image path within EPUB
 * @param cacheImgPath Target path for cached BMP
 * @param w Output parameter for image width
 * @param h Output parameter for image height
 * @return true if image is available in cache
 */
bool ChapterHtmlSlimParser::ensureImageCached(const std::string& internalPath, const std::string& cacheImgPath, int* w,
                                              int* h) {
  const bool cacheIsJpeg = hasJpegExt(cacheImgPath);
  const bool cacheIsPng = hasPngExt(cacheImgPath);
  if (SdMan.exists(cacheImgPath.c_str())) {
    if (getImageDimensions(cacheImgPath, w, h)) {
      Serial.printf("[%lu] [EBP-IMG] cache hit %s\n", static_cast<unsigned long>(millis()), cacheImgPath.c_str());
      return true;
    }
    Serial.printf("[%lu] [EBP-IMG] stale cache removed (bad BMP): %s\n", static_cast<unsigned long>(millis()),
                  cacheImgPath.c_str());
    SdMan.remove(cacheImgPath.c_str());
  }

  if (skipImages) {
    Serial.printf("[%lu] [EBP-IMG] skip extract (skipImages) href=%s\n", static_cast<unsigned long>(millis()),
                  internalPath.c_str());
    return false;
  }

  bool result = false;
  if (cacheIsJpeg || cacheIsPng) {
    result = epub.extractItemToPath(internalPath, cacheImgPath, 4096);
  } else {
    result = epub.extractAndConvertImage(internalPath, cacheImgPath, viewportWidth, 0);
  }

  if (result) {
    if (++imageExtractCountForYield_ % 2u == 0u) {
      yield();
    }
    if (getImageDimensions(cacheImgPath, w, h)) {
      return true;
    }
    Serial.printf("[%lu] [EBP-IMG] post-extract BMP unreadable: %s\n", static_cast<unsigned long>(millis()),
                  cacheImgPath.c_str());
    return false;
  }

  Serial.printf("[%lu] [EBP-IMG] ensureImageCached failed href=%s\n", static_cast<unsigned long>(millis()),
                internalPath.c_str());
  return false;
}

/**
 * Adds an image to the current page layout.
 * Handles scaling, centering, and special handling for extra-large images.
 * @param bmpPath Path to the cached BMP image
 * @param imgW Original image width
 * @param imgH Original image height
 */
namespace {
// Decides whether an image (JPEG / PNG / BMP — whatever format the cache holds) has enough continuous-tone
// (mid-gray) content to be worth grayscale rendering. It decodes the image through the SAME pipeline used for
// display (so the result matches what would actually be shown) at a small size for speed, and histograms the
// resulting 4 levels via the analysis hook in adjustTwoBitImageLevelForDisplay(). Comics / line art / mostly
// black-and-white images have almost no mid-gray pixels and render fine (and far faster) as plain 1-bit.
bool imageHasGrayscaleContent(GfxRenderer& renderer, const std::string& path, int imgW, int imgH) {
  if (imgW <= 0 || imgH <= 0) {
    return true;
  }
  // Tiny images (HR rules, separators, small icons/ornaments) never benefit from grayscale and their
  // anti-aliased edges easily trip the mid-gray threshold — always render them as fast 1-bit.
  constexpr int kMinGrayscaleImageDim = 48;
  if (imgW < kMinGrayscaleImageDim || imgH < kMinGrayscaleImageDim) {
    return false;
  }
  // Decode at a small size; the mid-gray fraction is ~scale-invariant and this keeps build time down.
  constexpr int kMaxAnalyzeW = 160;
  int aw = imgW;
  int ah = imgH;
  if (aw > kMaxAnalyzeW) {
    ah = std::max(1, imgH * kMaxAnalyzeW / imgW);
    aw = kMaxAnalyzeW;
  }

  ImageRender::Options opt;
  opt.mode = ImageRenderMode::TwoBit;  // 2-bit path -> adjustTwoBitImageLevelForDisplay runs per pixel (histogram)
  opt.useDisplayCache = false;         // detection only; don't read/write the display cache

  const int rowBytes = (aw + 7) / 8;
  std::vector<uint8_t> savedPixels(static_cast<size_t>(rowBytes) * static_cast<size_t>(ah));
  const bool restorePixels = !savedPixels.empty();
  if (restorePixels) {
    for (int y = 0; y < ah; ++y) {
      renderer.readPackedRow1bpp(0, y, aw, savedPixels.data() + static_cast<size_t>(y) * rowBytes);
    }
  }

  const GfxRenderer::RenderMode savedMode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  beginImageLevelAnalysis();
  const bool ok = ImageRender::create(renderer, path).render(0, 0, aw, ah, opt);
  const uint32_t midPct = imageLevelAnalysisMidPercent();
  endImageLevelAnalysis();
  renderer.setRenderMode(savedMode);
  if (restorePixels) {
    for (int y = 0; y < ah; ++y) {
      renderer.drawPackedRow1bpp(0, y, aw, savedPixels.data() + static_cast<size_t>(y) * rowBytes);
    }
  }

  if (!ok) {
    return true;  // default to grayscale if we couldn't decode it
  }
  constexpr uint32_t kMidGrayThresholdPercent = 6;  // below this -> treat as 1-bit (comic/line art)
  return midPct >= kMidGrayThresholdPercent;
}
}  // namespace

void ChapterHtmlSlimParser::addImageToPage(const std::string& bmpPath, int imgW, int imgH) {
  bool isExtraLarge = (imgW >= viewportWidth * 0.95 && imgH >= viewportHeight * 0.65);
  const bool grayscale = imageHasGrayscaleContent(renderer, bmpPath, imgW, imgH);

  const auto addPlacedImage = [this, &bmpPath, imgW, imgH, grayscale](const int16_t x, const int16_t y) {
    auto image = std::make_shared<PageImage>(bmpPath, imgW, imgH, x, y, grayscale);
    currentPage->elements.push_back(image);
    if (warmImageDisplayCache) {
      ImageRenderMode mode = warmImageRenderMode;
      bool quality = warmImageQuality;
      if (mode == ImageRenderMode::TwoBit && !grayscale) {
        mode = ImageRenderMode::OneBit;
        quality = false;
      }
      image->warmDisplayCache(renderer, 0, warmImageYOffset, mode, quality);
    }
  };

  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    makePages();
  }

  if (isExtraLarge) {
    if (currentPage && !currentPage->elements.empty()) {
      completePageFn(std::move(currentPage));
      currentPageNextY = 0;
    }

    currentPage.reset(new Page());
    currentPageNextY = 0;
    addPlacedImage(0, 0);

    currentPageNextY = imgH + (renderer.text.getLineHeight(fontId) / 2);
    int remainingSpace = viewportHeight - currentPageNextY;
    int minTextHeight = renderer.text.getLineHeight(fontId) * lineCompression * 2;

    if (remainingSpace < minTextHeight) {
      completePageFn(std::move(currentPage));
      currentPage.reset(new Page());
      currentPageNextY = 0;
    }

    return;
  }

  if (currentPageNextY + imgH > viewportHeight) {
    if (currentPage && !currentPage->elements.empty()) {
      completePageFn(std::move(currentPage));
    }
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  if (!currentPage) {
    currentPage.reset(new Page());
  }

  int xPos = (imgW < viewportWidth) ? (viewportWidth - imgW) / 2 : 0;
  addPlacedImage(xPos, currentPageNextY);

  currentPageNextY += imgH + (renderer.text.getLineHeight(fontId) / 2);
}

/**
 * Parses the HTML file and builds pages.
 * When skipImageProcessing is true, only processes text and uses existing cached images
 * without converting new ones. Images that aren't already cached will be skipped.
 * @param skipImageProcessing If true, skip converting new images and only process text
 * @return true if parsing was successful, false otherwise
 */
bool ChapterHtmlSlimParser::parseAndBuildPages(bool skipImageProcessing) {
  Serial.printf(
      "[%lu] [SCT] parseAndBuildPages start internal=%s tmp=%s skipImages=%d viewport=%ux%u font=%d headerFont=%d "
      "heap=%u\n",
      millis(), internalPath.c_str(), filepath.c_str(), skipImageProcessing ? 1 : 0, viewportWidth, viewportHeight,
      fontId, headerFontId, static_cast<unsigned>(ESP.getFreeHeap()));

  skipImages = skipImageProcessing;
  imageExtractCountForYield_ = 0;

  if (!skipImageProcessing) {
    imagePrefetchPassOnly_ = true;
    resetStructuralStateForParsePass();
    if (!parseHtmlThroughExpat(false)) {
      imagePrefetchPassOnly_ = false;
      Serial.printf("[%lu] [SCT] parseAndBuildPages failed during image-prefetch internal=%s tmp=%s heap=%u\n",
                    millis(), internalPath.c_str(), filepath.c_str(), static_cast<unsigned>(ESP.getFreeHeap()));
      return false;
    }
    imagePrefetchPassOnly_ = false;
  }

  skipImages = skipImageProcessing;
  inDropCap = false;
  dropCapDepth = INT_MAX;
  dropCapConsumeWholeContainer = false;
  dropCapLineCount = 3;
  nextWordJoinsPrevious = false;
  cssLoaded = false;
  currentBlockBottomSpacingPx = 0;
  currentBlockSpacingFromCss = false;
  currentBlockMarginBottomPx = 0;
  currentBlockPaddingBottomPx = 0;
  currentBlockBorderBottomPx = 0;
  currentBlockBorderBottomStyle = 0;
  currentBlockMinHeightPx = 0;
  currentBlockContentStartY = 0;
  currentBlockFontId = -1;
  cssHorizontalInsetStack.clear();
  currentCssInsetLeftPx = 0;
  currentCssInsetRightPx = 0;
  currentTextBlockContentX = 0;
  currentTextBlockContentWidth = std::max(1, static_cast<int>(viewportWidth));
  pendingTopBorderElem_.reset();

  loadCssRules();

  TextBlock::Style initialBlockStyle = TextBlock::LEFT_ALIGN;
  if (paragraphAlignment <= 3) {
    initialBlockStyle = static_cast<TextBlock::Style>(paragraphAlignment);
  } else if (paragraphAlignment == EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS) {
    initialBlockStyle = TextBlock::JUSTIFIED;
  }
  if (paragraphAlignment == EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS) {
    cssAlignmentStack.push_back(initialBlockStyle);
    cssAlignmentDepths.push_back(-1);  // sentinel root: no element depth (>=0) ever pops it
  }
  smallCapsStack.push_back(false);
  smallCapsDepths.push_back(-1);
  startNewTextBlock(initialBlockStyle);

  if (!parseHtmlThroughExpat(true)) {
    Serial.printf("[%lu] [SCT] parseAndBuildPages failed during layout internal=%s tmp=%s y=%d heap=%u\n", millis(),
                  internalPath.c_str(), filepath.c_str(), currentPageNextY, static_cast<unsigned>(ESP.getFreeHeap()));
    return false;
  }

  flushPartWordBuffer();

  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    makePages();
  }

  if (currentPage && !currentPage->elements.empty()) {
    completePageFn(std::move(currentPage));
  }

  Serial.printf("[%lu] [SCT] parseAndBuildPages success internal=%s finalY=%d heap=%u\n", millis(),
                internalPath.c_str(), currentPageNextY, static_cast<unsigned>(ESP.getFreeHeap()));
  return true;
}
