#pragma once
// Native build stub — minimal Print for PngToBmpConverter and BitmapUtil.

#include <cstdint>
#include <vector>

class Print {
 public:
  std::vector<uint8_t> data;
  size_t write(uint8_t b) {
    data.push_back(b);
    return 1;
  }
  size_t write(const uint8_t* buf, size_t n) {
    data.insert(data.end(), buf, buf + n);
    return n;
  }
  // uint8_t is the canonical overload; accept any integer/char via template
  template <typename T>
  size_t write(T v) {
    return write(static_cast<uint8_t>(v));
  }
};
