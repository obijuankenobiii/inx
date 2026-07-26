/**
 * @file DictionaryDefinitionLayout.cpp
 * @brief Definitions for the shared dictionary-definition HTML parser/layout.
 */

#include "DictionaryDefinitionLayout.h"

#include <algorithm>
#include <cctype>

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

}  // namespace

std::vector<DefinitionBlock> parseHtmlToBlocks(const std::string& html) {
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
  while (i < html.size()) {
    if (html[i] == '<') {
      const size_t close = html.find('>', i);
      if (close == std::string::npos) {
        break;  // unterminated tag - stop rather than emit garbage
      }
      std::string tag = html.substr(i + 1, close - i - 1);
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
    appendCollapsedChar(current, html[i]);
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
