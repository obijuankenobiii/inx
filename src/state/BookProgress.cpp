#include "BookProgress.h"
#include <SDCardManager.h>
#include <cstring>

BookProgress::BookProgress(const std::string& cachePath) 
    : filePath(cachePath + "/progress.bin") {
}

bool BookProgress::load(Data& data) const {
    FsFile f;
    if (!SdMan.openFileForRead("BPR", filePath.c_str(), f)) {
        return false;
    }
    
    size_t fileSize = f.fileSize();
    if (fileSize != sizeof(Data)) {
        f.close();
        return false;
    }
    
    bool success = (f.read(&data, sizeof(Data)) == sizeof(Data));
    f.close();
    return success;
}

bool BookProgress::save(const Data& data) const {
    std::string dirPath = filePath.substr(0, filePath.find_last_of('/'));
    SdMan.mkdir(dirPath.c_str());
    
    FsFile f;
    if (!SdMan.openFileForWrite("BPR", filePath.c_str(), f)) {
        return false;
    }
    
    bool success = (f.write(&data, sizeof(Data)) == sizeof(Data));
    f.close();
    return success;
}

bool BookProgress::exists() const {
    return SdMan.exists(filePath.c_str());
}

bool BookProgress::remove() {
    if (exists()) {
        return SdMan.remove(filePath.c_str());
    }
    return true;
}

bool BookProgress::validate(const Data& data, int totalSpines) const {
    if (totalSpines <= 0) return false;
    return (data.spineIndex >= 0 && data.spineIndex < totalSpines);
}

void BookProgress::sanitize(Data& data, int totalSpines) const {
    if (totalSpines > 0) {
        if (data.spineIndex < 0 || data.spineIndex >= totalSpines) {
            data.spineIndex = 0;
            data.pageNumber = 0;
            data.chapterPageCount = 0;
        }
    }
}