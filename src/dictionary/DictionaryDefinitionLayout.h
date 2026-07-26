#pragma once

/**
 * @file DictionaryDefinitionLayout.h
 * @brief Parses a StarDict "h" (HTML) definition into styled, word-wrapped lines.
 *
 * Shared between EpubDictionaryUi (the in-reader lookup panel) and the Recent activity's Dictionary
 * list (viewing a previously saved word) so both preserve the same bold/italic/heading formatting
 * instead of falling back to plain text.
 */

#include <EpdFontFamily.h>
#include <GfxRenderer.h>

#include <cstdint>
#include <string>
#include <vector>

enum class DefinitionBlockKind : uint8_t { Paragraph, Heading, ListItem };

/** A run of text within a block that shares one font style (bold/italic/bold-italic/regular, from
 *  nested <b>/<strong>/<i>/<em> tags). May contain '\n' from <br>. */
struct DefinitionTextRun {
  DefinitionTextRun() = default;
  DefinitionTextRun(std::string t, EpdFontFamily::Style s) : text(std::move(t)), style(s) {}

  std::string text;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
};

/** One block-level chunk of a parsed StarDict "h" (HTML) definition - a paragraph, heading, or list
 *  item, laid out with its own font size/indent at render time; each run within it may still carry
 *  its own bold/italic style. */
struct DefinitionBlock {
  DefinitionBlockKind kind = DefinitionBlockKind::Paragraph;
  int headingLevel = 1;  // 1-6, only meaningful when kind == Heading
  std::vector<DefinitionTextRun> runs;
};

/** One atom to render within a wrapped line: a word (or word fragment, if a style change happened
 *  mid-word) in one style, or a hard line break with text empty. spaceBefore marks whether a space
 *  should be rendered/measured before it when it's not the first atom on a line. */
struct DefinitionTextAtom {
  DefinitionTextAtom() = default;
  DefinitionTextAtom(std::string t, EpdFontFamily::Style s, bool hb, bool sb)
      : text(std::move(t)), style(s), hardBreak(hb), spaceBefore(sb) {}

  std::string text;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  bool hardBreak = false;
  bool spaceBefore = false;
};

/** One already-wrapped, already-styled line ready to render in the definition panel. */
struct DefinitionStyledLine {
  DefinitionStyledLine() = default;
  DefinitionStyledLine(std::vector<DefinitionTextAtom> a, int f, int indent, int gap)
      : atoms(std::move(a)), fontId(f), indentPx(indent), extraGapBeforePx(gap) {}

  std::vector<DefinitionTextAtom> atoms;
  int fontId = 0;
  int indentPx = 0;
  int extraGapBeforePx = 0;
};

/** Parses a StarDict "h" (HTML) definition into block-level chunks (paragraph/heading/list item),
 *  each holding one or more style runs, so the caller can render each block with its own font
 *  size/indent and each run with its own bold/italic style. <b>/<strong> and <i>/<em> (nestable, so
 *  e.g. <b><i>...</i></b> becomes bold-italic) set style; heading blocks are bold by default even
 *  without an explicit <b>. <br> becomes a forced line break within the current block. */
std::vector<DefinitionBlock> parseHtmlToBlocks(const std::string& html);

/** Flattens parsed HTML blocks into wrapped, styled lines with per-block font/indent/bullet, each
 *  narrowed to maxWidth (minus that block's indent) at its own font. */
std::vector<DefinitionStyledLine> layoutDefinitionBlocks(const GfxRenderer& renderer,
                                                         const std::vector<DefinitionBlock>& blocks, int maxWidth);

/** Draws pre-wrapped/styled lines top-down starting at (x, startY), beginning from lines[startIndex]
 *  (for scrollable panels like EpubDictionaryUi's) and stopping once a line would cross bottomLimit. */
void renderStyledLines(GfxRenderer& renderer, const std::vector<DefinitionStyledLine>& lines, int x, int startY,
                       int bottomLimit, size_t startIndex = 0);
