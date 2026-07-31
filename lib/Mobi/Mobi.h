#pragma once

/**
 * @file Mobi.h
 * @brief Converts a classic MOBI6 ebook into a minimal EPUB zip so the existing Epub/ChapterHtmlSlimParser/
 *        BookMetadataCache pipeline can read it completely unmodified.
 */

#include <string>

/**
 * MOBI6 (PalmDOC-era Mobipocket) files store their content as one big PalmDOC/LZ77- or uncompressed-text
 * blob, split across fixed-size records, plus separate image records - not a zip of XHTML files like EPUB.
 * Rather than teach the renderer a second container format, this does a one-shot transcode: decompress the
 * text into a single "OEBPS/content.html" and re-package it (plus any embedded images) as a minimal, valid
 * EPUB zip. From that point on the book is opened exactly like any other EPUB - no changes needed anywhere
 * else in the reading pipeline.
 *
 * Not supported (returns false): KF8/AZW3 (the newer hybrid format - a different, much more complex
 * container), HUFF/CDIC-compressed text (rare; used mostly for dictionaries), and DRM-protected books.
 */
namespace Mobi {

/**
 * Transcodes mobiPath into a minimal EPUB zip at outEpubPath. Safe to call every time a .mobi is opened -
 * callers should cache/skip this when outEpubPath already exists (see EpubActivity's mobi dispatch).
 * @return true on success. On failure, outEpubPath may be left as a partial/nonexistent file.
 */
bool convertToEpub(const std::string& mobiPath, const std::string& outEpubPath);

}  // namespace Mobi
