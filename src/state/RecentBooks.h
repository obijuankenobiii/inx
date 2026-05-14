#pragma once

/**
 * @file RecentBooks.h
 * @brief Public interface and types for RecentBooks.
 */

#include <string>
#include <vector>

struct RecentBook {
  std::string path;
  std::string cachePath;
  std::string title;
  std::string author;
  float progress = -1.0f;

  RecentBook() = default;
  
  RecentBook(const std::string& p, const std::string& cp, const std::string& t, 
             const std::string& a, float prog = -1.0f)
      : path(p), cachePath(cp), title(t), author(a), progress(prog) {}

  bool operator==(const RecentBook& other) const { return path == other.path; }
};

class RecentBooks {
  static RecentBooks instance;
  std::vector<RecentBook> recentBooks;

 public:
  ~RecentBooks() = default;

  /**
   * @brief Get the singleton instance
   */
  static RecentBooks& getInstance() { return instance; }

  /**
   * @brief Add or update a book in the recent list
   * @param path Full path to the EPUB file
   * @param cachePath Path to the book's cache directory
   * @param title Book title
   * @param author Book author
   * @param progress Reading progress (0.0-1.0, -1.0 if unknown)
   */
  void addBook(const std::string& path, const std::string& cachePath,
               const std::string& title, const std::string& author, 
               float progress = -1.0f, bool saveNow = true);

  /**
   * @brief Update reading progress for an existing book
   * @param path Full path to the EPUB file
   * @param progress Reading progress (0.0-1.0)
   */
  void updateProgress(const std::string& path, float progress);

  /**
   * @brief Remove a book from the recent list
   * @param path Full path to the EPUB file
   */
  void removeBook(const std::string& path);

  /**
   * @brief Get the list of recent books (most recent first)
   */
  const std::vector<RecentBook>& getBooks() const { return recentBooks; }

  /**
   * @brief Get the number of recent books
   */
  int getCount() const { return static_cast<int>(recentBooks.size()); }

  /**
   * @brief Save recent books to file
   */
  bool saveToFile() const;

  /**
   * @brief Load recent books from file
   */
  bool loadFromFile();
};

#define RECENT_BOOKS RecentBooks::getInstance()
