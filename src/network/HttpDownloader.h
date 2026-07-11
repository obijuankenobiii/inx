#pragma once

/**
 * @file HttpDownloader.h
 * @brief Public interface and types for HttpDownloader.
 */

#include <SDCardManager.h>

#include <functional>
#include <string>

/**
 * HTTP client utility for fetching content and downloading files.
 * Uses the native ESP-IDF esp_http_client for HTTPS connections
 * with certificate bundle verification (skip CN check).
 */
class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
  };

  /**
   * Fetch text content from a URL.
   * @param url The URL to fetch
   * @param outContent The fetched content (output)
   * @return true if fetch succeeded, false on error
   */
  static bool fetchUrl(const std::string& url, std::string& outContent);

  /** Fetch content from a URL into a stream, using the configured OPDS credentials. */
  static bool fetchUrl(const std::string& url, Stream& stream);

  /** Fetch text content from a URL using the given basic-auth credentials. */
  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                       const std::string& password);

  /** Fetch content from a URL into a stream using the given basic-auth credentials. */
  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username,
                       const std::string& password);

  /**
   * Download a file to the SD card.
   * @param url The URL to download
   * @param destPath The destination path on SD card
   * @param progress Optional progress callback
   * @return DownloadError indicating success or failure type
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr);

  /**
   * Download a file to the SD card using the given basic-auth credentials.
   * @param url The URL to download
   * @param destPath The destination path on SD card
   * @param username Basic-auth username
   * @param password Basic-auth password
   * @param progress Optional progress callback
   * @return DownloadError indicating success or failure type
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath, const std::string& username,
                                      const std::string& password, ProgressCallback progress = nullptr);

 private:
  static constexpr size_t DOWNLOAD_CHUNK_SIZE = 1024;
};
