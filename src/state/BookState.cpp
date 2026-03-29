#include "state/BookState.h"
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <algorithm>

namespace {
constexpr uint8_t BOOKS_FILE_VERSION = 1;
constexpr char BOOKS_FILE[] = "/.metadata/books.bin";
}

BookState BookState::instance;

void BookState::addOrUpdateBook(const std::string& path, 
                                const std::string& title,
                                const std::string& author) {
  auto it = std::find_if(books.begin(), books.end(), 
                         [&](const Book& book) { return book.path == path; });
  
  if (it != books.end()) {
    it->title = title;
    it->author = author;
  } else {
    books.push_back({path, title, author, nextId++});
  }

  saveToFile();
}

std::vector<BookState::Book> BookState::getFavoriteBooks() const {
  std::vector<Book> result;
  for (const auto& book : books) {
    if (book.isFavorite) result.push_back(book);
  }
  std::sort(result.begin(), result.end(),
            [](const Book& a, const Book& b) { return a.id > b.id; });
  return result;
}

std::vector<BookState::Book> BookState::getReadingBooks() const {
  std::vector<Book> result;
  for (const auto& book : books) {
    if (book.isReading) result.push_back(book);
  }
  std::sort(result.begin(), result.end(),
            [](const Book& a, const Book& b) { return a.id > b.id; });
  return result;
}

std::vector<BookState::Book> BookState::getFinishedBooks() const {
  std::vector<Book> result;
  for (const auto& book : books) {
    if (book.isFinished) result.push_back(book);
  }
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
  auto it = std::find_if(books.begin(), books.end(), 
                         [&](const Book& book) { return book.path == path; });
  return (it != books.end()) ? &(*it) : nullptr;
}

void BookState::toggleFavorite(const std::string& path) {
  auto it = std::find_if(books.begin(), books.end(), 
                         [&](const Book& book) { return book.path == path; });
  if (it != books.end()) {
    it->isFavorite = !it->isFavorite;
    saveToFile();
  }
}

void BookState::setReading(const std::string& path, bool reading) {
  auto it = std::find_if(books.begin(), books.end(), 
                         [&](const Book& book) { return book.path == path; });
  if (it != books.end()) {
    it->isReading = reading;
    saveToFile();
  }
}

void BookState::setFinished(const std::string& path, bool finished) {
  auto it = std::find_if(books.begin(), books.end(), 
                         [&](const Book& book) { return book.path == path; });
  if (it != books.end()) {
    it->isFinished = finished;
    if (finished) it->isReading = false;
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
  
  uint16_t count = static_cast<uint16_t>(books.size());
  serialization::writePod(outputFile, count);

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
  }

  outputFile.close();
  return true;
}

bool BookState::loadFromFile() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("BKS", BOOKS_FILE, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  
  if (version != BOOKS_FILE_VERSION) {
    inputFile.close();
    return false;
  }

  serialization::readPod(inputFile, nextId);
  
  uint16_t count;
  serialization::readPod(inputFile, count);

  books.clear();
  books.reserve(count);

  for (uint16_t i = 0; i < count; i++) {
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
  }

  inputFile.close();
  return true;
}