/**
 * @file HttpDownloader.cpp
 * @brief Definitions for HttpDownloader.
 */

#include "HttpDownloader.h"

#include <HardwareSerial.h>
#include <StreamString.h>
#include <base64.h>

#include <cstring>
#include <memory>
#include <string>

#include "state/SystemSetting.h"
#include "util/UrlUtils.h"

#ifdef SIMULATOR

/** Fetch content from a URL into a stream using the given basic-auth credentials (simulator stub, always fails). */
bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  (void)url;
  (void)outContent;
  (void)username;
  (void)password;
  Serial.printf("[%lu] [HTTP] Simulator does not support HTTP requests\n", millis());
  return false;
}

#else

#include <esp_http_client.h>

extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

struct FetchCtx {
  Stream* stream;
};

struct DownloadCtx {
  FsFile* file;
  HttpDownloader::ProgressCallback progress;
  size_t downloaded;
  size_t total;
};

/** esp_http_client event handler that writes received body data to a FetchCtx's stream. */
static esp_err_t fetchEventHandler(esp_http_client_event_t* event) {
  if (event->event_id == HTTP_EVENT_ON_DATA && event->data && event->data_len > 0) {
    auto* ctx = static_cast<FetchCtx*>(event->user_data);
    if (ctx && ctx->stream) {
      ctx->stream->write(static_cast<const uint8_t*>(event->data), event->data_len);
    }
  }
  return ESP_OK;
}

/** esp_http_client event handler that tracks Content-Length and writes received body data to a DownloadCtx's file. */
static esp_err_t downloadEventHandler(esp_http_client_event_t* event) {
  if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value) {
    if (strcmp(event->header_key, "Content-Length") == 0) {
      auto* ctx = static_cast<DownloadCtx*>(event->user_data);
      if (ctx) {
        ctx->total = atol(event->header_value);
      }
    }
  }
  if (event->event_id == HTTP_EVENT_ON_DATA && event->data && event->data_len > 0) {
    auto* ctx = static_cast<DownloadCtx*>(event->user_data);
    if (ctx && ctx->file) {
      const size_t written = ctx->file->write(event->data, event->data_len);
      if (written != static_cast<size_t>(event->data_len)) {
        return ESP_FAIL;
      }
      ctx->downloaded += written;
      if (ctx->progress && ctx->total > 0) {
        ctx->progress(ctx->downloaded, ctx->total);
      }
    }
  }
  return ESP_OK;
}

/** Perform a blocking HTTPS GET of url and stream the response body through fetchCtx, using optional basic auth. */
static bool doFetch(const std::string& url, FetchCtx* fetchCtx, const std::string& username,
                    const std::string& password) {
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.event_handler = fetchEventHandler;
  cfg.user_data = fetchCtx;
  cfg.timeout_ms = 15000;
  cfg.skip_cert_common_name_check = true;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.keep_alive_enable = false;
  cfg.buffer_size = 2048;
  cfg.buffer_size_tx = 1024;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    Serial.printf("[%lu] [HTTP] Failed to init HTTP client\n", millis());
    return false;
  }

  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" INX_VERSION);

  if (!username.empty() && !password.empty()) {
    std::string credentials = username + ":" + password;
    String encoded = base64::encode(credentials.c_str());
    esp_http_client_set_header(client, "Authorization", ("Basic " + encoded).c_str());
  }

  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    Serial.printf("[%lu] [HTTP] perform failed: %s\n", millis(), esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }

  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    Serial.printf("[%lu] [HTTP] Server returned HTTP %d\n", millis(), status);
    esp_http_client_cleanup(client);
    return false;
  }

  esp_http_client_cleanup(client);
  return true;
}

/** Fetch content from a URL into a stream using the given basic-auth credentials. */
bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  Serial.printf("[%lu] [HTTP] Fetching: %s\n", millis(), url.c_str());
  FetchCtx ctx = {&outContent};
  return doFetch(url, &ctx, username, password);
}

#endif

/** Fetch content from a URL into a stream, using the configured OPDS credentials. */
bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent) {
  std::string user = SETTINGS.opdsUsername;
  std::string pass = SETTINGS.opdsPassword;
  return fetchUrl(url, outContent, user, pass);
}

/** Fetch text content from a URL, using the configured OPDS credentials. */
bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent) {
  std::string user = SETTINGS.opdsUsername;
  std::string pass = SETTINGS.opdsPassword;
  return fetchUrl(url, outContent, user, pass);
}

/** Fetch text content from a URL using the given basic-auth credentials. */
bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  StreamString stream;
  if (!fetchUrl(url, stream, username, password)) {
    return false;
  }
  outContent = stream.c_str();
  return true;
}

/** Download a file to the SD card, using the configured OPDS credentials. */
HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress) {
  std::string user = SETTINGS.opdsUsername;
  std::string pass = SETTINGS.opdsPassword;
  return downloadToFile(url, destPath, user, pass, progress);
}

/** Download a file to the SD card using the given basic-auth credentials. */
HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             const std::string& username, const std::string& password,
                                                             ProgressCallback progress) {
  Serial.printf("[%lu] [HTTP] Downloading: %s\n", millis(), url.c_str());
  Serial.printf("[%lu] [HTTP] Destination: %s\n", millis(), destPath.c_str());

  if (SdMan.exists(destPath.c_str())) {
    SdMan.remove(destPath.c_str());
  }

  FsFile file;
  if (!SdMan.openFileForWrite("HTTP", destPath.c_str(), file)) {
    Serial.printf("[%lu] [HTTP] Failed to open file for writing\n", millis());
    return FILE_ERROR;
  }

#ifdef SIMULATOR
  (void)username;
  (void)password;
  (void)progress;
  file.close();
  Serial.printf("[%lu] [HTTP] Simulator does not support HTTP downloads\n", millis());
  return HTTP_ERROR;

#else
  DownloadCtx dctx = {&file, progress, 0, 0};
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.event_handler = downloadEventHandler;
  cfg.user_data = &dctx;
  cfg.timeout_ms = 15000;
  cfg.skip_cert_common_name_check = true;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.keep_alive_enable = false;
  cfg.buffer_size = 2048;
  cfg.buffer_size_tx = 1024;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    Serial.printf("[%lu] [HTTP] Failed to init HTTP client\n", millis());
    file.close();
    SdMan.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" INX_VERSION);

  if (!username.empty() && !password.empty()) {
    std::string credentials = username + ":" + password;
    String encoded = base64::encode(credentials.c_str());
    esp_http_client_set_header(client, "Authorization", ("Basic " + encoded).c_str());
  }

  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    Serial.printf("[%lu] [HTTP] perform failed: %s\n", millis(), esp_err_to_name(err));
    esp_http_client_cleanup(client);
    file.close();
    SdMan.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    Serial.printf("[%lu] [HTTP] Download failed with HTTP %d\n", millis(), status);
    esp_http_client_cleanup(client);
    file.close();
    SdMan.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  file.close();
  esp_http_client_cleanup(client);

  Serial.printf("[%lu] [HTTP] Downloaded %zu bytes\n", millis(), dctx.downloaded);

  if (dctx.total > 0 && dctx.downloaded != dctx.total) {
    Serial.printf("[%lu] [HTTP] Size mismatch: got %zu, expected %zu\n", millis(), dctx.downloaded, dctx.total);
    SdMan.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  return OK;
#endif
}
