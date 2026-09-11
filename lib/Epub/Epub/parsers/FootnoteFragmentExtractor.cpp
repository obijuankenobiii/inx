/**
 * @file FootnoteFragmentExtractor.cpp
 */

#include "FootnoteFragmentExtractor.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace {

constexpr size_t kBackwardBlockSearchLimit = 8192;  // don't scan unboundedly backward on a huge buffer

/** True if c can appear in an (X)HTML tag name. */
bool isTagNameChar(const char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_'; }

std::string lowerAscii(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

/** True for tag names whose content is a natural "one footnote's worth of text" container - i.e. a
 * block/row-level element, not an inline styling/link tag. */
bool isBlockContainerTagName(const std::string& lowerName) {
  static const char* const kBlockTags[] = {"li", "p", "div", "dd", "td", "aside", "blockquote", "section"};
  for (const char* t : kBlockTags) {
    if (lowerName == t) {
      return true;
    }
  }
  return false;
}

/** Reads the tag name starting right after an opening '<' at `ltPos` (skipping a leading '/' for a
 * closing tag). Returns {name, isClosingTag}. */
std::pair<std::string, bool> readTagNameAt(const std::string& html, const size_t ltPos) {
  size_t nameStart = ltPos + 1;
  const bool closing = nameStart < html.size() && html[nameStart] == '/';
  if (closing) {
    ++nameStart;
  }
  size_t nameEnd = nameStart;
  while (nameEnd < html.size() && isTagNameChar(html[nameEnd])) {
    ++nameEnd;
  }
  return {html.substr(nameStart, nameEnd - nameStart), closing};
}

/** Finds the next `id="target"` / `id='target'` attribute occurrence at or after `from`, and confirms
 * it sits inside a tag (i.e. there's an unmatched '<' before it with no '>' in between) - skips any
 * hit that isn't, in case the id string ever appears literally in body text. Returns the position of
 * the opening '<' of that tag, or npos if no valid match remains. */
size_t findTagStartForId(const std::string& html, const std::string& targetId, size_t from) {
  const std::string doubleQuoted = "id=\"" + targetId + "\"";
  const std::string singleQuoted = "id='" + targetId + "'";
  while (from < html.size()) {
    const size_t dq = html.find(doubleQuoted, from);
    const size_t sq = html.find(singleQuoted, from);
    size_t idPos = std::min(dq, sq);
    if (idPos == std::string::npos) {
      return std::string::npos;
    }
    const size_t lastOpen = html.rfind('<', idPos);
    const size_t lastClose = html.rfind('>', idPos);
    if (lastOpen != std::string::npos && (lastClose == std::string::npos || lastOpen > lastClose)) {
      return lastOpen;
    }
    from = idPos + 1;
  }
  return std::string::npos;
}

/** Many real EPUBs (Adobe InDesign/Calibre/Pandoc exports) put a footnote's `id` on a short inline
 * backlink anchor (e.g. `<a id="fn1" role="doc-backlink"><b>marker</b></a>`) with the actual note text
 * following as sibling content in the same enclosing block, NOT nested inside that anchor. When the
 * id-matched tag is itself an inline tag, walk backward to find the nearest still-open enclosing block
 * container (li/p/div/...) so the extracted fragment includes that trailing sibling text. Returns the
 * position of the container's opening '<', or npos if none is found within the search limit (falls
 * back to the id-matched tag itself in that case). */
size_t findEnclosingBlockStart(const std::string& html, const size_t pos) {
  const size_t limitPos = (pos > kBackwardBlockSearchLimit) ? pos - kBackwardBlockSearchLimit : 0;
  size_t searchFrom = pos;
  while (searchFrom > limitPos) {
    const size_t lt = html.rfind('<', searchFrom - 1);
    if (lt == std::string::npos || lt < limitPos) {
      break;
    }
    const std::pair<std::string, bool> tag = readTagNameAt(html, lt);
    const std::string lowerName = lowerAscii(tag.first);
    if (!tag.second && isBlockContainerTagName(lowerName)) {
      // Only a real enclosing container if it's still open at `pos` - i.e. its own close tag (if any)
      // doesn't appear before pos, which would mean this candidate already ended before our target.
      const std::string closeTag = "</" + lowerName;
      const size_t closeBeforePos = html.find(closeTag, lt);
      if (closeBeforePos == std::string::npos || closeBeforePos >= pos) {
        return lt;
      }
    }
    searchFrom = lt;
  }
  return std::string::npos;
}

/** Given a tag's opening '<' position and name, finds its matching close tag (tracking same-name
 * nesting depth) and returns the raw markup strictly between them. "" if self-closing or malformed. */
std::string innerHtmlOfTagAt(const std::string& html, const size_t tagStart, const std::string& tagName) {
  const size_t nameEnd = tagStart + 1 + tagName.size();
  const size_t openTagEnd = html.find('>', nameEnd);
  if (openTagEnd == std::string::npos) {
    return "";
  }
  if (openTagEnd > 0 && html[openTagEnd - 1] == '/') {
    return "";  // self-closing - no inner content
  }

  const std::string openPrefix = "<" + tagName;
  const std::string closeTag = "</" + tagName;
  size_t pos = openTagEnd + 1;
  int depth = 1;
  while (pos < html.size()) {
    const size_t nextOpen = html.find(openPrefix, pos);
    const size_t nextClose = html.find(closeTag, pos);
    if (nextClose == std::string::npos) {
      return "";  // no matching close tag - malformed, bail rather than guess
    }
    if (nextOpen != std::string::npos && nextOpen < nextClose) {
      const size_t after = nextOpen + openPrefix.size();
      const char c = after < html.size() ? html[after] : ' ';
      // Only count it as a nested same-name tag if the name actually ends here (not e.g. <tagName2>).
      if (!isTagNameChar(c)) {
        ++depth;
      }
      pos = after;
      continue;
    }
    --depth;
    if (depth == 0) {
      return html.substr(openTagEnd + 1, nextClose - (openTagEnd + 1));
    }
    pos = nextClose + closeTag.size();
  }
  return "";
}

}  // namespace

std::string extractElementInnerHtmlById(const std::string& html, const std::string& targetId) {
  if (targetId.empty()) {
    return "";
  }
  const size_t idTagStart = findTagStartForId(html, targetId, 0);
  if (idTagStart == std::string::npos) {
    return "";
  }
  const std::pair<std::string, bool> idTag = readTagNameAt(html, idTagStart);
  if (idTag.first.empty() || idTag.second) {
    return "";
  }

  // If the id sits on a block container itself (e.g. <aside epub:type="footnote" id="fn1">), that's
  // the whole footnote - use it directly. Otherwise (id on an inline marker/backlink tag), the real
  // text is most likely sibling content in the enclosing block - use that instead.
  if (isBlockContainerTagName(lowerAscii(idTag.first))) {
    return innerHtmlOfTagAt(html, idTagStart, idTag.first);
  }
  const size_t blockStart = findEnclosingBlockStart(html, idTagStart);
  if (blockStart == std::string::npos) {
    return innerHtmlOfTagAt(html, idTagStart, idTag.first);
  }
  const std::pair<std::string, bool> blockTag = readTagNameAt(html, blockStart);
  if (blockTag.first.empty() || blockTag.second) {
    return innerHtmlOfTagAt(html, idTagStart, idTag.first);
  }
  return innerHtmlOfTagAt(html, blockStart, blockTag.first);
}
