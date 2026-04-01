#pragma once

#include <SDCardManager.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>

class LibraryIndexer {
 public:
  static bool hasIndex() { return SdMan.exists("/.metadata/library/library.idx"); }
  static bool deleteIndex() { return SdMan.remove("/.metadata/library/library.idx"); }

  static int countBooks(FsFile& dir, int depth = 0) {
    if (depth > 10) return 0;
    int count = 0;
    dir.rewindDirectory();
    char name[256];
    FsFile file;
    while (file.openNext(&dir, O_RDONLY)) {
      file.getName(name, sizeof(name));
      if (name[0] == '.' || strcmp(name, ".metadata") == 0) {
        file.close();
        continue;
      }
      if (file.isDirectory()) {
        count += countBooks(file, depth + 1);
      } else {
        const char* ext = strrchr(name, '.');
        if (ext && (strcasecmp(ext, ".epub") == 0 || strcasecmp(ext, ".txt") == 0 || strcasecmp(ext, ".md") == 0 ||
                    strcasecmp(ext, ".xtc") == 0)) {
          count++;
        }
      }
      file.close();
    }
    return count;
  }

  static void indexAll(std::function<void(int, int, const char*)> progressCallback) {
    if (!SdMan.exists("/.metadata")) SdMan.mkdir("/.metadata");
    if (!SdMan.exists("/.metadata/library")) SdMan.mkdir("/.metadata/library");

    vTaskDelay(10 / portTICK_PERIOD_MS);

    int totalBooks = 0;
    FsFile root = SdMan.open("/");
    if (root) {
      totalBooks = countBooks(root);
      root.close();
    }

    if (totalBooks == 0) {
      if (progressCallback) progressCallback(0, 0, "No books found");
      return;
    }

    FsFile idxFile = SdMan.open("/.metadata/library/library.idx", O_WRITE | O_CREAT | O_TRUNC);
    if (!idxFile) {
      if (progressCallback) progressCallback(0, 0, "Failed to create index");
      return;
    }

    idxFile.write("LIBX", 4);
    uint8_t version = 1;
    idxFile.write(&version, 1);

    root = SdMan.open("/");
    if (root) {
      int currentBook = 0;
      indexDirectory(root, idxFile, currentBook, totalBooks, progressCallback, std::string("/"), 0);
      root.close();
    }

    idxFile.close();
    if (progressCallback) progressCallback(totalBooks, totalBooks, "Indexing complete!");
  }

 private:
  static void indexDirectory(FsFile& dir, FsFile& idxFile, int& currentBook, int totalBooks,
                             std::function<void(int, int, const char*)> progressCallback,
                             const std::string& currentPath, int depth) {
    if (depth > 10) return;

    dir.rewindDirectory();
    char name[256];

    // Write Directory Marker
    uint8_t dirMarker = 0xFF;
    idxFile.write(&dirMarker, 1);
    uint16_t pathLen = (uint16_t)currentPath.length();
    idxFile.write(&pathLen, sizeof(pathLen));
    idxFile.write(currentPath.c_str(), pathLen);

    uint16_t entryCount = 0;
    uint32_t countPos = idxFile.position();
    idxFile.write(&entryCount, sizeof(entryCount));

    FsFile file;
    while (file.openNext(&dir, O_RDONLY)) {
      file.getName(name, sizeof(name));

      if (name[0] == '.' || strcmp(name, ".metadata") == 0) {
        file.close();
        continue;
      }

      std::string thisItemPath;
      if (currentPath == "/")
        thisItemPath = "/" + std::string(name);
      else
        thisItemPath = currentPath + "/" + std::string(name);

      if (file.isDirectory()) {
        indexDirectory(file, idxFile, currentBook, totalBooks, progressCallback, thisItemPath, depth + 1);
        entryCount++;
      } else {
        const char* ext = strrchr(name, '.');
        if (ext && (strcasecmp(ext, ".epub") == 0 || strcasecmp(ext, ".txt") == 0 || strcasecmp(ext, ".md") == 0 ||
                    strcasecmp(ext, ".xtc") == 0 || strcasecmp(ext, ".xtch") == 0)) {
          uint8_t bookMarker = 0x01;
          idxFile.write(&bookMarker, 1);

          uint16_t pLen = (uint16_t)thisItemPath.length();
          idxFile.write(&pLen, sizeof(pLen));
          idxFile.write(thisItemPath.c_str(), pLen);

          uint8_t nLen = (uint8_t)strlen(name);
          idxFile.write(&nLen, sizeof(nLen));
          idxFile.write(name, nLen);

          std::string displayName = cleanFilename(name);
          uint8_t dLen = (uint8_t)displayName.length();
          idxFile.write(&dLen, sizeof(dLen));
          idxFile.write(displayName.c_str(), dLen);

          std::string folderName = extractRawFolderName(currentPath.c_str());
          uint8_t fLen = (uint8_t)folderName.length();
          idxFile.write(&fLen, sizeof(fLen));
          idxFile.write(folderName.c_str(), fLen);

          currentBook++;
          entryCount++;
          if (progressCallback) progressCallback(currentBook, totalBooks, name);
        }
      }
      file.close();
    }

    uint32_t endPos = idxFile.position();
    idxFile.seek(countPos);
    idxFile.write(&entryCount, sizeof(entryCount));
    idxFile.seek(endPos);

    uint8_t endMarker = 0xFE;
    idxFile.write(&endMarker, 1);
  }

  static std::string extractRawFolderName(const char* path) {
    std::string result = path;
    if (result.empty() || result == "/") return "Root";
    if (result.back() == '/') result.pop_back();
    size_t lastSlash = result.find_last_of('/');
    if (lastSlash != std::string::npos) {
      std::string folderName = result.substr(lastSlash + 1);
      return folderName.empty() ? "Root" : folderName;
    }
    return result;
  }

  static std::string cleanFilename(const char* name) {
    std::string result = name;
    size_t dot = result.find_last_of('.');
    if (dot != std::string::npos) {
      result.resize(dot);
    }
    std::replace(result.begin(), result.end(), '_', ' ');
    std::replace(result.begin(), result.end(), '-', ' ');
    return result;
  }
};