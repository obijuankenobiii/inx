#ifndef BOOK_PROGRESS_H
#define BOOK_PROGRESS_H

#include <string>
#include <cstdint>

class BookProgress {
public:
    struct Data {
        uint16_t spineIndex = 0;
        uint16_t pageNumber = 0;
        uint16_t chapterPageCount = 0;
        uint32_t lastReadTimestamp = 0;
        float progressPercent = 0.0f;
    };

    explicit BookProgress(const std::string& cachePath);
    
    bool load(Data& data) const;
    bool save(const Data& data) const;
    bool exists() const;
    bool remove();
    bool validate(const Data& data, int totalSpines) const;
    void sanitize(Data& data, int totalSpines) const;

private:
    std::string filePath;
};

#endif