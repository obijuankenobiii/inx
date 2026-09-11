#pragma once

/**
 * @file FootnoteFragmentExtractor.h
 * @brief Extracts one element's inner markup from a raw (X)HTML document by id, for footnote bodies.
 */

#include <string>

/**
 * Finds the element with the given id in raw (X)HTML text and returns its inner markup (the raw
 * substring between its opening tag's '>' and its matching closing tag's '<'), ready to feed into
 * DictionaryDefinitionLayout's parseHtmlToBlocks().
 *
 * This is a simple, permissive string scan rather than a strict XML parse - matching
 * parseHtmlToBlocks's own philosophy (it silently strips any tag it doesn't recognize and keeps the
 * inner text), since real-world EPUB chapter/notes files are not always well-formed XML. Returns ""
 * if the id isn't found, or the matching element is self-closing (no inner content to show).
 */
std::string extractElementInnerHtmlById(const std::string& html, const std::string& targetId);
