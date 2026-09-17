#pragma once

/**
 * @file Serialization.h
 * @brief Public interface and types for Serialization.
 */

#include <SdFat.h>

#include <cstdint>
#include <string>

namespace serialization {
template <typename T>
inline void writePod(FsFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
inline void readPod(FsFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

inline void writeString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

// Generous upper bound on any single serialized string field (word text, image/footnote paths, HTML
// fragments) - real values are always well under 1KB. A corrupted/misaligned read can hand `len` a
// garbage 32-bit value; without this check, `s.resize(len)` throws std::bad_alloc/length_error on this
// ~320KB device, which is never caught anywhere in this codebase and takes down the whole app. Treat an
// out-of-range length as a corrupted read and fail closed (empty string) instead of crashing.
constexpr uint32_t kMaxSerializedStringLen = 1u << 20;  // 1MB

inline void readString(FsFile& file, std::string& s) {
  uint32_t len;
  readPod(file, len);
  if (len > kMaxSerializedStringLen) {
    s.clear();
    return;
  }
  s.resize(len);
  file.read(&s[0], len);
}
}  // namespace serialization
