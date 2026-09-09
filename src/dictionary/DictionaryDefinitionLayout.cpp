/**
 * @file DictionaryDefinitionLayout.cpp
 * @brief Definitions for the shared dictionary-definition HTML parser/layout.
 */

#include "DictionaryDefinitionLayout.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

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
    return MONTSERRAT_10_FONT_ID;
  }
  if (block.headingLevel <= 1) {
    return MONTSERRAT_16_FONT_ID;
  }
  if (block.headingLevel == 2) {
    return MONTSERRAT_14_FONT_ID;
  }
  return MONTSERRAT_12_FONT_ID;
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

namespace {

struct DictionarySense {
  std::string gloss;
  std::vector<std::string> examples;
};

struct PosInfo {
  std::string label;
  std::string gender;
};

struct DictionarySection {
  PosInfo pos;
  std::string grammar;
  std::vector<DictionarySense> senses;
};

struct DictionaryCard {
  std::vector<DictionarySection> sections;
};

std::string trimCopy(std::string s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\n' || s.front() == '\t')) {
    s.erase(s.begin());
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\t' || s.back() == ',' ||
                        s.back() == ';' || s.back() == ':')) {
    s.pop_back();
  }
  return s;
}

std::string asciiLowerToken(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool blockAllItalic(const DefinitionBlock& block) {
  bool any = false;
  for (const DefinitionTextRun& run : block.runs) {
    if (run.text.empty()) {
      continue;
    }
    any = true;
    if (run.style != EpdFontFamily::ITALIC && run.style != EpdFontFamily::BOLD_ITALIC) {
      return false;
    }
  }
  return any;
}

size_t letterCount(const std::string& s) {
  size_t n = 0;
  for (unsigned char c : s) {
    if (std::isalnum(c) != 0 || c >= 0x80) {
      ++n;
    }
  }
  return n;
}

int countWords(const std::string& s) {
  int n = 0;
  bool inWord = false;
  for (unsigned char c : s) {
    if (c > ' ') {
      if (!inWord) {
        ++n;
        inWord = true;
      }
    } else {
      inWord = false;
    }
  }
  return n;
}

bool isLanguageCode(const std::string& lower) {
  static const char* kCodes[] = {"en", "eng", "english", "fr", "fra", "fre", "french", "nl", "dut",
                                 "dutch", "la", "lat", "latin", "ger", "german", "it", "es", "pt"};
  for (const char* c : kCodes) {
    if (lower == c) {
      return true;
    }
  }
  return false;
}

void stripTrailingDot(std::string& s) {
  while (!s.empty() && s.back() == '.') {
    s.pop_back();
  }
}

void stripTokenPunct(std::string& s) {
  while (!s.empty()) {
    const char c = s.back();
    if (c == '.' || c == ',' || c == ';' || c == ':' || c == ')' || c == '(' || c == '/') {
      s.pop_back();
    } else {
      break;
    }
  }
  while (!s.empty()) {
    const char c = s.front();
    if (c == '(' || c == ')' || c == ',' || c == '/') {
      s.erase(s.begin());
    } else {
      break;
    }
  }
}

bool genderFromToken(const std::string& lowerRaw, std::string& gender) {
  std::string lower = lowerRaw;
  stripTokenPunct(lower);
  stripTrailingDot(lower);
  if (lower == "f" || lower == "fem" || lower == "feminine" || lower == "female" || lower == "vrouwelijk" ||
      lower == "feminin" || lower == "féminin" || lower == "vrl" || lower == "v") {
    gender = "f";
    return true;
  }
  if (lower == "m" || lower == "masc" || lower == "masculine" || lower == "male" || lower == "mannelijk" ||
      lower == "masculin" || lower == "mnl") {
    gender = "m";
    return true;
  }
  if (lower == "n" || lower == "neut" || lower == "neuter" || lower == "onzijdig") {
    gender = "n";
    return true;
  }
  return false;
}

bool posFromToken(const std::string& raw, PosInfo& pos) {
  const std::string lowerRaw = asciiLowerToken(raw);
  if (isLanguageCode(lowerRaw)) {
    return false;
  }
  std::string lower = lowerRaw;
  stripTokenPunct(lower);
  stripTrailingDot(lower);

  if (lower == "znw" || lower == "zelfstandig naamwoord" || lower == "noun" || lower == "nom" || lower == "substantief" ||
      lower == "subst" || lower == "s" || lower == "nm" || lower == "nf" || lower == "nn" || lower == "n.m" ||
      lower == "n.f" || lower == "s.m" || lower == "s.f") {
    pos.label = "noun";
    if (lower == "nf" || lower == "n.f" || lower == "s.f") {
      pos.gender = "f";
    }
    if (lower == "nm" || lower == "n.m" || lower == "s.m") {
      pos.gender = "m";
    }
    return true;
  }
  if (lower == "n") {
    pos.label = "noun";
    return true;
  }
  if (lower == "bn" || lower == "bijvoeglijk naamwoord" || lower == "adjective" || lower == "adjectif" ||
      lower == "adj" || lower == "a") {
    pos.label = "adjective";
    return true;
  }
  if (lower == "ww" || lower == "werkwoord" || lower == "verb" || lower == "verbe" || lower == "vb") {
    pos.label = "verb";
    return true;
  }
  if (lower == "bw" || lower == "bijwoord" || lower == "adverb" || lower == "adverbe" || lower == "adv") {
    pos.label = "adverb";
    return true;
  }
  if (lower == "pronoun" || lower == "pron" || lower == "voornaamwoord" || lower == "vn" || lower == "vnw") {
    pos.label = "pronoun";
    return true;
  }
  if (lower == "preposition" || lower == "prep" || lower == "voorzetsel" || lower == "vz") {
    pos.label = "preposition";
    return true;
  }
  if (lower == "conjunction" || lower == "conj" || lower == "voegwoord" || lower == "vg") {
    pos.label = "conjunction";
    return true;
  }
  if (lower == "interjection" || lower == "int" || lower == "tussenwerpsel" || lower == "tw") {
    pos.label = "interjection";
    return true;
  }
  if (lower == "article" || lower == "lidwoord" || lower == "lw") {
    pos.label = "article";
    return true;
  }
  return false;
}

const char* posAbbrev(const std::string& canonical, const bool dutch) {
  if (canonical == "noun") {
    return dutch ? "znw" : "noun";
  }
  if (canonical == "adjective") {
    return dutch ? "bn" : "adjective";
  }
  if (canonical == "verb") {
    return dutch ? "ww" : "verb";
  }
  if (canonical == "adverb") {
    return dutch ? "bw" : "adverb";
  }
  if (canonical == "pronoun") {
    return dutch ? "vnw" : "pronoun";
  }
  if (canonical == "preposition") {
    return dutch ? "vz" : "preposition";
  }
  if (canonical == "conjunction") {
    return dutch ? "vg" : "conjunction";
  }
  if (canonical == "interjection") {
    return dutch ? "tw" : "interjection";
  }
  if (canonical == "article") {
    return dutch ? "lw" : "article";
  }
  return canonical.c_str();
}

const char* genderAbbrev(const std::string& gender, const bool dutch) {
  if (gender == "f/m" || gender == "m/f") {
    return dutch ? "v/m" : "f/m";
  }
  if (gender == "f" || gender == "feminine") {
    return dutch ? "v" : "f";
  }
  if (gender == "m" || gender == "masculine") {
    return dutch ? "m" : "m";
  }
  if (gender == "n" || gender == "neuter") {
    return dutch ? "o" : "n";
  }
  return gender.c_str();
}

bool tokenIsGenderLetter(const std::string& raw) {
  std::string lower = asciiLowerToken(raw);
  stripTrailingDot(lower);
  return lower == "f" || lower == "m" || lower == "n" || lower == "v";
}

std::string formatPos(const DictionarySection& section, const bool dutch) {
  std::string out;
  if (!section.pos.label.empty()) {
    out = posAbbrev(section.pos.label, dutch);
  }
  if (!section.pos.gender.empty()) {
    if (!out.empty()) {
      out += " ";
    }
    out += genderAbbrev(section.pos.gender, dutch);
  }
  std::string grammar = trimCopy(section.grammar);
  if (!grammar.empty()) {
    // Drop gender letters already shown on the POS line (ae f → ae).
    std::string cleaned;
    size_t i = 0;
    while (i < grammar.size()) {
      while (i < grammar.size() && grammar[i] == ' ') {
        ++i;
      }
      if (i >= grammar.size()) {
        break;
      }
      const size_t start = i;
      while (i < grammar.size() && grammar[i] != ' ' && grammar[i] != ',') {
        ++i;
      }
      const std::string tok = grammar.substr(start, i - start);
      if (i < grammar.size() && grammar[i] == ',') {
        ++i;
      }
      if (tokenIsGenderLetter(tok)) {
        continue;
      }
      if (!cleaned.empty()) {
        cleaned += " ";
      }
      cleaned += tok;
    }
    grammar = trimCopy(std::move(cleaned));
  }
  if (!grammar.empty()) {
    if (!out.empty()) {
      out += "  ";
    }
    out += grammar;
  }
  return out;
}

void mergePos(PosInfo& dest, const PosInfo& src) {
  if (dest.label.empty()) {
    dest.label = src.label;
  }
  if (dest.gender.empty()) {
    dest.gender = src.gender;
  }
}

size_t nextTokenEnd(const std::string& text, const size_t start) {
  size_t i = start;
  while (i < text.size() && text[i] != ' ') {
    ++i;
  }
  return i;
}

bool consumeLanguagePrefix(std::string& text) {
  const size_t end = nextTokenEnd(text, 0);
  if (end == 0) {
    return false;
  }
  std::string tok = asciiLowerToken(text.substr(0, end));
  stripTokenPunct(tok);
  stripTrailingDot(tok);
  if (!isLanguageCode(tok)) {
    return false;
  }
  text = trimCopy(text.substr(end));
  return true;
}

bool consumeLeadingPos(std::string& text, PosInfo& pos) {
  bool any = false;
  while (consumeLanguagePrefix(text)) {
  }
  static const char* kPhrases[] = {"zelfstandig naamwoord", "bijvoeglijk naamwoord"};
  const std::string lower = asciiLowerToken(text);
  for (const char* phrase : kPhrases) {
    const size_t n = std::strlen(phrase);
    if (lower.compare(0, n, phrase) == 0 && (lower.size() == n || lower[n] == ' ')) {
      PosInfo hit;
      posFromToken(phrase, hit);
      mergePos(pos, hit);
      text = trimCopy(text.substr(n));
      any = true;
    }
  }
  for (int step = 0; step < 6; ++step) {
    const size_t end = nextTokenEnd(text, 0);
    if (end == 0) {
      break;
    }
    const std::string tok = text.substr(0, end);
    PosInfo hit;
    std::string gender;
    std::string lowerTok = asciiLowerToken(tok);
    stripTokenPunct(lowerTok);
    stripTrailingDot(lowerTok);
    if (lowerTok == "poss" || lowerTok == "possessive" || lowerTok == "bezittelijk" || lowerTok == "bez") {
      if (pos.label.empty()) {
        pos.label = "pronoun";
      }
      text = trimCopy(text.substr(end));
      any = true;
      continue;
    }
    if (posFromToken(tok, hit)) {
      mergePos(pos, hit);
      text = trimCopy(text.substr(end));
      any = true;
      continue;
    }
    if (genderFromToken(asciiLowerToken(tok), gender)) {
      if (pos.gender.empty()) {
        pos.gender = gender;
      }
      text = trimCopy(text.substr(end));
      any = true;
      continue;
    }
    break;
  }
  return any;
}

bool isLatinGrammarLine(const std::string& text, std::string& grammarOut) {
  std::string s = trimCopy(text);
  if (s.size() < 3 || s.size() > 48) {
    return false;
  }
  const size_t comma = s.find(',');
  if (comma == std::string::npos) {
    return false;
  }
  std::string rest = trimCopy(s.substr(comma + 1));
  if (rest.size() < 3 || rest.size() > 24) {
    return false;
  }
  const std::string lower = asciiLowerToken(rest);
  const bool hasGender = lower.find(" f") != std::string::npos || lower == "f." || lower == "f" ||
                         lower.find(" m") != std::string::npos || lower.find(", f") != std::string::npos ||
                         lower.find(", m") != std::string::npos || lower.find(", n") != std::string::npos ||
                         (lower.size() >= 2 && (lower.back() == 'f' || lower.back() == 'm' || lower.back() == 'n' ||
                                                (lower.size() >= 3 && lower[lower.size() - 2] == '.' &&
                                                 (lower.back() == 'f' || lower.back() == 'm' || lower.back() == 'n'))));
  const bool hasEnding = rest.find("ae") != std::string::npos || rest.find("arum") != std::string::npos ||
                         rest.find("is") != std::string::npos || rest.find("um") != std::string::npos ||
                         rest.find("i,") != std::string::npos || rest.find("us") != std::string::npos;
  if (!hasGender && !hasEnding) {
    return false;
  }
  if (countWords(s) > 6) {
    return false;
  }
  grammarOut = rest;
  return true;
}

bool consumeArabicSenseNumber(std::string& text) {
  size_t i = 0;
  while (i < text.size() && text[i] == ' ') {
    ++i;
  }
  if (i >= text.size() || text[i] < '1' || text[i] > '9') {
    return false;
  }
  int n = 0;
  while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
    n = n * 10 + (text[i] - '0');
    ++i;
  }
  if (n <= 0 || n > 30) {
    return false;
  }
  if (i < text.size() && (text[i] == '.' || text[i] == ')' || text[i] == ':')) {
    ++i;
  }
  if (i < text.size() && text[i] != ' ') {
    return false;
  }
  const std::string rest = trimCopy(text.substr(i));
  if (rest.size() < 2) {
    return false;
  }
  text = rest;
  return true;
}

bool consumeRomanSenseNumber(std::string& text) {
  size_t i = 0;
  while (i < text.size() && text[i] == ' ') {
    ++i;
  }
  static const char* kRoman[] = {"VIII.", "VII.", "III.", "II.", "IV.", "IX.", "VI.", "I.", "X."};
  for (const char* r : kRoman) {
    const size_t n = std::strlen(r);
    if (text.compare(i, n, r) == 0) {
      const std::string rest = trimCopy(text.substr(i + n));
      if (letterCount(rest) < 3) {
        return false;
      }
      text = rest;
      return true;
    }
  }
  return false;
}

void splitNumberedInline(const std::string& text, std::vector<std::string>& parts) {
  auto pushRange = [&](const size_t from, const size_t to) {
    const std::string chunk = trimCopy(text.substr(from, to - from));
    if (!chunk.empty()) {
      parts.push_back(chunk);
    }
  };
  size_t start = 0;
  for (size_t i = 1; i + 2 < text.size(); ++i) {
    if (text[i - 1] != ' ') {
      continue;
    }
    if (text[i] >= '1' && text[i] <= '9') {
      size_t j = i;
      while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
        ++j;
      }
      if (j < text.size() && (text[j] == '.' || text[j] == ')') && j + 1 < text.size() && text[j + 1] == ' ') {
        pushRange(start, i);
        start = i;
      }
    }
    if (text[i] == 'I' || text[i] == 'V' || text[i] == 'X') {
      static const char* kRoman[] = {"VIII.", "VII.", "III.", "II.", "IV.", "IX.", "VI.", "I.", "X."};
      for (const char* r : kRoman) {
        const size_t n = std::strlen(r);
        if (text.compare(i, n, r) == 0 && i + n < text.size() && text[i + n] == ' ') {
          pushRange(start, i);
          start = i;
          break;
        }
      }
    }
  }
  pushRange(start, text.size());
}

std::vector<std::string> splitUnbracketed(const std::string& text, const char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  int depth = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '[' || c == '(') {
      ++depth;
    } else if ((c == ']' || c == ')') && depth > 0) {
      --depth;
    } else if (c == sep && depth == 0) {
      const std::string chunk = trimCopy(text.substr(start, i - start));
      if (!chunk.empty()) {
        out.push_back(chunk);
      }
      start = i + 1;
    }
  }
  const std::string tail = trimCopy(text.substr(start));
  if (!tail.empty()) {
    out.push_back(tail);
  }
  return out;
}

void splitLetterMarkers(const std::string& text, std::vector<std::string>& parts) {
  auto pushRange = [&](const size_t from, const size_t to) {
    const std::string chunk = trimCopy(text.substr(from, to - from));
    if (!chunk.empty()) {
      parts.push_back(chunk);
    }
  };
  size_t start = 0;
  for (size_t i = 0; i + 2 < text.size(); ++i) {
    if (text[i] != '(') {
      continue;
    }
    const char ch = text[i + 1];
    if (ch < 'a' || ch > 'f' || text[i + 2] != ')') {
      continue;
    }
    if (i > 0 && text[i - 1] != ' ' && text[i - 1] != ';' && text[i - 1] != ',') {
      continue;
    }
    if (i > start) {
      pushRange(start, i);
    }
    start = i + 3;
  }
  pushRange(start, text.size());
}

bool isMorphToken(const std::string& raw) {
  std::string t = asciiLowerToken(raw);
  while (!t.empty() && (t.back() == ',' || t.back() == '.')) {
    t.pop_back();
  }
  static const char* kMorph[] = {"a",  "um",  "us",   "i",    "ae",  "onis", "is", "es",
                                 "orum", "arum", "ibus", "e",    "ei",  "as",   "os", "uis"};
  for (const char* m : kMorph) {
    if (t == m) {
      return true;
    }
  }
  return false;
}

bool consumeLeadingMorphology(std::string& text, std::string& grammarOut) {
  const size_t comma = text.find(',');
  if (comma == std::string::npos || comma < 2 || comma > 36) {
    return false;
  }
  if (countWords(text.substr(0, comma)) != 1) {
    return false;
  }
  std::string rest = trimCopy(text.substr(comma + 1));
  std::string morph;
  int taken = 0;
  while (taken < 4 && !rest.empty()) {
    if (rest.front() == '(') {
      const size_t close = rest.find(')');
      if (close == std::string::npos || close > 28) {
        break;
      }
      const std::string paren = rest.substr(0, close + 1);
      if (!morph.empty()) {
        morph += " ";
      }
      morph += paren;
      rest = trimCopy(rest.substr(close + 1));
      ++taken;
      continue;
    }
    const size_t end = nextTokenEnd(rest, 0);
    std::string tok = rest.substr(0, end);
    while (!tok.empty() && tok.back() == ',') {
      tok.pop_back();
    }
    std::string gender;
    if (!isMorphToken(tok) && !genderFromToken(asciiLowerToken(tok), gender)) {
      break;
    }
    if (!morph.empty()) {
      morph += " ";
    }
    morph += tok;
    rest = trimCopy(rest.substr(end));
    ++taken;
  }
  if (taken == 0) {
    return false;
  }
  if (grammarOut.empty()) {
    grammarOut = morph;
  }
  text = rest;
  return true;
}

bool looksAttachedExample(const std::string& raw) {
  const std::string t = trimCopy(raw);
  if (t.empty()) {
    return false;
  }
  if (t[0] == '-' || t[0] == 'Q' || t[0] == 'q') {
    return true;
  }
  if (t.compare(0, 2, "Q ") == 0) {
    return true;
  }
  return false;
}

std::vector<std::string> splitEquivalents(const std::string& text) {
  std::vector<std::string> numbered;
  splitNumberedInline(text, numbered);
  if (numbered.size() > 1) {
    return numbered;
  }

  std::vector<std::string> letters;
  splitLetterMarkers(text, letters);
  if (letters.size() > 1) {
    return letters;
  }

  const std::vector<std::string> bySemi = splitUnbracketed(text, ';');
  if (bySemi.size() > 1) {
    std::vector<std::string> merged;
    merged.reserve(bySemi.size());
    for (const std::string& chunk : bySemi) {
      if (!merged.empty() && looksAttachedExample(chunk)) {
        merged.back() += "; ";
        merged.back() += chunk;
      } else {
        merged.push_back(chunk);
      }
    }
    return merged;
  }

  std::vector<std::string> one;
  if (!trimCopy(text).empty()) {
    one.push_back(trimCopy(text));
  }
  return one;
}

bool isEnglishDescriptorWord(const std::string& raw) {
  std::string l = asciiLowerToken(trimCopy(raw));
  stripTrailingDot(l);
  static const char* k[] = {"female",     "male",     "given",       "name",     "first", "last",
                            "past",       "present",  "participle",  "of",       "the",   "a",
                            "an",         "form",     "plural",      "singular", "diminutive",
                            "see",        "also",     "and",         "or",       "from",  "personified",
                            "personif"};
  for (const char* w : k) {
    if (l == w) {
      return true;
    }
  }
  return false;
}

bool looksDutchish(const std::string& raw) {
  const std::string s = asciiLowerToken(raw);
  if (s.find("ij") != std::string::npos) {
    return true;
  }
  if (s.find("heid") != std::string::npos || s.find("lijk") != std::string::npos ||
      s.find("tje") != std::string::npos) {
    return true;
  }
  if (s.find("v.d.") != std::string::npos || s.find("van jou") != std::string::npos ||
      s.find("van u") != std::string::npos || s.find("van de") != std::string::npos) {
    return true;
  }
  static const char* k[] = {"jouw",     "jullie",    "niet",      "naar",      "voor",      "zijn",
                            "worden",   "zonder",    "omdat",     "maar",      "geen",      "deze",
                            "godin",    "vergif",    "madelief",  "margriet",  "nutteloos", "gewoon",
                            "onderwijs", "inricht",  "instell",   "onderricht", "rechtvaard", "vrouw",
                            "kind",     "woord",     "uitdruk",   "iemand",    "deur",      "poort",
                            "oogje",    "kort",      "mondeling", "woordelijk", "bijvoorbeeld",
                            "vermogen", "familie",   "verwant",   "vriend",    "gerechtig"};
  for (const char* w : k) {
    if (s.find(w) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool isFrenchFunctionWord(const std::string& raw) {
  std::string l = asciiLowerToken(trimCopy(raw));
  stripTokenPunct(l);
  static const char* k[] = {"dans",     "une",      "des",     "qui",      "que",      "pour",     "cette",
                            "ces",      "les",      "aux",     "avec",     "par",      "sur",      "entre",
                            "ouverture","permettant","permet", "entrer",   "sortir",   "endroit",  "battante",
                            "passage",  "enceinte", "ville",   "acces",    "souvent",  "fortifie",
                            "defendu",  "muraille", "d'un",     "d'une",    "du",       "au",       "est",
                            "sont",     "ou",       "un",       "le",       "la",       "de",       "et"};
  for (const char* w : k) {
    if (l == w) {
      return true;
    }
  }
  return false;
}

bool hasFrenchMarker(const std::string& raw) {
  return raw.find("\xC3\xA0") != std::string::npos ||  // à
         raw.find("\xC3\xA7") != std::string::npos ||  // ç
         raw.find("\xC3\xA8") != std::string::npos ||  // è
         raw.find("\xC3\xAA") != std::string::npos ||  // ê
         raw.find("\xC3\xB9") != std::string::npos ||  // ù
         raw.find("\xC3\xB4") != std::string::npos;    // ô
}

bool looksFrenchish(const std::string& raw) {
  if (hasFrenchMarker(raw)) {
    return true;
  }
  int hits = 0;
  size_t i = 0;
  while (i < raw.size()) {
    while (i < raw.size() && raw[i] == ' ') {
      ++i;
    }
    if (i >= raw.size()) {
      break;
    }
    const size_t start = i;
    while (i < raw.size() && raw[i] != ' ') {
      ++i;
    }
    if (isFrenchFunctionWord(raw.substr(start, i - start))) {
      ++hits;
    }
  }
  return hits >= 2;
}

bool hasLatinMacron(const std::string& raw) {
  return raw.find("\xC4\x81") != std::string::npos || raw.find("\xC4\x93") != std::string::npos ||
         raw.find("\xC4\xAB") != std::string::npos || raw.find("\xC5\x8D") != std::string::npos ||
         raw.find("\xC5\xAB") != std::string::npos;
}

bool looksLatinWord(const std::string& raw) {
  std::string l = asciiLowerToken(trimCopy(raw));
  stripTokenPunct(l);
  if (l.size() < 2) {
    return false;
  }
  if (hasLatinMacron(raw)) {
    return true;
  }
  static const char* kExact[] = {"ex",   "ad",   "per",  "cum",  "pro",  "sine", "apud", "erga", "quod",
                                 "quae", "qui",  "sunt", "esse", "tamen", "causa", "gratia", "tibi",
                                 "mihi", "meis", "tuis", "suis", "uno",  "verbo", "verbis"};
  for (const char* w : kExact) {
    if (l == w) {
      return true;
    }
  }
  auto ends = [&](const char* suf) {
    const size_t n = std::strlen(suf);
    return l.size() > n + 1 && l.compare(l.size() - n, n, suf) == 0;
  };
  return ends("ibus") || ends("orum") || ends("arum") || ends("ae") || ends("um") || ends("us");
}

bool looksLatinPhrase(const std::string& raw) {
  if (countWords(raw) < 2 || looksDutchish(raw)) {
    return false;
  }
  if (hasLatinMacron(raw)) {
    return true;
  }
  const std::string s = asciiLowerToken(raw);
  static const char* k[] = {"ibus", "orum", "arum", "erga", "apud", "sine", "quod", "quae", "sunt",
                            "esse", "tamen", "publico", "manu", "accepi", "liberi", "sententia",
                            "desistere", "mandata", "tempore"};
  for (const char* w : k) {
    if (s.find(w) != std::string::npos) {
      return true;
    }
  }
  int latinWords = 0;
  size_t i = 0;
  while (i < raw.size()) {
    while (i < raw.size() && raw[i] == ' ') {
      ++i;
    }
    if (i >= raw.size()) {
      break;
    }
    const size_t start = i;
    while (i < raw.size() && raw[i] != ' ') {
      ++i;
    }
    if (looksLatinWord(raw.substr(start, i - start))) {
      ++latinWords;
    }
  }
  return latinWords >= 2;
}

size_t findLatinExampleCut(const std::string& s) {
  size_t cut = std::string::npos;
  auto consider = [&](const size_t p) {
    if (p != std::string::npos && p >= 3 && (cut == std::string::npos || p < cut)) {
      cut = p;
    }
  };
  consider(s.find(" Q "));
  consider(s.find(" Q-"));
  consider(s.find("; -"));
  static const char* kDash[] = {" -o ", " -um ", " -a ", " -is ", " -e ", " -i ", " -us "};
  for (const char* d : kDash) {
    consider(s.find(d));
  }

  int dutchRun = 0;
  size_t i = 0;
  size_t wordStart = 0;
  while (i <= s.size()) {
    if (i == s.size() || s[i] == ' ') {
      if (i > wordStart) {
        const std::string w = s.substr(wordStart, i - wordStart);
        if (looksDutchish(w)) {
          ++dutchRun;
        } else if (dutchRun >= 2 && looksLatinWord(w) && !isEnglishDescriptorWord(w)) {
          consider(wordStart > 0 ? wordStart - 1 : wordStart);
          break;
        }
      }
      wordStart = i + 1;
    }
    ++i;
  }
  return cut;
}

std::string capExample(std::string ex) {
  ex = trimCopy(ex);
  while (!ex.empty() && (ex.front() == ';' || ex.front() == 'Q' || ex.front() == 'q')) {
    if ((ex.front() == 'Q' || ex.front() == 'q') && ex.size() > 1 && ex[1] == ' ') {
      ex = trimCopy(ex.substr(2));
      continue;
    }
    ex = trimCopy(ex.substr(1));
  }
  if (ex.size() > 70) {
    size_t cut = 70;
    while (cut > 40 && ex[cut] != ' ') {
      --cut;
    }
    ex = trimCopy(ex.substr(0, cut));
  }
  while (!ex.empty() && (ex.back() == ';' || ex.back() == ',' || ex.back() == ':' || ex.back() == '-')) {
    ex.pop_back();
  }
  return trimCopy(ex);
}

void peelLatinExamples(std::string& gloss, std::vector<std::string>& examples) {
  const size_t cut = findLatinExampleCut(gloss);
  if (cut == std::string::npos) {
    return;
  }
  std::string ex = capExample(gloss.substr(cut));
  gloss = trimCopy(gloss.substr(0, cut));
  while (!gloss.empty() && (gloss.back() == ';' || gloss.back() == ',' || gloss.back() == ':')) {
    gloss.pop_back();
  }
  gloss = trimCopy(gloss);
  if (!ex.empty() && examples.size() < 2) {
    examples.insert(examples.begin(), std::move(ex));
  }
}

void stripLeadingLatinLemma(std::string& gloss, std::vector<std::string>& examples) {
  std::string rest = gloss;
  std::string latin;
  for (int taken = 0; taken < 2 && !rest.empty(); ++taken) {
    const size_t end = nextTokenEnd(rest, 0);
    if (end == 0) {
      break;
    }
    std::string tok = rest.substr(0, end);
    std::string lower = asciiLowerToken(tok);
    stripTokenPunct(lower);
    static const char* kLead[] = {"uno", "verbo", "verbis", "ex", "ad", "per", "pro", "cum"};
    bool allowed = false;
    for (const char* w : kLead) {
      if (lower == w) {
        allowed = true;
        break;
      }
    }
    if (!allowed) {
      break;
    }
    if (!latin.empty()) {
      latin += " ";
    }
    latin += tok;
    rest = trimCopy(rest.substr(end));
  }
  if (latin.empty() || rest.empty() || !looksDutchish(rest)) {
    return;
  }
  gloss = rest;
  if (examples.size() < 2) {
    examples.insert(examples.begin(), std::move(latin));
  }
}

bool isShortTranslationWord(const std::string& raw) {
  std::string w = trimCopy(raw);
  stripTokenPunct(w);
  if (w.size() < 2 || w.size() > 14) {
    return false;
  }
  if (isFrenchFunctionWord(w) || isEnglishDescriptorWord(w) || isLanguageCode(asciiLowerToken(w))) {
    return false;
  }
  if (hasFrenchMarker(w) || hasLatinMacron(w)) {
    return false;
  }
  for (unsigned char c : w) {
    if (c >= 'A' && c <= 'Z') {
      continue;
    }
    if (c >= 'a' && c <= 'z') {
      continue;
    }
    if (c >= 0x80) {
      continue;
    }
    if (c == '-' || c == '\'') {
      continue;
    }
    return false;
  }
  return true;
}

std::string commaJoinShortWords(const std::string& s) {
  if (s.find(',') != std::string::npos) {
    return s;
  }
  std::vector<std::string> words;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && s[i] == ' ') {
      ++i;
    }
    if (i >= s.size()) {
      break;
    }
    const size_t start = i;
    while (i < s.size() && s[i] != ' ') {
      ++i;
    }
    words.push_back(s.substr(start, i - start));
  }
  if (words.size() < 2 || words.size() > 6) {
    return s;
  }
  for (const std::string& w : words) {
    if (!isShortTranslationWord(w)) {
      return s;
    }
  }
  std::string out;
  for (size_t w = 0; w < words.size(); ++w) {
    if (w > 0) {
      out += ", ";
    }
    out += words[w];
  }
  return out;
}

std::string extractDutchFromBilingual(std::string s) {
  s = trimCopy(s);
  if (s.empty() || !looksFrenchish(s)) {
    return commaJoinShortWords(s);
  }
  std::vector<std::string> words;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && s[i] == ' ') {
      ++i;
    }
    if (i >= s.size()) {
      break;
    }
    const size_t start = i;
    while (i < s.size() && s[i] != ' ') {
      ++i;
    }
    words.push_back(s.substr(start, i - start));
  }
  if (words.size() < 3) {
    return "";
  }
  std::vector<std::string> tail;
  while (!words.empty() && tail.size() < 3 && isShortTranslationWord(words.back()) &&
         !looksFrenchish(words.back())) {
    tail.insert(tail.begin(), words.back());
    words.pop_back();
  }
  if (tail.empty()) {
    return "";
  }
  std::string out;
  for (size_t w = 0; w < tail.size(); ++w) {
    if (w > 0) {
      out += ", ";
    }
    out += tail[w];
  }
  return out;
}

bool glossLooksTruncated(const std::string& s) {
  if (s.empty()) {
    return true;
  }
  const unsigned char last = static_cast<unsigned char>(s.back());
  if (last == ',' || last == ';' || last == ':' || last == '-' || last == '/' || last == '&') {
    return true;
  }
  if (s.size() >= 3 && s.compare(s.size() - 3, 3, "\xE2\x80\xA6") == 0) {
    return true;
  }
  return false;
}

std::string cliticStrippedLower(const char* query) {
  if (query == nullptr || query[0] == '\0') {
    return "";
  }
  std::string w = asciiLowerToken(query);
  size_t cut = std::string::npos;
  for (size_t i = 0; i < w.size(); ++i) {
    if (w[i] == '\'') {
      cut = i + 1;
    } else if (i + 2 < w.size() && static_cast<unsigned char>(w[i]) == 0xE2 &&
               static_cast<unsigned char>(w[i + 1]) == 0x80 &&
               (static_cast<unsigned char>(w[i + 2]) == 0x98 || static_cast<unsigned char>(w[i + 2]) == 0x99)) {
      cut = i + 3;
    }
  }
  if (cut != std::string::npos && cut < w.size()) {
    w = w.substr(cut);
  }
  return w;
}

bool queryLooksFrenchAgentNoun(const char* query) {
  const std::string w = cliticStrippedLower(query);
  auto ends = [&](const char* suf) {
    const size_t n = std::strlen(suf);
    return w.size() > n + 2 && w.compare(w.size() - n, n, suf) == 0;
  };
  return ends("euses") || ends("euse") || ends("eurs") || ends("eur") || ends("trices") || ends("trice") ||
         ends("teurs") || ends("teur");
}

bool queryLooksFeminineAgent(const char* query) {
  const std::string w = cliticStrippedLower(query);
  auto ends = [&](const char* suf) {
    const size_t n = std::strlen(suf);
    return w.size() > n + 2 && w.compare(w.size() - n, n, suf) == 0;
  };
  return ends("euses") || ends("euse") || ends("trices") || ends("trice");
}

std::string agentNounFromVerbGloss(const std::string& gloss) {
  const std::string lower = asciiLowerToken(gloss);
  if (lower.compare(0, 11, "iemand die ") == 0) {
    return gloss;
  }
  if (countWords(gloss) == 1 && gloss.size() >= 4) {
    if (lower.size() >= 4 && lower.compare(lower.size() - 2, 2, "en") == 0) {
      return "iemand die " + gloss.substr(0, gloss.size() - 2) + "t";
    }
  }
  return "iemand die " + gloss;
}

bool looksCitationChunk(const std::string& raw) {
  const std::string s = trimCopy(raw);
  if (s.size() >= 3 && std::isupper(static_cast<unsigned char>(s[0])) && s[1] == ' ' &&
      (s[2] == '-' || s[2] == '.')) {
    return true;
  }
  const std::string l = asciiLowerToken(s);
  return l.compare(0, 3, "cf.") == 0 || l.compare(0, 4, "see ") == 0 || l.compare(0, 4, "zie ") == 0;
}

void stripBracketCitations(std::string& s) {
  size_t open = 0;
  while ((open = s.find('[')) != std::string::npos) {
    const size_t close = s.find(']', open);
    if (close == std::string::npos) {
      break;
    }
    s.erase(open, close - open + 1);
  }
  s = trimCopy(s);
}

void stripPersonifPrefix(std::string& s) {
  const std::string l = asciiLowerToken(s);
  if (l.compare(0, 8, "personif") != 0) {
    return;
  }
  size_t i = 0;
  while (i < s.size() && s[i] != ' ' && s[i] != '.') {
    ++i;
  }
  while (i < s.size() && (s[i] == '.' || s[i] == ' ')) {
    ++i;
  }
  s = trimCopy(s.substr(i));
}

void stripLeadingDescriptors(std::string& s) {
  while (!s.empty()) {
    const size_t end = nextTokenEnd(s, 0);
    if (end == 0 || !isEnglishDescriptorWord(s.substr(0, end))) {
      break;
    }
    s = trimCopy(s.substr(end));
  }
}

bool wordLooksProperName(const std::string& w) {
  if (w.empty() || static_cast<unsigned char>(w[0]) < 'A' || static_cast<unsigned char>(w[0]) > 'Z') {
    return false;
  }
  for (size_t i = 1; i < w.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(w[i]);
    if (c >= 'A' && c <= 'Z') {
      continue;
    }
    if (c >= 'a' && c <= 'z') {
      continue;
    }
    if (c >= 0x80) {
      continue;
    }
    return false;
  }
  return w.size() >= 2;
}

std::string commaJoinProperNames(const std::string& s) {
  std::vector<std::string> words;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && s[i] == ' ') {
      ++i;
    }
    if (i >= s.size()) {
      break;
    }
    const size_t start = i;
    while (i < s.size() && s[i] != ' ') {
      ++i;
    }
    words.push_back(s.substr(start, i - start));
  }
  if (words.size() < 2 || words.size() > 6) {
    return s;
  }
  for (const std::string& w : words) {
    if (!wordLooksProperName(w)) {
      return s;
    }
  }
  std::string out;
  for (size_t w = 0; w < words.size(); ++w) {
    if (w > 0) {
      out += ", ";
    }
    out += words[w];
  }
  return out;
}

std::string cleanGloss(std::string s) {
  s = trimCopy(s);
  stripPersonifPrefix(s);
  stripBracketCitations(s);
  std::string morph;
  consumeLeadingMorphology(s, morph);
  std::string gender;
  while (true) {
    const size_t end = nextTokenEnd(s, 0);
    if (end == 0) {
      break;
    }
    if (!genderFromToken(asciiLowerToken(s.substr(0, end)), gender) && !isMorphToken(s.substr(0, end))) {
      break;
    }
    s = trimCopy(s.substr(end));
  }
  (void)morph;
  (void)gender;
  stripLeadingDescriptors(s);
  if (!s.empty() && s.front() == '(') {
    const size_t close = s.find(')');
    if (close != std::string::npos && close < 28 && countWords(s.substr(1, close - 1)) <= 2) {
      s = trimCopy(s.substr(close + 1));
    }
  }

  const std::vector<std::string> parts = splitUnbracketed(s, ',');
  if (parts.size() <= 1) {
    if (looksCitationChunk(s)) {
      return "";
    }
    return commaJoinProperNames(trimCopy(s));
  }
  std::string kept;
  for (const std::string& part : parts) {
    if (part.empty() || looksCitationChunk(part)) {
      continue;
    }
    if (looksLatinPhrase(part) && !looksDutchish(part)) {
      continue;
    }
    if (!kept.empty()) {
      kept += ", ";
    }
    kept += part;
  }
  return trimCopy(kept);
}

void takeExampleMarkers(std::string& gloss, std::vector<std::string>& examples) {
  static const char* kMarks[] = {"\xE2\x96\xB6", "\xE2\x96\xB8", "\xE2\x96\xBA"};
  for (const char* mark : kMarks) {
    const size_t p = gloss.find(mark);
    if (p == std::string::npos) {
      continue;
    }
    std::string ex = trimCopy(gloss.substr(p + std::strlen(mark)));
    gloss = trimCopy(gloss.substr(0, p));
    if (!ex.empty() && examples.size() < 2) {
      examples.push_back(std::move(ex));
    }
    return;
  }
}

DictionarySense makeSense(std::string text) {
  DictionarySense sense;
  text = trimCopy(text);
  consumeArabicSenseNumber(text);
  consumeRomanSenseNumber(text);
  size_t q = text.find('"');
  if (q != std::string::npos) {
    const size_t q2 = text.find('"', q + 1);
    if (q2 != std::string::npos && q2 > q + 2 && q2 - q < 80) {
      sense.examples.push_back(text.substr(q + 1, q2 - q - 1));
      text.erase(q, q2 - q + 1);
      text = trimCopy(text);
    }
  }
  takeExampleMarkers(text, sense.examples);
  sense.gloss = cleanGloss(std::move(text));
  peelLatinExamples(sense.gloss, sense.examples);
  stripLeadingLatinLemma(sense.gloss, sense.examples);
  return sense;
}

void pushSense(DictionarySection& section, std::string text) {
  if (text.empty() || section.senses.size() >= 12) {
    return;
  }
  DictionarySense sense = makeSense(std::move(text));
  if (sense.gloss.empty() || letterCount(sense.gloss) < 2) {
    if (!sense.examples.empty() && !section.senses.empty()) {
      for (std::string& ex : sense.examples) {
        if (section.senses.back().examples.size() < 2) {
          section.senses.back().examples.push_back(std::move(ex));
        }
      }
    }
    return;
  }
  section.senses.push_back(std::move(sense));
}

void pushEquivalents(DictionarySection& section, const std::string& text) {
  for (std::string& part : splitEquivalents(text)) {
    pushSense(section, std::move(part));
  }
}

void ingestGloss(DictionarySection& section, std::string text) {
  text = trimCopy(text);
  if (text.size() > 64 || text.find(';') != std::string::npos || text.find("(a)") != std::string::npos ||
      text.find("(b)") != std::string::npos) {
    consumeLeadingMorphology(text, section.grammar);
    PosInfo lead;
    consumeLeadingPos(text, lead);
    mergePos(section.pos, lead);
  }
  pushEquivalents(section, text);
}

void polishSection(DictionarySection& section, const bool dutchTarget) {
  std::vector<DictionarySense> merged;
  std::string desc;
  auto dropDesc = [&]() { desc.clear(); };
  for (DictionarySense& sense : section.senses) {
    if (dutchTarget) {
      PosInfo extra;
      consumeLeadingPos(sense.gloss, extra);
      mergePos(section.pos, extra);
      sense.gloss = extractDutchFromBilingual(sense.gloss);
    }
    const bool oneWord = countWords(sense.gloss) == 1 && sense.examples.empty();
    if (oneWord && isEnglishDescriptorWord(sense.gloss)) {
      if (!desc.empty()) {
        desc += " ";
      }
      desc += sense.gloss;
      continue;
    }
    dropDesc();
    if (sense.gloss.empty() || letterCount(sense.gloss) < 2) {
      if (!sense.examples.empty() && !merged.empty()) {
        for (std::string& ex : sense.examples) {
          if (merged.back().examples.size() < 2) {
            merged.back().examples.push_back(std::move(ex));
          }
        }
      }
      continue;
    }
    if (dutchTarget && looksFrenchish(sense.gloss) && !looksDutchish(sense.gloss)) {
      continue;
    }
    if (looksLatinPhrase(sense.gloss) && !looksDutchish(sense.gloss)) {
      if (!merged.empty() && merged.back().examples.size() < 2) {
        merged.back().examples.push_back(capExample(std::move(sense.gloss)));
      }
      continue;
    }
    if (glossLooksTruncated(sense.gloss) && !merged.empty()) {
      continue;
    }
    merged.push_back(std::move(sense));
  }
  if (merged.empty() && !desc.empty()) {
    DictionarySense sense;
    sense.gloss = desc;
    merged.push_back(std::move(sense));
  }

  bool allOneWord = !merged.empty();
  for (const DictionarySense& sense : merged) {
    if (countWords(sense.gloss) > 1 || !sense.examples.empty()) {
      allOneWord = false;
      break;
    }
  }
  if (allOneWord && merged.size() >= 2 && merged.size() <= 8) {
    DictionarySense joined;
    for (size_t i = 0; i < merged.size(); ++i) {
      if (i > 0) {
        joined.gloss += ", ";
      }
      joined.gloss += merged[i].gloss;
    }
    merged.clear();
    merged.push_back(std::move(joined));
  }

  if (!merged.empty() && glossLooksTruncated(merged.back().gloss) && merged.size() > 1) {
    merged.pop_back();
  }

  if (merged.size() > 5) {
    merged.resize(5);
  }
  for (DictionarySense& sense : merged) {
    if (sense.examples.size() > 2) {
      sense.examples.resize(2);
    }
  }
  section.senses = std::move(merged);
}

void applyAgentNounOverride(DictionaryCard& card, const char* queryWord) {
  if (!queryLooksFrenchAgentNoun(queryWord)) {
    return;
  }
  for (DictionarySection& section : card.sections) {
    if (section.pos.label != "verb") {
      continue;
    }
    section.pos.label = "noun";
    section.pos.gender = queryLooksFeminineAgent(queryWord) ? "f" : "m";
    if (!section.senses.empty()) {
      section.senses[0].gloss = agentNounFromVerbGloss(section.senses[0].gloss);
      section.senses.resize(1);
    }
  }
}

void polishCard(DictionaryCard& card, const bool dutchTarget, const char* queryWord) {
  for (DictionarySection& section : card.sections) {
    polishSection(section, dutchTarget);
  }
  applyAgentNounOverride(card, queryWord);
  card.sections.erase(std::remove_if(card.sections.begin(), card.sections.end(),
                                     [](const DictionarySection& s) { return s.senses.empty(); }),
                      card.sections.end());
}

void finalizeStructuredCard(DictionaryCard& card, const char* queryWord) {
  for (DictionarySection& section : card.sections) {
    if (section.senses.size() > 5) {
      section.senses.resize(5);
    }
    if (!section.senses.empty() && glossLooksTruncated(section.senses.back().gloss) && section.senses.size() > 1) {
      section.senses.pop_back();
    }
    for (DictionarySense& sense : section.senses) {
      if (sense.examples.size() > 2) {
        sense.examples.resize(2);
      }
    }
  }
  applyAgentNounOverride(card, queryWord);
  card.sections.erase(std::remove_if(card.sections.begin(), card.sections.end(),
                                     [](const DictionarySection& s) { return s.senses.empty(); }),
                      card.sections.end());
}

size_t findClassPos(const std::string& html, const char* cls, const size_t from) {
  const std::string dq = std::string("class=\"") + cls + "\"";
  const std::string sq = std::string("class='") + cls + "'";
  const std::string dqSp = std::string("class=\"") + cls + " ";
  const std::string sqSp = std::string("class='") + cls + " ";
  size_t best = std::string::npos;
  auto consider = [&](const std::string& pat) {
    const size_t p = html.find(pat, from);
    if (p != std::string::npos && (best == std::string::npos || p < best)) {
      best = p;
    }
  };
  consider(dq);
  consider(sq);
  consider(dqSp);
  consider(sqSp);
  return best;
}

size_t findEarliestPosClass(const std::string& html, const size_t from, const char*& which) {
  static const char* kPosClass[] = {"grammar", "pos", "ctx", "psg", "grammatik", "wordclass"};
  size_t best = std::string::npos;
  which = nullptr;
  for (const char* cls : kPosClass) {
    const size_t p = findClassPos(html, cls, from);
    if (p != std::string::npos && (best == std::string::npos || p < best)) {
      best = p;
      which = cls;
    }
  }
  return best;
}

size_t tagOpenBefore(const std::string& html, const size_t inner) {
  size_t i = inner;
  while (i > 0 && html[i] != '<') {
    --i;
  }
  return (i < html.size() && html[i] == '<') ? i : std::string::npos;
}

std::string matchingCloseTag(const std::string& tagName) {
  return "</" + tagName + ">";
}

std::string collapseWs(std::string s) {
  std::string out;
  out.reserve(s.size());
  bool spaced = true;
  for (unsigned char c : s) {
    if (c <= ' ') {
      if (!spaced && !out.empty()) {
        out.push_back(' ');
        spaced = true;
      }
      continue;
    }
    out.push_back(static_cast<char>(c));
    spaced = false;
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

std::string stripHtmlTags(const std::string& html) {
  std::string out;
  out.reserve(html.size());
  bool inTag = false;
  for (unsigned char c : html) {
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
    if (!inTag) {
      out.push_back(static_cast<char>(c));
    }
  }
  return collapseWs(decodeHtmlEntities(out));
}

bool readHtmlTag(const std::string& s, const size_t i, size_t& closeOut, bool& closing, std::string& name) {
  if (i >= s.size() || s[i] != '<') {
    return false;
  }
  closeOut = s.find('>', i);
  if (closeOut == std::string::npos) {
    return false;
  }
  size_t p = i + 1;
  closing = p < closeOut && s[p] == '/';
  if (closing) {
    ++p;
  }
  const size_t start = p;
  while (p < closeOut && s[p] != ' ' && s[p] != '\t' && s[p] != '/') {
    ++p;
  }
  name = asciiLowerCopy(s.substr(start, p - start));
  return !name.empty() && name[0] != '!';
}

std::vector<std::string> leafDivTexts(const std::string& html) {
  std::vector<std::string> out;
  size_t p = 0;
  while (p < html.size()) {
    const size_t open = html.find("<div", p);
    if (open == std::string::npos) {
      break;
    }
    const unsigned char after = open + 4 < html.size() ? static_cast<unsigned char>(html[open + 4]) : 0;
    if (after != '>' && after != ' ' && after != '\t') {
      p = open + 4;
      continue;
    }
    const size_t gt = html.find('>', open);
    if (gt == std::string::npos) {
      break;
    }
    const size_t end = html.find("</div>", gt);
    if (end == std::string::npos) {
      break;
    }
    const std::string inner = html.substr(gt + 1, end - (gt + 1));
    if (inner.find('<') == std::string::npos) {
      const std::string text = trimCopy(decodeHtmlEntities(collapseWs(inner)));
      if (!text.empty()) {
        bool dup = false;
        for (const std::string& prev : out) {
          if (prev == text) {
            dup = true;
            break;
          }
        }
        if (!dup) {
          out.push_back(text);
        }
      }
    }
    p = end + 6;
  }
  return out;
}

std::string joinComma(const std::vector<std::string>& parts) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      out += ", ";
    }
    out += parts[i];
  }
  return out;
}

std::vector<std::string> topLevelOlLiChunks(const std::string& body) {
  std::vector<std::string> chunks;
  size_t i = 0;
  while (i < body.size()) {
    if (body[i] != '<') {
      ++i;
      continue;
    }
    bool closing = false;
    std::string name;
    size_t close = 0;
    if (!readHtmlTag(body, i, close, closing, name)) {
      ++i;
      continue;
    }
    if (name == "ol" && !closing) {
      int olDepth = 1;
      size_t startLi = std::string::npos;
      i = close + 1;
      while (i < body.size() && olDepth > 0) {
        if (body[i] != '<') {
          ++i;
          continue;
        }
        bool tagClose = false;
        std::string tag;
        size_t tagEnd = 0;
        if (!readHtmlTag(body, i, tagEnd, tagClose, tag)) {
          ++i;
          continue;
        }
        if (tag == "ol") {
          olDepth += tagClose ? -1 : 1;
        } else if (tag == "li") {
          if (!tagClose && olDepth == 1) {
            startLi = tagEnd + 1;
          } else if (tagClose && olDepth == 1 && startLi != std::string::npos) {
            chunks.push_back(body.substr(startLi, i - startLi));
            startLi = std::string::npos;
          }
        }
        i = tagEnd + 1;
      }
      return chunks;
    }
    i = close + 1;
  }
  return chunks;
}

PosInfo posFromGrammarLabel(std::string raw) {
  PosInfo pos;
  for (char& c : raw) {
    if (c == ',') {
      c = ' ';
    }
  }
  consumeLeadingPos(raw, pos);
  return pos;
}

bool parsePosTaggedLeafGlosses(const std::string& html, DictionaryCard& card) {
  size_t searchFrom = 0;
  bool any = false;
  while (searchFrom < html.size()) {
    const char* cls = nullptr;
    const size_t g = findEarliestPosClass(html, searchFrom, cls);
    if (g == std::string::npos) {
      break;
    }
    const size_t open = tagOpenBefore(html, g);
    if (open == std::string::npos) {
      searchFrom = g + 1;
      continue;
    }
    bool closing = false;
    std::string tagName;
    size_t gt = 0;
    if (!readHtmlTag(html, open, gt, closing, tagName) || closing) {
      searchFrom = g + 1;
      continue;
    }
    const std::string closePat = matchingCloseTag(tagName);
    const size_t fontEnd = html.find(closePat, gt);
    if (fontEnd == std::string::npos) {
      searchFrom = gt + 1;
      continue;
    }
    const std::string label = stripHtmlTags(html.substr(gt + 1, fontEnd - (gt + 1)));
    const size_t bodyStart = fontEnd + closePat.size();
    const char* nextCls = nullptr;
    const size_t nextG = findEarliestPosClass(html, bodyStart, nextCls);
    const size_t bodyEnd = nextG == std::string::npos ? html.size() : nextG;
    const std::string body = html.substr(bodyStart, bodyEnd - bodyStart);

    DictionarySection section;
    section.pos = posFromGrammarLabel(label);
    const std::vector<std::string> lis = topLevelOlLiChunks(body);
    if (!lis.empty()) {
      for (const std::string& li : lis) {
        const std::vector<std::string> divs = leafDivTexts(li);
        if (divs.empty()) {
          continue;
        }
        DictionarySense sense;
        sense.gloss = joinComma(divs);
        section.senses.push_back(std::move(sense));
      }
    }
    if (section.senses.empty()) {
      const std::vector<std::string> divs = leafDivTexts(body);
      if (!divs.empty()) {
        DictionarySense sense;
        sense.gloss = joinComma(divs);
        section.senses.push_back(std::move(sense));
      }
    }
    if (!section.senses.empty()) {
      card.sections.push_back(std::move(section));
      any = true;
    }
    searchFrom = bodyStart;
  }
  return any;
}

void foldLatinMacrons(std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char a = static_cast<unsigned char>(s[i]);
    const unsigned char b = i + 1 < s.size() ? static_cast<unsigned char>(s[i + 1]) : 0;
    if (a == 0xC4 && (b == 0x80 || b == 0x81)) {
      out.push_back(b == 0x80 ? 'A' : 'a');
      ++i;
      continue;
    }
    if (a == 0xC4 && (b == 0x92 || b == 0x93)) {
      out.push_back(b == 0x92 ? 'E' : 'e');
      ++i;
      continue;
    }
    if (a == 0xC4 && (b == 0xAA || b == 0xAB)) {
      out.push_back(b == 0xAA ? 'I' : 'i');
      ++i;
      continue;
    }
    if (a == 0xC5 && (b == 0x8C || b == 0x8D)) {
      out.push_back(b == 0x8C ? 'O' : 'o');
      ++i;
      continue;
    }
    if (a == 0xC5 && (b == 0xAA || b == 0xAB)) {
      out.push_back(b == 0xAA ? 'U' : 'u');
      ++i;
      continue;
    }
    out.push_back(s[i]);
  }
  s = std::move(out);
}

bool isLatinPrincipalPart(std::string tok) {
  foldLatinMacrons(tok);
  tok = asciiLowerToken(tok);
  stripTokenPunct(tok);
  if (tok == "esse" || tok == "fui" || tok == "futurus") {
    return true;
  }
  auto ends = [&](const char* suf) {
    const size_t n = std::strlen(suf);
    return tok.size() > n && tok.compare(tok.size() - n, n, suf) == 0;
  };
  return ends("are") || ends("ere") || ends("ire");
}

bool isMorphTokenFolded(std::string tok) {
  foldLatinMacrons(tok);
  return isMorphToken(tok);
}

std::string takeQuotedExample(const std::string& s) {
  auto takeAfter = [&](const size_t p, const size_t skip) -> std::string {
    std::string ex = s.substr(p + skip);
    const size_t semi = ex.find(';');
    if (semi != std::string::npos) {
      ex = ex.substr(0, semi);
    }
    return capExample(std::move(ex));
  };
  size_t q = s.find(" Q ");
  if (q != std::string::npos) {
    return takeAfter(q, 3);
  }
  q = s.find(" Ex. ");
  if (q != std::string::npos) {
    return takeAfter(q, 5);
  }
  q = s.find(" e.g. ");
  if (q != std::string::npos) {
    return takeAfter(q, 6);
  }
  const size_t guillemet = s.find("\xC2\xAB");
  if (guillemet != std::string::npos) {
    const size_t end = s.find("\xC2\xBB", guillemet + 2);
    if (end != std::string::npos && end - guillemet > 4 && end - guillemet < 90) {
      return capExample(s.substr(guillemet + 2, end - (guillemet + 2)));
    }
  }
  return "";
}

std::string compactNumberedGloss(std::string s) {
  stripBracketCitations(s);
  size_t q = s.find(" Q ");
  if (q == std::string::npos) {
    q = s.find(" Ex. ");
  }
  if (q == std::string::npos) {
    q = s.find(" e.g. ");
  }
  if (q == std::string::npos) {
    q = s.find("\xC2\xAB");
  }
  if (q != std::string::npos) {
    s = s.substr(0, q);
  }
  const size_t dash = s.find("; -");
  if (dash != std::string::npos && dash > 6) {
    s = s.substr(0, dash);
  }
  const size_t letter = s.find(" (a)");
  if (letter != std::string::npos && letter > 8) {
    s = s.substr(0, letter);
  }
  const size_t firstSemi = s.find(';');
  if (firstSemi != std::string::npos) {
    const std::string head = trimCopy(s.substr(0, firstSemi));
    if (letterCount(head) >= 3) {
      s = head;
    }
  }
  s = trimCopy(s);
  while (!s.empty() && s.front() == '(') {
    const size_t close = s.find(')');
    if (close == std::string::npos || close > 28) {
      break;
    }
    s = trimCopy(s.substr(close + 1));
  }
  if (s.size() >= 3 && s[0] == '(' && s[2] == ')' && s[1] >= 'a' && s[1] <= 'f') {
    s = trimCopy(s.substr(3));
  }
  return trimCopy(s);
}

void parsePlaintextLemmaHeader(std::string header, PosInfo& pos, std::string& grammar, std::string& headerGloss) {
  const size_t numbered = header.find("1. ");
  if (numbered != std::string::npos && numbered > 0) {
    header = trimCopy(header.substr(0, numbered));
  }
  size_t i = 0;
  while (i < header.size() && header[i] != ' ' && header[i] != ',') {
    ++i;
  }
  std::string rest;
  if (i < header.size() && header[i] == ',') {
    rest = trimCopy(header.substr(i + 1));
  } else {
    rest = trimCopy(header.substr(i));
  }

  while (!rest.empty()) {
    if (rest.front() == '(') {
      const size_t close = rest.find(')');
      if (close == std::string::npos || close > 32) {
        break;
      }
      const std::string paren = rest.substr(0, close + 1);
      if (grammar.empty()) {
        grammar = paren;
      } else {
        grammar += " ";
        grammar += paren;
      }
      rest = trimCopy(rest.substr(close + 1));
      continue;
    }
    const size_t end = nextTokenEnd(rest, 0);
    const std::string tok = rest.substr(0, end);
    PosInfo hit;
    std::string gender;
    if (posFromToken(tok, hit)) {
      mergePos(pos, hit);
      rest = trimCopy(rest.substr(end));
      continue;
    }
    if (genderFromToken(asciiLowerToken(tok), gender)) {
      if (pos.gender.empty()) {
        pos.gender = gender;
      }
      rest = trimCopy(rest.substr(end));
      continue;
    }
    if (isMorphTokenFolded(tok) || isLatinPrincipalPart(tok)) {
      if (isLatinPrincipalPart(tok) && pos.label.empty()) {
        pos.label = "verb";
      }
      if (!grammar.empty()) {
        grammar += " ";
      }
      grammar += tok;
      rest = trimCopy(rest.substr(end));
      continue;
    }
    break;
  }
  headerGloss = rest;
  if (pos.label.empty() && !pos.gender.empty()) {
    pos.label = "noun";
  }
}

void splitNumberedPlainSenses(const std::string& text, std::vector<std::string>& senses) {
  size_t i = 0;
  size_t start = std::string::npos;
  auto push = [&](const size_t from, const size_t to) {
    const std::string chunk = trimCopy(text.substr(from, to - from));
    if (!chunk.empty()) {
      senses.push_back(chunk);
    }
  };
  while (i < text.size()) {
    const bool atLine = (i == 0 || text[i - 1] == '\n');
    if (atLine && text[i] >= '1' && text[i] <= '9') {
      size_t j = i;
      while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
        ++j;
      }
      if (j < text.size() && text[j] == '.' && j + 1 < text.size() && (text[j + 1] == ' ' || text[j + 1] == '\n')) {
        if (start != std::string::npos) {
          push(start, i);
        }
        start = j + 2;
        i = start;
        continue;
      }
    }
    ++i;
  }
  if (start != std::string::npos) {
    push(start, text.size());
  }
}

bool parseNumberedPlaintext(const std::string& text, DictionaryCard& card) {
  std::string src = text;
  for (char& c : src) {
    if (c == '\r') {
      c = '\n';
    }
  }
  size_t lineEnd = src.find('\n');
  std::string header = trimCopy(lineEnd == std::string::npos ? src : src.substr(0, lineEnd));
  std::string rest = lineEnd == std::string::npos ? "" : src.substr(lineEnd + 1);

  DictionarySection section;
  std::string headerGloss;
  parsePlaintextLemmaHeader(header, section.pos, section.grammar, headerGloss);

  std::vector<std::string> rawSenses;
  splitNumberedPlainSenses(rest, rawSenses);
  if (rawSenses.empty()) {
    std::string one = trimCopy(headerGloss);
    if (one.empty()) {
      one = trimCopy(rest);
    } else if (!trimCopy(rest).empty()) {
      one += "; ";
      one += trimCopy(rest);
    }
    if (!one.empty()) {
      rawSenses.push_back(std::move(one));
    }
  }

  for (std::string& raw : rawSenses) {
    if (section.senses.size() >= 4) {
      break;
    }
    DictionarySense sense;
    sense.gloss = compactNumberedGloss(raw);
    stripPersonifPrefix(sense.gloss);
    const std::string ex = takeQuotedExample(raw);
    if (!ex.empty()) {
      sense.examples.push_back(ex);
    }
    if (sense.gloss.empty() || letterCount(sense.gloss) < 2) {
      continue;
    }
    section.senses.push_back(std::move(sense));
  }
  if (section.senses.empty()) {
    return false;
  }
  card.sections.push_back(std::move(section));
  return true;
}

bool skipMetaHeading(const std::string& lower) {
  static const char* kSkip[] = {
      "uitdrukking", "spreekwoord", "woordherkomst", "synoniem", "antoniem", "schrijfwijze",
      "vervoeging",  "vertaling",   "uitspraak",     "verbuiging", "hyperoniem", "hyponiem",
      "etymolog",    "pronunciation", "derived",     "related",    "anagram",    "hyphen",
      "origin",      "translation", "conjugation",   "declension", "inflection", "proverb",
      "idiom",       "phrase",      "quotation",     "see also",   "reference",  "usage note",
      "herkunft",    "aussprache",  "synonyme",      "antonyme",   "konjugation",
      "conjugaison", "locution",    "proverbe",      "traduction", "prononciation"};
  for (const char* s : kSkip) {
    if (lower.find(s) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string firstItalicInUl(const std::string& li) {
  const size_t ul = li.find("<ul");
  if (ul == std::string::npos) {
    return "";
  }
  const size_t iOpen = li.find("<i>", ul);
  if (iOpen == std::string::npos) {
    return "";
  }
  const size_t iClose = li.find("</i>", iOpen);
  if (iClose == std::string::npos) {
    return "";
  }
  return capExample(stripHtmlTags(li.substr(iOpen + 3, iClose - (iOpen + 3))));
}

bool parseHeadingPosSections(const std::string& html, DictionaryCard& card) {
  size_t p = 0;
  bool any = false;
  while (p < html.size()) {
    size_t h = html.find("<h4>", p);
    const size_t h3 = html.find("<h3>", p);
    if (h3 != std::string::npos && (h == std::string::npos || h3 < h)) {
      h = h3;
    }
    if (h == std::string::npos) {
      break;
    }
    const bool isH3 = html.compare(h, 4, "<h3>") == 0;
    const char* close = isH3 ? "</h3>" : "</h4>";
    const size_t hend = html.find(close, h);
    if (hend == std::string::npos) {
      break;
    }
    const std::string heading = stripHtmlTags(html.substr(h, hend - h));
    const std::string headingLower = asciiLowerToken(heading);
    p = hend + std::strlen(close);
    if (heading.empty() || skipMetaHeading(headingLower)) {
      continue;
    }
    PosInfo pos;
    std::string headingRest = heading;
    consumeLeadingPos(headingRest, pos);
    if (pos.label.empty()) {
      continue;
    }
    size_t nextH = html.find("<h4>", p);
    const size_t nextH3 = html.find("<h3>", p);
    if (nextH3 != std::string::npos && (nextH == std::string::npos || nextH3 < nextH)) {
      nextH = nextH3;
    }
    const std::string body = html.substr(p, (nextH == std::string::npos ? html.size() : nextH) - p);

    DictionarySection section;
    section.pos = pos;
    const size_t ol = body.find("<ol");
    const std::string headBit = ol == std::string::npos ? body : body.substr(0, ol);
    const bool sawF = headBit.find(">v</span>") != std::string::npos || headBit.find(">f</span>") != std::string::npos;
    const bool sawM = headBit.find(">m</span>") != std::string::npos;
    const bool sawN = headBit.find(">o</span>") != std::string::npos || headBit.find(">n</span>") != std::string::npos;
    if (sawF && sawM) {
      section.pos.gender = "f/m";
    } else if (sawF) {
      section.pos.gender = "f";
    } else if (sawM) {
      section.pos.gender = "m";
    } else if (sawN) {
      section.pos.gender = "n";
    }

    const std::vector<std::string> lis = topLevelOlLiChunks(body);
    for (const std::string& li : lis) {
      if (section.senses.size() >= 4) {
        break;
      }
      size_t cut = li.find("<ul");
      if (cut == std::string::npos) {
        cut = li.size();
      }
      DictionarySense sense;
      sense.gloss = trimCopy(stripHtmlTags(li.substr(0, cut)));
      const std::string ex = firstItalicInUl(li);
      if (!ex.empty()) {
        sense.examples.push_back(ex);
      }
      if (sense.gloss.empty() || letterCount(sense.gloss) < 3) {
        continue;
      }
      section.senses.push_back(std::move(sense));
    }
    if (!section.senses.empty()) {
      card.sections.push_back(std::move(section));
      any = true;
    }
  }
  return any;
}

std::string stripDateSpans(std::string s) {
  size_t p = 0;
  while ((p = s.find("<span class=\"d\">", p)) != std::string::npos) {
    const size_t end = s.find("</span>", p);
    if (end == std::string::npos) {
      break;
    }
    s.erase(p, end + 7 - p);
  }
  return s;
}

bool parseNumberedHtmlSenses(const std::string& html, DictionaryCard& card) {
  DictionarySection section;
  const char* posCls = nullptr;
  const size_t ctx = findEarliestPosClass(html, 0, posCls);
  if (ctx != std::string::npos) {
    const size_t gt = html.find('>', ctx);
    const size_t open = tagOpenBefore(html, ctx);
    bool closing = false;
    std::string tagName;
    size_t tagClose = 0;
    if (open != std::string::npos && readHtmlTag(html, open, tagClose, closing, tagName)) {
      const std::string closePat = matchingCloseTag(tagName);
      const size_t end = html.find(closePat, gt);
      if (gt != std::string::npos && end != std::string::npos) {
        std::string label = stripHtmlTags(html.substr(gt + 1, end - (gt + 1)));
        consumeLeadingPos(label, section.pos);
      }
    }
  }

  static const char* kNumClass[] = {"num", "sense", "sn"};
  size_t p = 0;
  while (p < html.size() && section.senses.size() < 4) {
    size_t num = std::string::npos;
    for (const char* cls : kNumClass) {
      const size_t hit = findClassPos(html, cls, p);
      if (hit != std::string::npos && (num == std::string::npos || hit < num)) {
        num = hit;
      }
    }
    if (num == std::string::npos) {
      break;
    }
    const size_t gt = html.find('>', num);
    if (gt == std::string::npos) {
      break;
    }
    const size_t endB = html.find("</b>", gt);
    const size_t endSpan = html.find("</span>", gt);
    size_t endMark = std::string::npos;
    if (endB != std::string::npos && (endSpan == std::string::npos || endB < endSpan)) {
      endMark = endB;
    } else {
      endMark = endSpan;
    }
    if (endMark == std::string::npos) {
      break;
    }
    const std::string n = trimCopy(html.substr(gt + 1, endMark - (gt + 1)));
    p = endMark + 1;
    if (n.size() != 1 || n[0] < '1' || n[0] > '9') {
      continue;
    }
    size_t stop = std::string::npos;
    for (const char* cls : kNumClass) {
      const size_t hit = findClassPos(html, cls, p);
      if (hit != std::string::npos && (stop == std::string::npos || hit < stop)) {
        stop = hit;
      }
    }
    auto consider = [&](const char* cls) {
      const size_t loc = findClassPos(html, cls, p);
      if (loc != std::string::npos && (stop == std::string::npos || loc < stop)) {
        stop = loc;
      }
    };
    consider("ib");
    consider("phg");
    consider("et");
    const std::string slice = html.substr(p, (stop == std::string::npos ? html.size() : stop) - p);
    DictionarySense sense;
    sense.gloss = trimCopy(stripHtmlTags(stripDateSpans(slice)));
    if (sense.gloss.size() > 160) {
      size_t cut = 160;
      while (cut > 80 && sense.gloss[cut] != ' ') {
        --cut;
      }
      sense.gloss = trimCopy(sense.gloss.substr(0, cut));
    }
    if (sense.gloss.empty() || letterCount(sense.gloss) < 8) {
      continue;
    }
    if (static_cast<unsigned char>(sense.gloss[0]) == 0xE2 || sense.gloss.find("obs.") == 0) {
      continue;
    }
    section.senses.push_back(std::move(sense));
  }
  if (section.senses.empty()) {
    return false;
  }
  card.sections.push_back(std::move(section));
  return true;
}

bool parseXdxfTags(const std::string& html, DictionaryCard& card) {
  if (html.find("<dtrn") == std::string::npos && html.find("<tr>") == std::string::npos &&
      html.find("<pos>") == std::string::npos) {
    return false;
  }
  DictionarySection section;
  size_t posOpen = html.find("<pos>");
  if (posOpen != std::string::npos) {
    const size_t posClose = html.find("</pos>", posOpen);
    if (posClose != std::string::npos) {
      std::string label = stripHtmlTags(html.substr(posOpen + 5, posClose - (posOpen + 5)));
      consumeLeadingPos(label, section.pos);
    }
  }
  auto collectTag = [&](const char* open, const char* close) {
    size_t p = 0;
    while (section.senses.size() < 5) {
      const size_t a = html.find(open, p);
      if (a == std::string::npos) {
        break;
      }
      const size_t gt = html.find('>', a);
      const size_t b = html.find(close, gt);
      if (gt == std::string::npos || b == std::string::npos) {
        break;
      }
      DictionarySense sense;
      sense.gloss = trimCopy(stripHtmlTags(html.substr(gt + 1, b - (gt + 1))));
      p = b + std::strlen(close);
      if (sense.gloss.size() < 2) {
        continue;
      }
      section.senses.push_back(std::move(sense));
    }
  };
  collectTag("<dtrn", "</dtrn>");
  if (section.senses.empty()) {
    collectTag("<tr>", "</tr>");
  }
  size_t ex = html.find("<ex>");
  if (ex != std::string::npos && !section.senses.empty()) {
    const size_t exEnd = html.find("</ex>", ex);
    if (exEnd != std::string::npos) {
      const std::string example = capExample(stripHtmlTags(html.substr(ex + 4, exEnd - (ex + 4))));
      if (!example.empty()) {
        section.senses[0].examples.push_back(example);
      }
    }
  }
  if (section.senses.empty()) {
    return false;
  }
  card.sections.push_back(std::move(section));
  return true;
}

bool parseOrderedListSenses(const std::string& html, DictionaryCard& card) {
  const std::vector<std::string> lis = topLevelOlLiChunks(html);
  if (lis.empty()) {
    return false;
  }
  DictionarySection section;
  for (const std::string& li : lis) {
    if (section.senses.size() >= 5) {
      break;
    }
    size_t cut = li.find("<ul");
    if (cut == std::string::npos) {
      cut = li.size();
    }
    DictionarySense sense;
    sense.gloss = trimCopy(stripHtmlTags(li.substr(0, cut)));
    if (!sense.gloss.empty() && sense.gloss[0] == '/') {
      continue;
    }
    const std::vector<std::string> divs = leafDivTexts(li);
    if (!divs.empty()) {
      sense.gloss = joinComma(divs);
    }
    const std::string ex = firstItalicInUl(li);
    if (!ex.empty()) {
      sense.examples.push_back(ex);
    }
    if (sense.gloss.empty() || letterCount(sense.gloss) < 2) {
      continue;
    }
    section.senses.push_back(std::move(sense));
  }
  if (section.senses.empty()) {
    return false;
  }
  card.sections.push_back(std::move(section));
  return true;
}

bool parseKnownDictionaryHtml(const std::string& html, DictionaryCard& card) {
  card.sections.clear();
  auto accepted = [&]() {
    return !card.sections.empty();
  };
  if (html.find('<') != std::string::npos) {
    if (parseXdxfTags(html, card) && accepted()) {
      return true;
    }
    card.sections.clear();
    if (parsePosTaggedLeafGlosses(html, card) && accepted()) {
      return true;
    }
    card.sections.clear();
    if (parseHeadingPosSections(html, card) && accepted()) {
      return true;
    }
    card.sections.clear();
    if (parseNumberedHtmlSenses(html, card) && accepted()) {
      return true;
    }
    card.sections.clear();
    if (parseOrderedListSenses(html, card) && accepted()) {
      return true;
    }
    card.sections.clear();
    return false;
  }
  return parseNumberedPlaintext(html, card);
}

std::vector<std::string> splitNewlines(const std::string& text) {
  std::vector<std::string> lines;
  size_t start = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      const std::string line = trimCopy(text.substr(start, i - start));
      if (!line.empty()) {
        lines.push_back(line);
      }
      start = i + 1;
    }
  }
  return lines;
}

void blockStyleParts(const DefinitionBlock& block, std::string& regular, std::string& italic) {
  auto append = [](std::string& dest, const std::string& piece) {
    if (piece.empty()) {
      return;
    }
    if (!dest.empty() && dest.back() != ' ' && piece.front() != ' ') {
      dest += ' ';
    }
    dest += piece;
  };
  for (const DefinitionTextRun& run : block.runs) {
    if (run.style == EpdFontFamily::ITALIC || run.style == EpdFontFamily::BOLD_ITALIC) {
      append(italic, run.text);
    } else {
      append(regular, run.text);
    }
  }
  regular = trimCopy(regular);
  italic = trimCopy(italic);
}

DictionaryCard cardFromBlocks(const std::vector<DefinitionBlock>& blocks, const bool dutchTarget,
                              const char* queryWord) {
  DictionaryCard card;
  DictionarySection section;
  std::string pending;

  auto flushPending = [&]() {
    if (!pending.empty()) {
      ingestGloss(section, pending);
      pending.clear();
    }
  };
  auto flushSection = [&]() {
    flushPending();
    if (!section.senses.empty()) {
      card.sections.push_back(std::move(section));
    }
    section = DictionarySection{};
  };

  for (const DefinitionBlock& block : blocks) {
    std::string regular;
    std::string italic;
    blockStyleParts(block, regular, italic);
    std::string raw = regular.empty() ? italic : regular;
    std::string inlineExample;
    if (!regular.empty() && !italic.empty()) {
      PosInfo ipos;
      std::string italicRest = italic;
      if (consumeLeadingPos(italicRest, ipos) && italicRest.empty()) {
        mergePos(section.pos, ipos);
      } else if (countWords(italic) >= 2 && italic.size() < 80) {
        inlineExample = italic;
        raw = regular;
      }
    }
    if (raw.empty()) {
      continue;
    }
    const std::vector<std::string> lines = splitNewlines(raw);
    for (size_t li = 0; li < lines.size(); ++li) {
      std::string text = lines[li];
      std::string grammar;
      if (isLatinGrammarLine(text, grammar)) {
        flushPending();
        if (section.grammar.empty()) {
          section.grammar = grammar;
        }
        PosInfo gpos;
        consumeLeadingPos(grammar, gpos);
        mergePos(section.pos, gpos);
        if (section.pos.label.empty()) {
          section.pos.label = "noun";
        }
        continue;
      }

      consumeLeadingMorphology(text, section.grammar);

      PosInfo lead;
      const bool hadPos = consumeLeadingPos(text, lead);
      if (hadPos && text.empty()) {
        flushPending();
        if (!section.senses.empty() && (!lead.label.empty() && lead.label != section.pos.label)) {
          flushSection();
        }
        mergePos(section.pos, lead);
        continue;
      }
      if (hadPos) {
        if (!section.senses.empty() && !lead.label.empty() && !section.pos.label.empty() &&
            lead.label != section.pos.label) {
          flushSection();
        }
        mergePos(section.pos, lead);
      }

      const bool numbered = consumeArabicSenseNumber(text) || consumeRomanSenseNumber(text);
      if (block.kind == DefinitionBlockKind::ListItem || numbered) {
        flushPending();
        pending = std::move(text);
        continue;
      }
      if (blockAllItalic(block) && lines.size() == 1 && !pending.empty() && text.size() < 80 &&
          countWords(text) <= 8) {
        if (!section.senses.empty()) {
          section.senses.back().examples.push_back(text);
        } else {
          pending += " \"";
          pending += text;
          pending += "\"";
        }
        continue;
      }
      if (block.kind == DefinitionBlockKind::Heading && text.size() < 32) {
        PosInfo headingPos;
        std::string heading = text;
        if (consumeLeadingPos(heading, headingPos) && heading.empty()) {
          flushPending();
          if (!section.senses.empty()) {
            flushSection();
          }
          mergePos(section.pos, headingPos);
          continue;
        }
      }
      if (!pending.empty()) {
        pending += " ";
      }
      pending += text;
    }
    if (!inlineExample.empty()) {
      flushPending();
      if (!section.senses.empty() && section.senses.back().examples.size() < 2) {
        section.senses.back().examples.push_back(std::move(inlineExample));
      }
    }
  }
  flushSection();
  polishCard(card, dutchTarget, queryWord);
  return card;
}

void appendWrapped(std::vector<DefinitionStyledLine>& out, const GfxRenderer& renderer, const std::string& text,
                   const int fontId, const EpdFontFamily::Style style, const int indentPx, const int maxWidth,
                   const int gapBefore) {
  if (text.empty()) {
    return;
  }
  DefinitionBlock block;
  block.runs.push_back(DefinitionTextRun(text, style));
  auto wrapped = wrapAtomsToWidth(renderer, tokenizeBlock(block), fontId, indentPx, maxWidth - indentPx);
  for (size_t i = 0; i < wrapped.size(); ++i) {
    wrapped[i].extraGapBeforePx = (i == 0) ? gapBefore : 0;
    out.push_back(std::move(wrapped[i]));
  }
}

}  // namespace

std::vector<DefinitionStyledLine> layoutDictionaryCard(const GfxRenderer& renderer, const std::string& html,
                                                       const int maxWidth, const char* targetLang,
                                                       const char* queryWord) {
  const bool dutch = (targetLang == nullptr || std::strcmp(targetLang, "nl") == 0);
  DictionaryCard card;
  const bool structured = parseKnownDictionaryHtml(html, card);
  if (structured) {
    finalizeStructuredCard(card, queryWord);
  }

  std::vector<DefinitionBlock> blocks;
  if (!structured || card.sections.empty()) {
    if (html.find('<') != std::string::npos) {
      blocks = parseHtmlToBlocks(html);
    } else {
      DefinitionBlock block;
      block.runs.push_back(DefinitionTextRun(html, EpdFontFamily::REGULAR));
      blocks.push_back(std::move(block));
    }
    card = cardFromBlocks(blocks, dutch, queryWord);
  }

  bool hasSense = false;
  for (const DictionarySection& section : card.sections) {
    if (!section.senses.empty()) {
      hasSense = true;
      break;
    }
  }
  if (!hasSense) {
    if (blocks.empty()) {
      if (html.find('<') != std::string::npos) {
        blocks = parseHtmlToBlocks(html);
      } else {
        DefinitionBlock block;
        block.runs.push_back(DefinitionTextRun(html, EpdFontFamily::REGULAR));
        blocks.push_back(std::move(block));
      }
    }
    return layoutDefinitionBlocks(renderer, blocks, maxWidth);
  }

  constexpr int kPosFont = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
  constexpr int kBodyFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
  constexpr int kExampleIndent = 14;
  std::vector<DefinitionStyledLine> out;

  size_t totalSenses = 0;
  for (const DictionarySection& section : card.sections) {
    totalSenses += section.senses.size();
  }
  const bool numberAll = totalSenses > 1;
  size_t senseIndex = 0;

  for (size_t si = 0; si < card.sections.size(); ++si) {
    const DictionarySection& section = card.sections[si];
    const std::string posLine = formatPos(section, dutch);
    if (!posLine.empty()) {
      appendWrapped(out, renderer, posLine, kPosFont, EpdFontFamily::REGULAR, 0, maxWidth, si == 0 ? 0 : 12);
    }
    for (size_t i = 0; i < section.senses.size(); ++i) {
      const DictionarySense& sense = section.senses[i];
      const int gap = (i == 0) ? (posLine.empty() ? (si == 0 ? 0 : 8) : 6) : 8;
      const bool shortGloss = countWords(sense.gloss) <= 8 && sense.gloss.size() <= 48;
      std::string head = sense.gloss;
      std::string tail;
      if (!shortGloss) {
        size_t cut = 0;
        int words = 0;
        while (cut < head.size() && words < 6) {
          while (cut < head.size() && head[cut] == ' ') {
            ++cut;
          }
          if (cut >= head.size()) {
            break;
          }
          while (cut < head.size() && head[cut] != ' ' && head[cut] != ';' && head[cut] != ',') {
            ++cut;
          }
          ++words;
          if (cut < head.size() && (head[cut] == ';' || (head[cut] == ',' && words >= 3))) {
            break;
          }
        }
        if (cut > 0 && cut < head.size()) {
          tail = trimCopy(head.substr(cut));
          head = trimCopy(head.substr(0, cut));
          while (!head.empty() && (head.back() == ',' || head.back() == ';')) {
            head.pop_back();
          }
          head = trimCopy(head);
        } else {
          tail = std::move(head);
          head.clear();
        }
      }
      DefinitionBlock glossBlock;
      if (numberAll) {
        glossBlock.runs.push_back(DefinitionTextRun(std::to_string(++senseIndex) + "  ", EpdFontFamily::REGULAR));
      }
      if (!head.empty()) {
        glossBlock.runs.push_back(DefinitionTextRun(head, EpdFontFamily::BOLD));
      }
      if (!tail.empty()) {
        glossBlock.runs.push_back(DefinitionTextRun((head.empty() ? "" : "  ") + tail, EpdFontFamily::REGULAR));
      }
      auto wrapped = wrapAtomsToWidth(renderer, tokenizeBlock(glossBlock), kBodyFont, 0, maxWidth);
      for (size_t li = 0; li < wrapped.size(); ++li) {
        wrapped[li].extraGapBeforePx = (li == 0) ? gap : 0;
        out.push_back(std::move(wrapped[li]));
      }
      for (const std::string& example : sense.examples) {
        appendWrapped(out, renderer, example, kBodyFont, EpdFontFamily::ITALIC, kExampleIndent, maxWidth, 3);
      }
    }
  }
  return out;
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
    "feminine of",
    "masculine of",
    "feminine form of",
    "masculine form of",
    "feminin de",
    "f\xC3\xA9minin de",
    "masculin de",
    "forme de",
    "vrouwelijke vorm van",
    "mannelijke vorm van",
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
  return letters >= 8;
}
