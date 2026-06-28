#pragma once

#ifdef SIMULATOR

#include <cstddef>

namespace firmware_flash {

enum class Result {
  OK = 0,
  OPEN_FAIL,
  TOO_SMALL,
  TOO_LARGE,
  BAD_MAGIC,
  BAD_SEGMENTS,
  BAD_CHECKSUM,
  BAD_SHA,
  BAD_SIZE,
  NO_PARTITION,
  OOM,
  READ_FAIL,
  ERASE_FAIL,
  WRITE_FAIL,
  OTADATA_FAIL,
};

using ProgressCb = void (*)(size_t current, size_t total, void* ctx);

Result flashFromSdPath(const char* path, ProgressCb onProgress, void* ctx, bool validateOnly);
Result validateImageFile(const char* path, size_t maxBytes);
const char* resultName(Result result);

}  // namespace firmware_flash

#else
#error "src/simulator/network/FirmwareFlasher.h is only for simulator builds"
#endif
