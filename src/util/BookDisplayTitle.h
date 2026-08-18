#pragma once

/**
 * @file BookDisplayTitle.h
 * @brief Resolve a human book title from EPUB metadata / recent / book state.
 *
 * Library lists must never prefer the on-disk filename when a real OPF (or
 * previously stored) title is available.
 */

#include <Epub/BookMetadataCache.h>

#include <string>

#include "state/BookState.h"
#include "state/RecentBooks.h"
#include "util/StringUtils.h"

namespace BookDisplayTitle {

inline std::string epubCachePath(const std::string& bookPath) {
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(bookPath));
}

/** Returns OPF / cached title when present; empty string otherwise. */
inline std::string lookup(const std::string& bookPath) {
  if (bookPath.empty()) {
    return {};
  }

  if (StringUtils::checkFileExtension(bookPath, ".epub")) {
    BookMetadataCache metadata(epubCachePath(bookPath));
    if (metadata.load() && !metadata.coreMetadata.title.empty()) {
      return metadata.coreMetadata.title;
    }
  }

  RECENT_BOOKS.loadFromFile();
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    if (book.path == bookPath && !book.title.empty()) {
      return book.title;
    }
  }

  BookState::Book book;
  if (BOOK_STATE.findBook(bookPath, book) && !book.title.empty()) {
    return book.title;
  }

  return {};
}

/** Prefer cached metadata title; otherwise keep the provided filename-based fallback. */
inline std::string resolve(const std::string& bookPath, const std::string& fallback) {
  const std::string title = lookup(bookPath);
  return title.empty() ? fallback : title;
}

}  // namespace BookDisplayTitle
