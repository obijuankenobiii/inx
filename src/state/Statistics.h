#pragma once

/**
 * @file Statistics.h
 * @brief Public interface and types for Statistics.
 */

#include <cstdint>
#include <string>
#include <vector>

/**
 * Structure containing reading statistics for a single book.
 * 
 * This structure tracks reading progress, time spent, pages read,
 * and other metrics for a specific EPUB book.
 */
struct BookReadingStats {
    std::string path;                ///< Full filesystem path to the book file
    std::string title;               ///< Title of the book from EPUB metadata
    std::string author;              ///< Author of the book from EPUB metadata
    
    uint32_t totalReadingTimeMs;     ///< Total time spent reading in milliseconds
    uint32_t totalPagesRead;         ///< Total number of pages turned
    uint32_t totalChaptersRead;      ///< Number of chapters fully read
    uint32_t lastReadTimeMs;         ///< Timestamp of last reading session
    float progressPercent;           ///< Current reading progress (0.0-100.0)
    uint16_t lastSpineIndex;         ///< Index of last accessed chapter
    uint16_t lastPageNumber;         ///< Page number within last chapter
    uint32_t avgPageTimeMs;          ///< Average time per page in milliseconds
    uint32_t sessionCount;           ///< Number of reading sessions for this book
    
    /**
     * Default constructor initializing all numeric fields to zero
     * and string fields to empty.
     */
    BookReadingStats() : 
        path(""),
        title(""),
        author(""),
        totalReadingTimeMs(0),
        totalPagesRead(0),
        totalChaptersRead(0),
        lastReadTimeMs(0),
        progressPercent(0),
        lastSpineIndex(0),
        lastPageNumber(0),
        avgPageTimeMs(0),
        sessionCount(0) {}
};

/**
 * Structure containing aggregated reading statistics across all books.
 */
struct GlobalReadingStats {
    uint32_t totalBooksStarted;      ///< Total number of books ever opened
    uint32_t totalBooksFinished;     ///< Total number of books completed (progress >= 99.9%)
    uint32_t totalReadingTimeMs;     ///< Total reading time across all books in milliseconds
    uint32_t totalPagesRead;         ///< Total pages turned across all books
    uint32_t totalChaptersRead;      ///< Total chapters read across all books
    uint32_t totalSessions;          ///< Total number of reading sessions across all books
    
    /**
     * Default constructor initializing all fields to zero.
     */
    GlobalReadingStats() : 
        totalBooksStarted(0),
        totalBooksFinished(0),
        totalReadingTimeMs(0),
        totalPagesRead(0),
        totalChaptersRead(0),
        totalSessions(0) {}
};

/**
 * Saves reading statistics for a book to its cache directory.
 * 
 * @param cachePath Path to the book's cache directory
 * @param stats The book reading statistics to persist
 */
void saveBookStats(const char* cachePath, const BookReadingStats& stats);

/**
 * Loads reading statistics for a book from its cache directory.
 * 
 * @param cachePath Path to the book's cache directory
 * @param stats Reference to populate with loaded statistics
 * @return true if statistics were successfully loaded, false otherwise
 */
bool loadBookStats(const char* cachePath, BookReadingStats& stats);

/**
 * Retrieves reading statistics for all books with per-book stats on the SD card.
 *
 * Scans `/.metadata/epub` (EPUB caches) and `/.metadata/xtc` (XTC caches)
 * for `statistics.bin` files.
 *
 * @return Vector containing statistics for all books with valid stats files
 */
std::vector<BookReadingStats> getAllBooksStats();

/**
 * Retrieves reading statistics for a specific book.
 * 
 * @param bookPath Path to the book's cache directory
 * @param stats Reference to populate with the book's statistics
 * @return true if statistics were successfully loaded, false otherwise
 */
bool getBookStats(const char* bookPath, BookReadingStats& stats);

/**
 * Loads global reading statistics from the statistics file.
 * 
 * @param stats Reference to populate with global statistics
 * @return true if statistics were successfully loaded, false otherwise
 */
bool loadGlobalStats(GlobalReadingStats& stats);

/**
 * Saves global reading statistics to the statistics file.
 * 
 * @param stats The global statistics to persist
 */
void saveGlobalStats(const GlobalReadingStats& stats);

/**
 * Recomputes global totals by scanning all per-book statistics files.
 */
GlobalReadingStats generateGlobalStats();