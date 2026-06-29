#pragma once
// Native build stub — minimal FsFile for PngToBmpConverter and Bitmap.

#include <cstdint>
#include <cstring>

class FsFile {
 public:
  const uint8_t* buf_ = nullptr;
  size_t size_ = 0;
  size_t pos_ = 0;

  FsFile() = default;
  FsFile(const uint8_t* buf, size_t size) : buf_(buf), size_(size), pos_(0) {}

  int read(void* dst, size_t n) {
    if (!buf_) return 0;
    size_t avail = (pos_ < size_) ? (size_ - pos_) : 0;
    size_t toRead = (n < avail) ? n : avail;
    memcpy(dst, buf_ + pos_, toRead);
    pos_ += toRead;
    return static_cast<int>(toRead);
  }

  bool seekCur(int32_t offset) {
    int64_t newPos = static_cast<int64_t>(pos_) + offset;
    if (newPos < 0 || static_cast<size_t>(newPos) > size_) return false;
    pos_ = static_cast<size_t>(newPos);
    return true;
  }

  uint32_t position() const { return static_cast<uint32_t>(pos_); }

  bool seek(uint32_t pos) {
    if (pos > size_) return false;
    pos_ = pos;
    return true;
  }
};
