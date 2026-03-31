#include "state/RecentBooks.h"
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <algorithm>

namespace {
constexpr uint8_t RECENT_BOOKS_FILE_VERSION = 4;
constexpr char RECENT_BOOKS_FILE[] = "/.metadata/recent.bin";
constexpr int MAX_RECENT_BOOKS = 8;
}

RecentBooks RecentBooks::instance;

void RecentBooks::addBook(const std::string& path, const std::string& cachePath,
                          const std::string& title, const std::string& author, 
                          float progress) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(), 
                         [&](const RecentBook& book) { return book.path == path; });
  
  if (it != recentBooks.end()) {
    if (progress >= 0.0f) {
      it->progress = progress;
    }
    RecentBook book = *it;
    recentBooks.erase(it);
    recentBooks.insert(recentBooks.begin(), book);
  } else {
    recentBooks.insert(recentBooks.begin(), {path, cachePath, title, author, progress});
  }

  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  saveToFile();
}

void RecentBooks::updateProgress(const std::string& path, float progress) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(), 
                         [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    it->progress = progress;
    saveToFile();
  }
}

bool RecentBooks::saveToFile() const {
  SdMan.mkdir("/.metadata");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("RBS", RECENT_BOOKS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, RECENT_BOOKS_FILE_VERSION);
  
  uint8_t count = static_cast<uint8_t>(recentBooks.size());
  serialization::writePod(outputFile, count);

  for (const auto& book : recentBooks) {
    serialization::writeString(outputFile, book.path);
    serialization::writeString(outputFile, book.cachePath);
    serialization::writeString(outputFile, book.title);
    serialization::writeString(outputFile, book.author);
    serialization::writePod(outputFile, book.progress);
  }

  outputFile.close();
  return true;
}

bool RecentBooks::loadFromFile() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("RBS", RECENT_BOOKS_FILE, inputFile)) {
    saveToFile();
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  
  // Basic sanity check
  if (version < 1 || version > 4) {
    inputFile.close();
    return false;
  }
  
  if (version == 1 || version == 2) {
    uint8_t count;
    serialization::readPod(inputFile, count);
    
    // Sanity check count
    if (count > MAX_RECENT_BOOKS * 2) {
      inputFile.close();
      return false;
    }
    
    recentBooks.clear();
    recentBooks.reserve(count);
    
    for (uint8_t i = 0; i < count; i++) {
      std::string path, title, author;
      serialization::readString(inputFile, path);
      serialization::readString(inputFile, title);
      serialization::readString(inputFile, author);
      
      // Skip if path is empty (corrupted)
      if (path.empty()) {
        recentBooks.clear();
        inputFile.close();
        return false;
      }
      
      recentBooks.push_back({path, "", title, author, -1.0f});
    }
    
    if (version < RECENT_BOOKS_FILE_VERSION) {
      saveToFile();
    }
  } else if (version == 3) {
    uint8_t count;
    serialization::readPod(inputFile, count);
    
    if (count > MAX_RECENT_BOOKS * 2) {
      inputFile.close();
      return false;
    }
    
    recentBooks.clear();
    recentBooks.reserve(count);

    for (uint8_t i = 0; i < count; i++) {
      std::string path, title, author;
      float progress;
      serialization::readString(inputFile, path);
      serialization::readString(inputFile, title);
      serialization::readString(inputFile, author);
      serialization::readPod(inputFile, progress);
      
      if (path.empty()) {
        recentBooks.clear();
        inputFile.close();
        return false;
      }
      
      recentBooks.push_back({path, "", title, author, progress});
    }
    
    if (version < RECENT_BOOKS_FILE_VERSION) {
      saveToFile();
    }
  } else (version == RECENT_BOOKS_FILE_VERSION) {
    uint8_t count;
    serialization::readPod(inputFile, count);
    
    if (count > MAX_RECENT_BOOKS * 2) {
      inputFile.close();
      return false;
    }
    
    recentBooks.clear();
    recentBooks.reserve(count);

    for (uint8_t i = 0; i < count; i++) {
      std::string path, cachePath, title, author;
      float progress;
      serialization::readString(inputFile, path);
      serialization::readString(inputFile, cachePath);
      serialization::readString(inputFile, title);
      serialization::readString(inputFile, author);
      serialization::readPod(inputFile, progress);
      
      if (path.empty()) {
        recentBooks.clear();
        inputFile.close();
        return false;
      }
      
      recentBooks.push_back({path, cachePath, title, author, progress});
    }
  }

  inputFile.close();
  return true;
}

void RecentBooks::removeBook(const std::string& path) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(), 
                         [&](const RecentBook& book) { return book.path == path; });
  
  if (it != recentBooks.end()) {
    recentBooks.erase(it);
    saveToFile();
  }
}