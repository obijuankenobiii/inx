/**
 * @file DictionaryDefinitionLayout.cpp
 * @brief Definitions for the shared dictionary-definition HTML parser/layout.
 */

#include "DictionaryDefinitionLayout.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "system/Fonts.h"

namespace {

std::string decodeHtmlEntities(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '&') {
      const size_t semi = s.find(';', i);
      if (semi != std::string::npos && semi - i <= 10) {
        const std::string entity = s.substr(i + 1, semi - i - 1);
        if (entity == "amp") {
          out += '&';
          i = semi + 1;
          continue;
        }
        if (entity == "lt") {
          out += '<';
          i = semi + 1;
          continue;
        }
        if (entity == "gt") {
          out += '>';
          i = semi + 1;
          continue;
        }
        if (entity == "quot") {
          out += '"';
          i = semi + 1;
          continue;
        }
        if (entity == "apos" || entity == "#39") {
          out += '\'';
          i = semi + 1;
          continue;
        }
        if (entity == "nbsp") {
          out += ' ';
          i = semi + 1;
          continue;
        }
      }
    }
    out += s[i];
    ++i;
  }
  return out;
}

/** Appends c to the current run's text, collapsing runs of whitespace (including raw source
 *  newlines/tabs, which are just HTML source formatting, not real line breaks) down to a single
 *  space, and never starting a block with leading whitespace. Looks at the last character across ALL
 *  of the block's runs (not just the current one) so collapsing still works across a style change,
 *  e.g. "hello <b> world</b>" shouldn't keep the space right after <b>. Deliberate '\n' breaks (from
 *  <br>) are appended directly by the caller instead of going through this. */
void appendCollapsedChar(DefinitionBlock& block, char c) {
  if (c == '\n' || c == '\r' || c == '\t') {
    c = ' ';
  }
  char lastChar = '\0';
  for (auto it = block.runs.rbegin(); it != block.runs.rend(); ++it) {
    if (!it->text.empty()) {
      lastChar = it->text.back();
      break;
    }
  }
  if (c == ' ' && (lastChar == '\0' || lastChar == ' ' || lastChar == '\n')) {
    return;
  }
  block.runs.back().text += c;
}

/** Splits a block's style runs into atoms (words, carrying their run's style) plus hard-break atoms
 *  for embedded '\n's, tracking whether each atom had a space before it (false only when two runs
 *  are glued together with no space between them, e.g. a style change mid-word). */
std::vector<DefinitionTextAtom> tokenizeBlock(const DefinitionBlock& block) {
  std::vector<DefinitionTextAtom> atoms;
  bool pendingSpace = false;
  bool isFirstAtom = true;
  for (const DefinitionTextRun& run : block.runs) {
    size_t i = 0;
    while (i < run.text.size()) {
      if (run.text[i] == '\n') {
        atoms.push_back(DefinitionTextAtom{"", EpdFontFamily::REGULAR, true, false});
        ++i;
        pendingSpace = false;
        isFirstAtom = true;
        continue;
      }
      if (run.text[i] == ' ') {
        pendingSpace = true;
        ++i;
        continue;
      }
      const size_t start = i;
      while (i < run.text.size() && run.text[i] != ' ' && run.text[i] != '\n') {
        ++i;
      }
      DefinitionTextAtom atom;
      atom.text = run.text.substr(start, i - start);
      atom.style = run.style;
      atom.spaceBefore = !isFirstAtom && pendingSpace;
      atoms.push_back(std::move(atom));
      pendingSpace = false;
      isFirstAtom = false;
    }
  }
  return atoms;
}

/** Greedy word-wrap of a block's atoms into lines no wider than maxWidth, breaking only where an
 *  atom has spaceBefore (or at a hard break) so mid-word style changes never split across lines. */
std::vector<DefinitionStyledLine> wrapAtomsToWidth(const GfxRenderer& renderer,
                                                   const std::vector<DefinitionTextAtom>& atoms, const int fontId,
                                                   const int indentPx, const int maxWidth) {
  std::vector<DefinitionStyledLine> lines;
  DefinitionStyledLine current{{}, fontId, indentPx, 0};
  int currentWidth = 0;
  const int spaceW = renderer.text.getSpaceWidth(fontId);

  auto flushLine = [&]() {
    if (!current.atoms.empty()) {
      lines.push_back(std::move(current));
    }
    current = DefinitionStyledLine{{}, fontId, indentPx, 0};
    currentWidth = 0;
  };

  for (const DefinitionTextAtom& atom : atoms) {
    if (atom.hardBreak) {
      flushLine();
      continue;
    }
    const int atomW = renderer.text.getWidth(fontId, atom.text.c_str(), atom.style);
    const int extra = (atom.spaceBefore && !current.atoms.empty()) ? spaceW : 0;
    if (!current.atoms.empty() && currentWidth + extra + atomW > maxWidth) {
      flushLine();
      DefinitionTextAtom first = atom;
      first.spaceBefore = false;
      currentWidth = atomW;
      current.atoms.push_back(std::move(first));
    } else {
      currentWidth += extra + atomW;
      current.atoms.push_back(atom);
    }
  }
  flushLine();
  return lines;
}

int fontIdForBlock(const DefinitionBlock& block) {
  if (block.kind != DefinitionBlockKind::Heading) {
    return ATKINSON_HYPERLEGIBLE_10_FONT_ID;
  }
  if (block.headingLevel <= 1) {
    return ATKINSON_HYPERLEGIBLE_16_FONT_ID;
  }
  if (block.headingLevel == 2) {
    return ATKINSON_HYPERLEGIBLE_14_FONT_ID;
  }
  return ATKINSON_HYPERLEGIBLE_12_FONT_ID;
}

std::string asciiLowerCopy(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

/** Drops WikDict / FreeDict IPA: gray <font> runs and slash-wrapped transcriptions like /ti.t/. */
std::string stripPhoneticMarkup(const std::string& html) {
  std::string withoutFont;
  withoutFont.reserve(html.size());
  size_t i = 0;
  while (i < html.size()) {
    if (html[i] == '<') {
      const size_t close = html.find('>', i);
      if (close == std::string::npos) {
        break;
      }
      const std::string inner = asciiLowerCopy(html.substr(i + 1, close - i - 1));
      if (inner.compare(0, 4, "font") == 0 && inner.find("gray") != std::string::npos) {
        if (!withoutFont.empty() && withoutFont.back() == '/') {
          withoutFont.pop_back();
        }
        size_t end = close + 1;
        while (end < html.size()) {
          const size_t next = html.find('<', end);
          if (next == std::string::npos) {
            end = html.size();
            break;
          }
          const size_t nextClose = html.find('>', next);
          if (nextClose == std::string::npos) {
            end = html.size();
            break;
          }
          const std::string nextInner = asciiLowerCopy(html.substr(next + 1, nextClose - next - 1));
          end = nextClose + 1;
          if (nextInner == "/font") {
            break;
          }
        }
        i = end;
        while (i < html.size() && (html[i] == '/' || html[i] == ',' || html[i] == ' ')) {
          ++i;
        }
        continue;
      }
    }
    withoutFont.push_back(html[i]);
    ++i;
  }

  std::string out;
  out.reserve(withoutFont.size());
  i = 0;
  while (i < withoutFont.size()) {
    if (withoutFont[i] == '/') {
      size_t j = i + 1;
      while (j < withoutFont.size() && withoutFont[j] != '/' && withoutFont[j] != '<') {
        ++j;
      }
      if (j < withoutFont.size() && withoutFont[j] == '/' && (j - i) < 48) {
        i = j + 1;
        while (i < withoutFont.size() && (withoutFont[i] == ',' || withoutFont[i] == ' ')) {
          ++i;
        }
        continue;
      }
    }
    out.push_back(withoutFont[i]);
    ++i;
  }
  return out;
}

}  // namespace

std::vector<DefinitionBlock> parseHtmlToBlocks(const std::string& html) {
  const std::string source = stripPhoneticMarkup(html);
  std::vector<DefinitionBlock> blocks;
  DefinitionBlock current;
  current.runs.push_back(DefinitionTextRun{});
  int boldDepth = 0;
  int italicDepth = 0;

  auto currentStyle = [&]() -> EpdFontFamily::Style {
    const bool bold = boldDepth > 0 || current.kind == DefinitionBlockKind::Heading;
    const bool italic = italicDepth > 0;
    if (bold && italic) {
      return EpdFontFamily::BOLD_ITALIC;
    }
    if (bold) {
      return EpdFontFamily::BOLD;
    }
    if (italic) {
      return EpdFontFamily::ITALIC;
    }
    return EpdFontFamily::REGULAR;
  };

  auto ensureRunStyle = [&]() {
    if (current.runs.back().style != currentStyle()) {
      current.runs.push_back(DefinitionTextRun{"", currentStyle()});
    }
  };

  auto flush = [&]() {
    // Drop trailing empty/whitespace-only runs (find_last_not_of returns npos for both cases), then
    // trim trailing whitespace off whatever real run is left at the end.
    while (!current.runs.empty() && current.runs.back().text.find_last_not_of(" \n") == std::string::npos) {
      current.runs.pop_back();
    }
    if (!current.runs.empty()) {
      std::string& t = current.runs.back().text;
      while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) {
        t.pop_back();
      }
    }
    const bool hasContent =
        std::any_of(current.runs.begin(), current.runs.end(), [](const DefinitionTextRun& r) { return !r.text.empty(); });
    if (hasContent) {
      blocks.push_back(current);
    }
    current = DefinitionBlock{};
    current.runs.push_back(DefinitionTextRun{});
    boldDepth = 0;
    italicDepth = 0;
  };

  size_t i = 0;
  while (i < source.size()) {
    if (source[i] == '<') {
      const size_t close = source.find('>', i);
      if (close == std::string::npos) {
        break;  // unterminated tag - stop rather than emit garbage
      }
      std::string tag = source.substr(i + 1, close - i - 1);
      i = close + 1;
      const bool closing = !tag.empty() && tag[0] == '/';
      if (closing) {
        tag.erase(0, 1);
      }
      const size_t space = tag.find_first_of(" \t");
      if (space != std::string::npos) {
        tag = tag.substr(0, space);
      }
      if (!tag.empty() && tag.back() == '/') {
        tag.pop_back();
      }
      std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) { return std::tolower(c); });

      if (tag == "br") {
        ensureRunStyle();
        if (!current.runs.back().text.empty() && current.runs.back().text.back() != '\n') {
          current.runs.back().text += '\n';
        }
        continue;
      }
      if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
        flush();
        if (!closing) {
          current.kind = DefinitionBlockKind::Heading;
          current.headingLevel = tag[1] - '0';
        }
        continue;
      }
      if (tag == "li") {
        flush();
        if (!closing) {
          current.kind = DefinitionBlockKind::ListItem;
        }
        continue;
      }
      if (tag == "p" || tag == "div" || tag == "ul" || tag == "ol") {
        flush();
        continue;
      }
      if (tag == "b" || tag == "strong") {
        boldDepth = closing ? std::max(0, boldDepth - 1) : boldDepth + 1;
        continue;
      }
      if (tag == "i" || tag == "em") {
        italicDepth = closing ? std::max(0, italicDepth - 1) : italicDepth + 1;
        continue;
      }
      // Any other tag (u, span, font, tt, sub, sup, etc.) - strip, keep inline text content.
      continue;
    }
    ensureRunStyle();
    appendCollapsedChar(current, source[i]);
    ++i;
  }
  flush();

  for (DefinitionBlock& block : blocks) {
    for (DefinitionTextRun& run : block.runs) {
      run.text = decodeHtmlEntities(run.text);
    }
  }
  return blocks;
}

std::vector<DefinitionStyledLine> layoutDefinitionBlocks(const GfxRenderer& renderer,
                                                         const std::vector<DefinitionBlock>& blocks,
                                                         const int maxWidth) {
  constexpr int kListIndentPx = 14;
  constexpr int kBlockGapPx = 4;

  std::vector<DefinitionStyledLine> styledLines;
  for (size_t bi = 0; bi < blocks.size(); ++bi) {
    const DefinitionBlock& block = blocks[bi];
    const int fontId = fontIdForBlock(block);
    const int indent = block.kind == DefinitionBlockKind::ListItem ? kListIndentPx : 0;

    auto atoms = tokenizeBlock(block);
    if (block.kind == DefinitionBlockKind::ListItem && !atoms.empty()) {
      atoms.insert(atoms.begin(), DefinitionTextAtom{"\xE2\x80\xA2", EpdFontFamily::REGULAR, false, false});
      atoms[1].spaceBefore = true;
    }

    auto wrapped = wrapAtomsToWidth(renderer, atoms, fontId, indent, maxWidth - indent);
    for (size_t li = 0; li < wrapped.size(); ++li) {
      wrapped[li].extraGapBeforePx = (li == 0 && bi > 0) ? kBlockGapPx : 0;
      styledLines.push_back(std::move(wrapped[li]));
    }
  }
  return styledLines;
}

void renderStyledLines(GfxRenderer& renderer, const std::vector<DefinitionStyledLine>& lines, const int x,
                       const int startY, const int bottomLimit, const size_t startIndex) {
  int y = startY;
  for (size_t i = startIndex; i < lines.size(); ++i) {
    const DefinitionStyledLine& sl = lines[i];
    const int lineH = renderer.text.getLineHeight(sl.fontId);
    const int gap = (i == startIndex) ? 0 : sl.extraGapBeforePx;
    if (y + gap + lineH > bottomLimit) {
      break;
    }
    y += gap;
    int lineX = x + sl.indentPx;
    const int spaceW = renderer.text.getSpaceWidth(sl.fontId);
    for (size_t ai = 0; ai < sl.atoms.size(); ++ai) {
      const DefinitionTextAtom& atom = sl.atoms[ai];
      if (ai > 0 && atom.spaceBefore) {
        lineX += spaceW;
      }
      renderer.text.render(sl.fontId, lineX, y, atom.text.c_str(), true, atom.style);
      lineX += renderer.text.getWidth(sl.fontId, atom.text.c_str(), atom.style);
    }
    y += lineH;
  }
}

namespace {

std::string htmlToCollapsedPlain(const std::string& html) {
  const std::string source = stripPhoneticMarkup(html);
  std::string out;
  out.reserve(source.size());
  bool inTag = false;
  for (unsigned char c : source) {
    if (c == '<') {
      inTag = true;
      continue;
    }
    if (c == '>') {
      inTag = false;
      if (!out.empty() && out.back() != ' ') {
        out.push_back(' ');
      }
      continue;
    }
    if (inTag) {
      continue;
    }
    if (c <= ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!out.empty() && out.back() != ' ') {
        out.push_back(' ');
      }
      continue;
    }
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<unsigned char>(c - 'A' + 'a');
    }
    out.push_back(static_cast<char>(c));
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return decodeHtmlEntities(out);
}

const char* const kFormOfMarkers[] = {
    "past participle of",
    "present participle of",
    "gerund of",
    "simple past of",
    "past tense of",
    "third-person singular of",
    "third person singular of",
    "third-person singular",
    "inflected form of",
    "conjugated form of",
    "comparative of",
    "superlative of",
    "plural of",
    "voltooid deelwoord van",
    "tegenwoordig deelwoord van",
    "onvoltooid deelwoord van",
    "verleden tijd van",
    "meervoud van",
    "verkleinwoord van",
    "vervoeging van",
    "verbuiging van",
    "participe passe de",
    "forme flechie de",
};

bool isWordChar(const unsigned char c) {
  return c >= 0x80 || std::isalnum(c) != 0 || c == '\'' || c == '-';
}

void skipSpaces(const std::string& s, size_t& i) {
  while (i < s.size() && s[i] == ' ') {
    ++i;
  }
}

bool consumeWord(const std::string& s, size_t& i, const char* word) {
  const size_t n = std::strlen(word);
  if (i + n <= s.size() && s.compare(i, n, word) == 0) {
    const size_t after = i + n;
    if (after == s.size() || s[after] == ' ') {
      i = after;
      skipSpaces(s, i);
      return true;
    }
  }
  return false;
}

std::string firstLemmaToken(const std::string& s, size_t i) {
  skipSpaces(s, i);
  (void)consumeWord(s, i, "to");
  (void)consumeWord(s, i, "the");
  (void)consumeWord(s, i, "a");
  (void)consumeWord(s, i, "an");
  (void)consumeWord(s, i, "het");
  (void)consumeWord(s, i, "de");
  (void)consumeWord(s, i, "een");
  const size_t start = i;
  while (i < s.size() && isWordChar(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
  if (i - start < 2) {
    return "";
  }
  return s.substr(start, i - start);
}

void eraseFormOfClauses(std::string& plain) {
  for (const char* marker : kFormOfMarkers) {
    const size_t markerLen = std::strlen(marker);
    size_t p = 0;
    while ((p = plain.find(marker, p)) != std::string::npos) {
      size_t end = p + markerLen;
      skipSpaces(plain, end);
      int words = 0;
      while (end < plain.size() && words < 4) {
        if (plain[end] == ' ') {
          ++words;
          skipSpaces(plain, end);
          continue;
        }
        if (!isWordChar(static_cast<unsigned char>(plain[end]))) {
          break;
        }
        ++end;
      }
      plain.erase(p, end - p);
    }
  }
}

}  // namespace

std::string lemmaFromDefinition(const std::string& html) {
  const std::string plain = htmlToCollapsedPlain(html);
  for (const char* marker : kFormOfMarkers) {
    const size_t p = plain.find(marker);
    if (p == std::string::npos) {
      continue;
    }
    const std::string lemma = firstLemmaToken(plain, p + std::strlen(marker));
    if (!lemma.empty()) {
      return lemma;
    }
  }
  return "";
}

bool definitionHasUsefulGloss(const std::string& html) {
  std::string plain = htmlToCollapsedPlain(html);
  if (plain.empty()) {
    return false;
  }
  eraseFormOfClauses(plain);

  static const char* kPos[] = {"verb",
                               "noun",
                               "adjective",
                               "adverb",
                               "pronoun",
                               "preposition",
                               "conjunction",
                               "interjection",
                               "article",
                               "determiner",
                               "werkwoord",
                               "zelfstandig naamwoord",
                               "bijvoeglijk naamwoord",
                               "bijwoord",
                               "lidwoord",
                               "verbe",
                               "nom",
                               "adjectif",
                               "adverbe",
                               "substantief"};
  for (const char* pos : kPos) {
    const size_t n = std::strlen(pos);
    size_t p = 0;
    while ((p = plain.find(pos, p)) != std::string::npos) {
      const bool startOk = p == 0 || plain[p - 1] == ' ';
      const bool endOk = p + n == plain.size() || plain[p + n] == ' ';
      if (startOk && endOk) {
        plain.erase(p, n);
      } else {
        ++p;
      }
    }
  }

  int letters = 0;
  for (unsigned char c : plain) {
    if (std::isalnum(c) != 0 || c >= 0x80) {
      ++letters;
    }
  }
  return letters >= 24;
}
