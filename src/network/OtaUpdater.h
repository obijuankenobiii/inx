#pragma once

/**
 * @file OtaUpdater.h
 * @brief Public interface and types for OtaUpdater.
 */

#include <functional>
#include <string>

class OtaUpdater {
  bool updateAvailable = false;
  std::string latestVersion;
  std::string otaUrl;
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;
  bool render = false;

 public:
  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
  };

 private:
  /** Task entry point that runs checkForUpdateWorker() on a background FreeRTOS task. */
  friend void otaGithubCheckTask(void* param);
  /** Query the GitHub releases API and record whether a firmware update is available. */
  OtaUpdaterError checkForUpdateWorker();

 public:
  /** Return the size in bytes of the available OTA update asset. */
  size_t getOtaSize() const { return otaSize; }

  /** Return the number of bytes processed so far during an in-progress update. */
  size_t getProcessedSize() const { return processedSize; }

  /** Return the total size in bytes of the update currently being installed. */
  size_t getTotalSize() const { return totalSize; }

  /** Return whether progress should currently be rendered to the screen. */
  bool getRender() const { return render; }

  /** Construct an OtaUpdater with no update state. */
  OtaUpdater() = default;
  /** Check whether the latest known release version is newer than the running firmware. */
  bool isUpdateNewer() const;
  /** Return the version string of the latest release found. */
  const std::string& getLatestVersion() const;
  /** Check GitHub for the latest release, running the request on a background task. */
  OtaUpdaterError checkForUpdate();
  /** Download and install the latest update over HTTPS. */
  OtaUpdaterError installUpdate();
  /** Install a firmware image read from the SD card. */
  OtaUpdaterError installUpdateFromSd(const char* firmwarePath = "/firmware.bin");
};
