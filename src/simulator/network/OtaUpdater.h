#pragma once

#ifdef SIMULATOR

#include <cstddef>
#include <string>

class OtaUpdater {
 public:
  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
    CANCELLED_ERROR,
  };

  using ProgressCallback = void (*)(void* ctx);

  size_t getOtaSize() const { return 0; }
  size_t getProcessedSize() const { return processedSize; }
  size_t getTotalSize() const { return totalSize; }
  bool getRender() const { return render; }

  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  OtaUpdaterError checkForUpdate();
  OtaUpdaterError installUpdate(ProgressCallback onProgress, void* ctx);
  OtaUpdaterError installUpdate() {
    processedSize = 1;
    totalSize = 1;
    return INTERNAL_UPDATE_ERROR;
  }
  OtaUpdaterError installUpdateFromSd(const char* firmwarePath = "/firmware.bin") {
    (void)firmwarePath;
    processedSize = 1;
    totalSize = 1;
    return INTERNAL_UPDATE_ERROR;
  }

 private:
  bool render = false;
  size_t processedSize = 0;
  size_t totalSize = 0;
};

#else
#error "src/simulator/network/OtaUpdater.h is only for simulator builds"
#endif
