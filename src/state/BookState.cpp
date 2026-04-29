/**
 * @file BookState.cpp
 * @brief Definitions for BookState.
 */

#include "state/BookState.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <algorithm>
#include <cstdint>

namespace {
constexpr uint8_t BOOKS_FILE_VERSION = 2;
constexpr uint8_t BOOKS_FILE_VERSION_V1 = 1;
constexpr char BOOKS_FILE[] = "/.metadata/books.bin";
}  

BookState BookState::instance;

void BookState::rebuildPathIndex() {
  pathIndex_.clear();
  pathIndex_.reserve(books.size());
  for (size_t i = 0; i < books.size(); ++i) {
    pathIndex_[books[i].path] = i;
  }
}

void BookState::addOrUpdateBook(const std::string& path, 
                                const std::string& title,
                                const std::string& author) {
  const auto mapIt = pathIndex_.find(path);
  if (mapIt != pathIndex_.end()) {
    books[mapIt->second].title = title;
    books[mapIt->second].author = author;
  } else {
    books.push_back({path, title, author, nextId++});
    pathIndex_[path] = books.size() - 1;
  }

  saveToFile();
}

std::vector<BookState::Book> BookState::getFavoriteBooks() const {
  std::vector<Book> result;
  std::copy_if(books.begin(), books.end(), std::back_inserter(result),
               [](const Book& book) { return book.isFavorite; });
  std::sort(result.begin(), result.end(),
            [](const Book& a, const Book& b) { return a.id > b.id; });
  return result;
}

std::vector<BookState::Book> BookState::getReadingBooks() const {
  std::vector<Book> result;
  std::copy_if(books.begin(), books.end(), std::back_inserter(result),
               [](const Book& book) { return book.isReading; });
  std::sort(result.begin(), result.end(),
            [](const Book& a, const Book& b) { return a.id > b.id; });
  return result;
}

std::vector<BookState::Book> BookState::getFinishedBooks() const {
  std::vector<Book> result;
  
  std::copy_if(books.begin(), books.end(), std::back_inserter(result),
               [](const Book& book) { return book.isFinished; });
  std::sort(result.begin(), result.end(),
            [](const Book& a, const Book& b) { return a.id > b.id; });
  return result;
}

std::vector<BookState::Book> BookState::getRecentlyAdded(int limit) const {
  std::vector<Book> sorted = books;
  std::sort(sorted.begin(), sorted.end(),
            [](const Book& a, const Book& b) { return a.id > b.id; });
  
  if (sorted.size() > limit) sorted.resize(limit);
  return sorted;
}

BookState::Book* BookState::findBookByPath(const std::string& path) {
  const auto mapIt = pathIndex_.find(path);
  if (mapIt == pathIndex_.end()) {
    return nullptr;
  }
  return &books[mapIt->second];
}

void BookState::toggleFavorite(const std::string& path) {
  Book* b = findBookByPath(path);
  if (b != nullptr) {
    b->isFavorite = !b->isFavorite;
    saveToFile();
  }
}

void BookState::setReading(const std::string& path, bool reading) {
  Book* b = findBookByPath(path);
  if (b != nullptr) {
    b->isReading = reading;
    saveToFile();
  }
}

void BookState::setFinished(const std::string& path, bool finished) {
  Book* b = findBookByPath(path);
  if (b != nullptr) {
    b->isFinished = finished;
    if (finished) b->isReading = false;
    saveToFile();
  }
}

bool BookState::saveToFile() const {
  SdMan.mkdir("/.metadata");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("BKS", BOOKS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, BOOKS_FILE_VERSION);
  serialization::writePod(outputFile, nextId);

  const uint32_t count = static_cast<uint32_t>(books.size());
  serialization::writePod(outputFile, count);

  uint32_t written = 0;
  for (const auto& book : books) {
    serialization::writeString(outputFile, book.path);
    serialization::writeString(outputFile, book.title);
    serialization::writeString(outputFile, book.author);
    serialization::writePod(outputFile, book.id);
    
    uint8_t flags = 0;
    if (book.isFavorite) flags |= 0x01;
    if (book.isReading) flags |= 0x02;
    if (book.isFinished) flags |= 0x04;
    serialization::writePod(outputFile, flags);
    if ((++written % 256u) == 0u) {
      yield();
    }
  }

  outputFile.close();
  return true;
}

bool BookState::loadFromFile() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("BKS", BOOKS_FILE, inputFile)) {
    books.clear();
    pathIndex_.clear();
    nextId = 1;
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);

  if (version != BOOKS_FILE_VERSION && version != BOOKS_FILE_VERSION_V1) {
    inputFile.close();
    return false;
  }

  serialization::readPod(inputFile, nextId);

  size_t count = 0;
  if (version == BOOKS_FILE_VERSION_V1) {
    uint16_t count16;
    serialization::readPod(inputFile, count16);
    count = count16;
  } else {
    uint32_t count32;
    serialization::readPod(inputFile, count32);
    count = static_cast<size_t>(count32);
  }

  books.clear();
  pathIndex_.clear();
  if (count != 0) {
    books.reserve(count);
  }

  for (size_t i = 0; i < count; i++) {
    Book book;
    serialization::readString(inputFile, book.path);
    serialization::readString(inputFile, book.title);
    serialization::readString(inputFile, book.author);
    serialization::readPod(inputFile, book.id);

    uint8_t flags;
    serialization::readPod(inputFile, flags);
    book.isFavorite = (flags & 0x01) != 0;
    book.isReading = (flags & 0x02) != 0;
    book.isFinished = (flags & 0x04) != 0;

    books.push_back(book);
    if ((i % 256u) == 255u) {
      yield();
    }
  }

  inputFile.close();
  rebuildPathIndex();
  return true;
}