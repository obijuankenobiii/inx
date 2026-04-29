#pragma once

/**
 * @file BookState.h
 * @brief Public interface and types for BookState.
 */

#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

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

private:
  static BookState instance;
  uint32_t nextId = 1;
  /** path string → index in `books` for O(1) lookup (rebuilt on load). */
  std::unordered_map<std::string, size_t> pathIndex_;
  void rebuildPathIndex();

public:
  std::vector<Book> books;
  
  ~BookState() = default;

  static BookState& getInstance() { return instance; }

  void addOrUpdateBook(const std::string& path, 
                       const std::string& title,
                       const std::string& author);

  std::vector<Book> getFavoriteBooks() const;
  std::vector<Book> getReadingBooks() const;
  std::vector<Book> getFinishedBooks() const;
  std::vector<Book> getRecentlyAdded(int limit = 10) const;

  Book* findBookByPath(const std::string& path);

  void toggleFavorite(const std::string& path);
  void setReading(const std::string& path, bool isReading);
  void setFinished(const std::string& path, bool isFinished);

  bool saveToFile() const;
  bool loadFromFile();
};

#define BOOK_STATE BookState::getInstance()