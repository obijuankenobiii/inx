#include "OtaUpdater.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <new>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"

namespace {
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/obijuankenobiii/inx/releases";

/* Pre-TLS body slab; smaller first so esp_http_client_init + TLS have headroom in internal heap. */
constexpr size_t kReleaseJsonPreallocSizes[] = {4096, 6144, 8192, 10240, 12288};

constexpr int kGithubCheckTaskStack = 16384;
constexpr int kGithubCheckTaskPrio = 3;

/* Response body buffer for latestReleaseUrl (GitHub uses chunked encoding). */
char* local_buf = nullptr;
int output_len = 0;
size_t local_buf_cap = 0;

/*
 * When esp_crt_bundle.h included, it is pointing wrong header file
 * which is something under WifiClientSecure because of our framework based on arduno platform.
 * To manage this obstacle, don't include anything, just extern and it will point correct one.
 */
extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

esp_err_t http_client_set_header_cb(esp_http_client_handle_t http_client) {
  return esp_http_client_set_header(http_client, "User-Agent", "Inx-ESP32-" INX_VERSION);
}

esp_err_t event_handler(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
  if (event->data == nullptr || event->data_len <= 0) return ESP_OK;

  const int chunk = event->data_len;
  const size_t need = static_cast<size_t>(output_len) + static_cast<size_t>(chunk) + 1;

  if (local_buf == nullptr) {
    const int content_len = esp_http_client_get_content_length(event->client);
    const bool chunked = esp_http_client_is_chunked_response(event->client);
    if (!chunked && content_len > 0) {
      local_buf_cap = static_cast<size_t>(content_len) + 1;
      local_buf = static_cast<char*>(calloc(local_buf_cap, 1));
    } else {
      /* Chunked or unknown length: do not preallocate 8K — right after Wi‑Fi/TLS, heap is
       * often fragmented; a large first calloc fails while smaller blocks succeed. */
      local_buf_cap = need;
      local_buf = static_cast<char*>(calloc(local_buf_cap, 1));
    }
    if (local_buf == nullptr) {
      Serial.printf("[%lu] [OTA] HTTP body buffer alloc failed (cap %u for need %u)\n", millis(),
                    static_cast<unsigned>(local_buf_cap), static_cast<unsigned>(need));
      return ESP_ERR_NO_MEM;
    }
    output_len = 0;
  }

  if (need > local_buf_cap) {
    /* Grow to exact `need` only. Doubling (e.g. max(need, cap*2)) asks for more RAM than the JSON
     * needs; under Wi‑Fi/TLS that extra contiguous block often fails while `need` still fits. */
    const size_t ncap = need;
    char* nb = static_cast<char*>(realloc(local_buf, ncap));
    if (nb == nullptr) {
      Serial.printf("[%lu] [OTA] HTTP body buffer realloc failed (cap %u need %u free %u largest %u)\n", millis(),
                    static_cast<unsigned>(ncap), static_cast<unsigned>(need),
                    static_cast<unsigned>(ESP.getFreeHeap()),
                    static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
      return ESP_ERR_NO_MEM;
    }
    local_buf = nb;
    local_buf_cap = ncap;
  }

  memcpy(local_buf + output_len, event->data, static_cast<size_t>(chunk));
  output_len += chunk;
  local_buf[output_len] = '\0';

  if (output_len % 4096 < chunk) {
    esp_task_wdt_reset();
  }

  return ESP_OK;
} /* event_handler */
} /* namespace */

struct OtaGithubCheckCtx {
  OtaUpdater* updater;
  OtaUpdater::OtaUpdaterError result;
  SemaphoreHandle_t done;
};

void otaGithubCheckTask(void* param) {
  auto* ctx = static_cast<OtaGithubCheckCtx*>(param);
  ctx->result = ctx->updater->checkForUpdateWorker();
  xSemaphoreGive(ctx->done);
  vTaskDelete(nullptr);
}

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  SemaphoreHandle_t done = xSemaphoreCreateBinary();
  if (done == nullptr) {
    return OOM_ERROR;
  }

  auto* ctx = new (std::nothrow) OtaGithubCheckCtx{this, INTERNAL_UPDATE_ERROR, done};
  if (ctx == nullptr) {
    vSemaphoreDelete(done);
    return OOM_ERROR;
  }

  if (xTaskCreate(otaGithubCheckTask, "otaGhChk", kGithubCheckTaskStack, ctx, kGithubCheckTaskPrio, nullptr) !=
      pdPASS) {
    delete ctx;
    vSemaphoreDelete(done);
    Serial.printf("[%lu] [OTA] Failed to spawn GitHub check task (stack %d)\n", millis(), kGithubCheckTaskStack);
    return OOM_ERROR;
  }

  while (xSemaphoreTake(done, pdMS_TO_TICKS(500)) != pdTRUE) {
    esp_task_wdt_reset();
  }

  const OtaUpdaterError out = ctx->result;
  delete ctx;
  vSemaphoreDelete(done);
  return out;
}

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdateWorker() {
  JsonDocument filter;
  esp_err_t esp_err;
  JsonDocument doc;

  /* Smaller TLS/HTTP buffers than install path: GitHub JSON check runs when heap is
   * tight (e.g. right after WiFi + display); large buffers make mbedtls_ssl_setup fail
   * with MBEDTLS_ERR_SSL_ALLOC_FAILED (-0x7F00). */
  esp_http_client_config_t client_config = {};
  client_config.url = latestReleaseUrl;
  client_config.event_handler = event_handler;
  client_config.buffer_size = 3072;
  client_config.buffer_size_tx = 1536;
  client_config.timeout_ms = 25000;
  client_config.skip_cert_common_name_check = true;
  client_config.crt_bundle_attach = esp_crt_bundle_attach;
  client_config.keep_alive_enable = false;

  /* Reset body buffer state (in case a prior check aborted). */
  if (local_buf != nullptr) {
    free(local_buf);
    local_buf = nullptr;
  }
  output_len = 0;
  local_buf_cap = 0;

  /* To track life time of local_buf, dtor will be called on exit from that function */
  struct localBufCleaner {
    char** bufPtr;
    ~localBufCleaner() {
      if (*bufPtr) {
        free(*bufPtr);
        *bufPtr = nullptr;
      }
      output_len = 0;
      local_buf_cap = 0;
    }
  } localBufCleaner = {&local_buf};

  esp_http_client_handle_t client_handle = esp_http_client_init(&client_config);
  if (!client_handle) {
    Serial.printf("[%lu] [OTA] HTTP Client Handle Failed\n", millis());
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_set_header(client_handle, "User-Agent", "Inx-ESP32-" INX_VERSION);
  if (esp_err != ESP_OK) {
    Serial.printf("[%lu] [OTA] esp_http_client_set_header Failed : %s\n", millis(), esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  /* Reserve body after client init; lets init use heap first, then slab before TLS in perform(). */
  for (const size_t tryBytes : kReleaseJsonPreallocSizes) {
    void* p = calloc(tryBytes, 1);
    if (p != nullptr) {
      local_buf = static_cast<char*>(p);
      local_buf_cap = tryBytes;
      Serial.printf("[%lu] [OTA] HTTP body prealloc %u bytes (free %u largest %u)\n", millis(),
                    static_cast<unsigned>(tryBytes), static_cast<unsigned>(ESP.getFreeHeap()),
                    static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
      break;
    }
  }

  /* Let EPD / Wi-Fi driver finish in-flight work; mbedtls_ssl_setup was failing when perform() ran
   * immediately after display flush on the same CPU. */
  vTaskDelay(pdMS_TO_TICKS(200));
  esp_task_wdt_reset();

  esp_err = esp_http_client_perform(client_handle);

  esp_task_wdt_reset();
  if (esp_err != ESP_OK) {
    Serial.printf("[%lu] [OTA] esp_http_client_perform Failed : %s\n", millis(), esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return HTTP_ERROR;
  }

  /* esp_http_client_close will be called inside cleanup as well*/
  esp_err = esp_http_client_cleanup(client_handle);
  if (esp_err != ESP_OK) {
    Serial.printf("[%lu] [OTA] esp_http_client_cleanupp Failed : %s\n", millis(), esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  filter["assets"][0]["size"] = true;
  if (local_buf == nullptr || output_len <= 0) {
    Serial.printf("[%lu] [OTA] Empty HTTP body (len=%d)\n", millis(), output_len);
    return HTTP_ERROR;
  }

  const DeserializationError error = deserializeJson(doc, local_buf, DeserializationOption::Filter(filter));
  if (error) {
    Serial.printf("[%lu] [OTA] JSON parse failed: %s\n", millis(), error.c_str());
    return JSON_PARSE_ERROR;
  }

  if (!doc["tag_name"].is<std::string>()) {
    Serial.printf("[%lu] [OTA] No tag_name found\n", millis());
    return JSON_PARSE_ERROR;
  }

  if (!doc["assets"].is<JsonArray>()) {
    Serial.printf("[%lu] [OTA] No assets found\n", millis());
    return JSON_PARSE_ERROR;
  }

  latestVersion = doc["tag_name"].as<std::string>();

  for (int i = 0; i < doc["assets"].size(); i++) {
    if (doc["assets"][i]["name"] == "firmware.bin") {
      otaUrl = doc["assets"][i]["browser_download_url"].as<std::string>();
      otaSize = doc["assets"][i]["size"].as<size_t>();
      totalSize = otaSize;
      updateAvailable = true;
      break;
    }
  }

  if (!updateAvailable) {
    Serial.printf("[%lu] [OTA] No firmware.bin asset found\n", millis());
    return NO_UPDATE;
  }

  Serial.printf("[%lu] [OTA] Found update: %s\n", millis(), latestVersion.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == INX_VERSION) {
    return false;
  }

  int currentMajor, currentMinor, currentPatch;
  int latestMajor, latestMinor, latestPatch;

  const auto currentVersion = INX_VERSION;

  // semantic version check (only match on 3 segments)
  sscanf(latestVersion.c_str(), "%d.%d.%d", &latestMajor, &latestMinor, &latestPatch);
  sscanf(currentVersion, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);

  /*
   * Compare major versions.
   * If they differ, return true if latest major version greater than current major version
   * otherwise return false.
   */
  if (latestMajor != currentMajor) return latestMajor > currentMajor;

  /*
   * Compare minor versions.
   * If they differ, return true if latest minor version greater than current minor version
   * otherwise return false.
   */
  if (latestMinor != currentMinor) return latestMinor > currentMinor;

  /*
   * Check patch versions.
   */
  return latestPatch > currentPatch;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate() {
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  esp_https_ota_handle_t ota_handle = NULL;
  esp_err_t esp_err;
  /* Signal for OtaUpdateActivity */
  render = false;

  esp_http_client_config_t client_config = {};
  client_config.url = otaUrl.c_str();
  client_config.timeout_ms = 15000;
  client_config.buffer_size = 8192;
  client_config.buffer_size_tx = 8192;
  client_config.skip_cert_common_name_check = true;
  client_config.crt_bundle_attach = esp_crt_bundle_attach;
  client_config.keep_alive_enable = true;

  esp_https_ota_config_t ota_config = {};
  ota_config.http_config = &client_config;
  ota_config.http_client_init_cb = http_client_set_header_cb;

  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);

  esp_err = esp_https_ota_begin(&ota_config, &ota_handle);
  if (esp_err != ESP_OK) {
    Serial.printf("[%lu] [OTA] HTTP OTA Begin Failed: %s\n", millis(), esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  do {
    esp_err = esp_https_ota_perform(ota_handle);
    processedSize = esp_https_ota_get_image_len_read(ota_handle);
    /* Sent signal to  OtaUpdateActivity */
    render = true;
    vTaskDelay(10 / portTICK_PERIOD_MS);
  } while (esp_err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

  /* Return back to default power saving for WiFi in case of failing */
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (esp_err != ESP_OK) {
    Serial.printf("[%lu] [OTA] esp_https_ota_perform Failed: %s\n", millis(), esp_err_to_name(esp_err));
    esp_https_ota_finish(ota_handle);
    return HTTP_ERROR;
  }

  if (!esp_https_ota_is_complete_data_received(ota_handle)) {
    Serial.printf("[%lu] [OTA] esp_https_ota_is_complete_data_received Failed: %s\n", millis(),
                  esp_err_to_name(esp_err));
    esp_https_ota_finish(ota_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_https_ota_finish(ota_handle);
  if (esp_err != ESP_OK) {
    Serial.printf("[%lu] [OTA] esp_https_ota_finish Failed: %s\n", millis(), esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  Serial.printf("[%lu] [OTA] Update completed\n", millis());
  return OK;
}
