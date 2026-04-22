/**
 * @file Statistics.cpp
 * @brief Definitions for Statistics.
 */

#include "state/Statistics.h"
#include <SDCardManager.h>
#include <cstring>
#include <string>
#include <vector>
#include <Arduino.h>

static const char* STATS_DIR = ".system/";
static const char* statistics_FILE = ".system/statistics.bin";

constexpr uint32_t STATS_FILE_VERSION = 2;  
constexpr uint32_t STATS_MAGIC_NUMBER = 0x53544154; 

/**
 * RAII wrapper for FsFile that ensures file is closed when object goes out of scope.
 */
class FileGuard {
private:
    FsFile& file;
public:
    explicit FileGuard(FsFile& f) : file(f) {}
    ~FileGuard() { 
        if (file) {
            file.close();
        }
    }
    FileGuard(const FileGuard&) = delete;
    FileGuard& operator=(const FileGuard&) = delete;
};

/**
 * Saves reading statistics for a specific book to a binary file in its cache directory.
 * Uses a temporary file and atomic rename to prevent corruption.
 * 
 * @param cachePath Path to the book's cache directory
 * @param stats The book reading statistics to save
 */
void saveBookStats(const char* cachePath, const BookReadingStats& stats) {
    if (!cachePath) return;
    std::string statsPath = std::string(cachePath) + "/statistics.bin";
    std::string tempPath = statsPath + ".tmp";
    
    bool writeSuccess = false;
    
    
    {
        FsFile file;
        FileGuard guard(file);
        
        if (SdMan.openFileForWrite("STATS", tempPath.c_str(), file)) {
            uint32_t magic = STATS_MAGIC_NUMBER;
            uint32_t version = STATS_FILE_VERSION;
            
            file.write(&magic, sizeof(uint32_t));
            file.write(&version, sizeof(uint32_t));
            
            
            file.write(&stats.totalReadingTimeMs, sizeof(uint32_t));
            file.write(&stats.totalPagesRead, sizeof(uint32_t));
            file.write(&stats.totalChaptersRead, sizeof(uint32_t));
            file.write(&stats.lastReadTimeMs, sizeof(uint32_t));
            file.write(&stats.progressPercent, sizeof(float));
            file.write(&stats.lastSpineIndex, sizeof(uint16_t));
            file.write(&stats.lastPageNumber, sizeof(uint16_t));
            file.write(&stats.avgPageTimeMs, sizeof(uint32_t));
            file.write(&stats.sessionCount, sizeof(uint32_t));  
            
            uint32_t pathLength = stats.path.length();
            file.write(&pathLength, sizeof(uint32_t));
            if (pathLength > 0) file.write(stats.path.c_str(), pathLength);
            
            uint32_t titleLength = stats.title.length();
            file.write(&titleLength, sizeof(uint32_t));
            if (titleLength > 0) file.write(stats.title.c_str(), titleLength);

            uint32_t authorLength = stats.author.length();
            file.write(&authorLength, sizeof(uint32_t));
            if (authorLength > 0) file.write(stats.author.c_str(), authorLength);
            
            writeSuccess = true;
        }
        
    }
    
    
    if (writeSuccess) {
        SdMan.remove(statsPath.c_str());
        if (!SdMan.rename(tempPath.c_str(), statsPath.c_str())) {
            
            SdMan.remove(tempPath.c_str());
        }
    } else {
        SdMan.remove(tempPath.c_str());
    }
}

/**
 * Loads reading statistics for a specific book from its cache directory.
 * Validates file format using magic number and version.
 * 
 * @param cachePath Path to the book's cache directory
 * @param stats Reference to populate with loaded statistics
 * @return true if statistics were successfully loaded, false otherwise
 */
bool loadBookStats(const char* cachePath, BookReadingStats& stats) {
    if (!cachePath) return false;
    std::string statsPath = std::string(cachePath) + "/statistics.bin";
    
    FsFile file;
    FileGuard guard(file);  
    
    if (!SdMan.openFileForRead("STATS", statsPath.c_str(), file)) {
        return false;
    }
    
    uint32_t magic, version;
    if (file.read(&magic, sizeof(uint32_t)) != sizeof(uint32_t) || magic != STATS_MAGIC_NUMBER) {
        return false;  
    }
    
    if (file.read(&version, sizeof(uint32_t)) != sizeof(uint32_t)) {
        return false;  
    }
    
    
    file.read(&stats.totalReadingTimeMs, sizeof(uint32_t));
    file.read(&stats.totalPagesRead, sizeof(uint32_t));
    file.read(&stats.totalChaptersRead, sizeof(uint32_t));
    file.read(&stats.lastReadTimeMs, sizeof(uint32_t));
    file.read(&stats.progressPercent, sizeof(float));
    file.read(&stats.lastSpineIndex, sizeof(uint16_t));
    file.read(&stats.lastPageNumber, sizeof(uint16_t));
    file.read(&stats.avgPageTimeMs, sizeof(uint32_t));
    
    
    if (version >= 2) {
        file.read(&stats.sessionCount, sizeof(uint32_t));
    } else {
        stats.sessionCount = 0;  
    }
    
    
    uint32_t pathLen;
    if (file.read(&pathLen, sizeof(uint32_t)) == sizeof(uint32_t) && pathLen < 512) {
        if (pathLen > 0) {
            stats.path.resize(pathLen);
            file.read(&stats.path[0], pathLen);
        } else {
            stats.path.clear();
        }
    }

    
    uint32_t titleLen;
    if (file.read(&titleLen, sizeof(uint32_t)) == sizeof(uint32_t) && titleLen < 512) {
        if (titleLen > 0) {
            stats.title.resize(titleLen);
            file.read(&stats.title[0], titleLen);
        } else {
            stats.title.clear();
        }
    }

    uint32_t authorLen;
    if (file.read(&authorLen, sizeof(uint32_t)) == sizeof(uint32_t) && authorLen < 512) {
        if (authorLen > 0) {
            stats.author.resize(authorLen);
            file.read(&stats.author[0], authorLen);
        } else {
            stats.author.clear();
        }
    }
    
    
    return true;
}

/**
 * Retrieves reading statistics for all books in the EPUB cache directory.
 * Scans through all subdirectories in /.metadata/epub and loads stats for each.
 * 
 * @return Vector containing statistics for all books that have saved stats
 */
std::vector<BookReadingStats> getAllBooksStats() {
    std::vector<BookReadingStats> result;
    
    result.reserve(10); 
    
    FsFile root;
    FileGuard rootGuard(root);  
    
    root = SdMan.open("/.metadata/epub");
    if (!root || !root.isDirectory()) {
        return result;  
    }
    
    root.rewindDirectory();
    char fileName[128]; 
    
    while (true) {
        FsFile entry;
        FileGuard entryGuard(entry);  
        
        entry = root.openNextFile();
        if (!entry) break;
        
        if (entry.isDirectory()) {
            entry.getName(fileName, sizeof(fileName));
            std::string path = "/.metadata/epub/" + std::string(fileName);
            
            BookReadingStats stats;
            if (loadBookStats(path.c_str(), stats)) {
                result.push_back(stats);
            }
        }
        
    }
    
    
    return result;
}

/**
 * Retrieves reading statistics for a specific book.
 * 
 * @param bookPath Path to the book's cache directory
 * @param stats Reference to populate with the book's statistics
 * @return true if statistics were successfully loaded, false otherwise
 */
bool getBookStats(const char* bookPath, BookReadingStats& stats) {
    return loadBookStats(bookPath, stats);
}

/**
 * Loads global reading statistics from the main statistics file.
 * Zeros out the stats struct if read fails.
 * 
 * @param stats Reference to populate with global statistics
 * @return true if statistics were successfully loaded, false otherwise
 */
bool loadGlobalStats(GlobalReadingStats& stats) {
    
    memset(&stats, 0, sizeof(GlobalReadingStats));
    
    FsFile file;
    FileGuard guard(file);  
    
    if (!SdMan.openFileForRead("STATS", statistics_FILE, file)) {
        return false;  
    }
    
    size_t bytesRead = file.read(&stats, sizeof(GlobalReadingStats));
    
    
    return bytesRead == sizeof(GlobalReadingStats);
}

/**
 * Saves global reading statistics to the main statistics file.
 * Uses a temporary file and atomic rename to prevent corruption.
 * Creates the statistics directory if it doesn't exist.
 * 
 * @param stats Global statistics to save
 */
void saveGlobalStats(const GlobalReadingStats& stats) {
    SdMan.mkdir(STATS_DIR);
    std::string tempPath = std::string(statistics_FILE) + ".tmp";
    
    FsFile file;
    FileGuard guard(file);  
    
    if (SdMan.openFileForWrite("STATS", tempPath.c_str(), file)) {
        file.write(&stats, sizeof(GlobalReadingStats));
        
    }
    
    
    if (file) {  
        SdMan.remove(statistics_FILE);
        SdMan.rename(tempPath.c_str(), statistics_FILE);
    } else {
        SdMan.remove(tempPath.c_str());
    }
}

/**
 * Generates global reading statistics by aggregating all individual book statistics.
 * Calculates totals for books started, finished, reading time, pages, chapters, and sessions.
 * 
 * @return Aggregated global reading statistics
 */
GlobalReadingStats generateGlobalStats() {
    GlobalReadingStats global;
    memset(&global, 0, sizeof(GlobalReadingStats));
    
    std::vector<BookReadingStats> allBooks = getAllBooksStats();
    
    for (const auto& book : allBooks) {
        global.totalBooksStarted++;
        global.totalReadingTimeMs += book.totalReadingTimeMs;
        global.totalPagesRead += book.totalPagesRead;
        global.totalChaptersRead += book.totalChaptersRead;
        global.totalSessions += book.sessionCount;
        if (book.progressPercent >= 99.0f) global.totalBooksFinished++;
    }
    return global;
}