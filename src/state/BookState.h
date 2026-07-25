#pragma once

/**
 * @file BookState.h
 * @brief Public interface and types for BookState.
 *
 * Per-book favorite/reading/finished flags for the whole library, persisted at
 * /.metadata/books.bin. Unlike the old design, nothing is cached in RAM between calls - every method
 * opens the file, does exactly the read/scan/write it needs, and closes it before returning. A full
 * library's worth of Book records (each with a path string, plus title/author for the small subset
 * that's favorited or currently reading) held resident for the app's whole session was the single
 * biggest avoidable RAM cost on boot; trading that for an on-disk scan per call is worth it since
 * every call site here is already either user-action-triggered or memoized by its own caller (e.g.
 * LibraryActivity::bookStateCache_), never a per-frame hot path.
 */

#include <cstdint>
#include <string>
#include <vector>

class BookState {
 public:
  struct Book {
    std::string path;
    std::string title;
    std::string author;
    uint32_t id = 0;
    bool isFavorite = false;
    bool isReading = false;
    bool isFinished = false;

    Book() = default;

    Book(const std::string& p, const std::string& t, const std::string& a, uint32_t idNum = 0)
        : path(p), title(t), author(a), id(idNum) {}
  };

  static BookState& getInstance() { return instance; }

  /** Looks up a single book's stored state. Returns false (out left untouched) if not tracked. */
  bool findBook(const std::string& path, Book& out) const;

  /** Adds a new tracked book, or updates title/author for an existing one. */
  void addOrUpdateBook(const std::string& path, const std::string& title, const std::string& author);

  /** Toggles favorite, tracking the book first (using fallbackTitle) if it wasn't already, and
   *  restoring fallbackTitle if the stored title had been compacted away. */
  void toggleFavorite(const std::string& path, const std::string& fallbackTitle = "");
  /** Sets the reading flag, tracking the book first (using fallbackTitle) if isReading and it wasn't
   *  already, and restoring fallbackTitle the same way toggleFavorite does. */
  void setReading(const std::string& path, bool isReading, const std::string& fallbackTitle = "");
  void setFinished(const std::string& path, bool isFinished);
  void removeBook(const std::string& path);

  /** Repoints an existing book's path entry (favorite/reading/finished flags, id) after a rename/move.
   *  No-op if oldPath isn't tracked. */
  void renamePath(const std::string& oldPath, const std::string& newPath);

  /** Erases all tracked books. If writeFile is false, this is a no-op (for callers that already
   *  deleted books.bin directly and don't want it recreated empty). */
  void clear(bool writeFile = true);

  /** Full on-disk scans - each builds and returns a fresh list; nothing is cached afterward. */
  std::vector<Book> getAllBooks() const;
  std::vector<Book> getFavoriteBooks() const;
  std::vector<Book> getReadingBooks() const;
  std::vector<Book> getFinishedBooks() const;

 private:
  BookState() = default;
  ~BookState() = default;
  static BookState instance;
};

#define BOOK_STATE BookState::getInstance()
