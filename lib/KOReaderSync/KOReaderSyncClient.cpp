/**
 * @file KOReaderSyncClient.cpp
 * @brief Definitions for KOReaderSyncClient using native esp_http_client.
 */

#include "KOReaderSyncClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>

#include <ctime>

#include "KOReaderCredentialStore.h"
#ifndef SIMULATOR
#include "esp_http_client.h"

extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}
#endif

int KOReaderSyncClient::lastHttpCode = 0;

namespace {

constexpr char DEVICE_NAME[] = "CrossPoint";
constexpr char DEVICE_ID[] = "crosspoint-reader";
constexpr int HTTP_BUF_SIZE = 2048;
constexpr uint32_t MIN_HEAP_FOR_TLS = 55000;

#ifndef SIMULATOR

struct KoreaderCtx {
  std::string* responseBody;
};

esp_err_t koreaderEventHandler(esp_http_client_event_t* event) {
  if (event->event_id == HTTP_EVENT_ON_DATA && event->data && event->data_len > 0) {
    auto* ctx = static_cast<KoreaderCtx*>(event->user_data);
    if (ctx && ctx->responseBody) {
      ctx->responseBody->append(static_cast<const char*>(event->data), event->data_len);
    }
  }
  return ESP_OK;
}

int doRequest(const std::string& url, const std::string& method, const std::string* body, std::string* responseBody) {
  KoreaderCtx ctx = {responseBody};
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.event_handler = koreaderEventHandler;
  cfg.user_data = &ctx;
  cfg.timeout_ms = 15000;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.keep_alive_enable = false;
  cfg.buffer_size = HTTP_BUF_SIZE;
  cfg.buffer_size_tx = HTTP_BUF_SIZE;
  cfg.disable_auto_redirect = false;
  cfg.username = KOREADER_STORE.getUsername().c_str();
  cfg.password = KOREADER_STORE.getPassword().c_str();
  cfg.auth_type = HTTP_AUTH_TYPE_BASIC;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    Serial.printf("[%lu] [KOSync] Failed to init HTTP client\n", millis());
    return -1;
  }

  esp_http_client_set_header(client, "Accept", "application/vnd.koreader.v1+json");
  esp_http_client_set_header(client, "x-auth-user", KOREADER_STORE.getUsername().c_str());
  esp_http_client_set_header(client, "x-auth-key", KOREADER_STORE.getMd5Password().c_str());

  if (method == "POST") {
    esp_http_client_set_method(client, HTTP_METHOD_POST);
  } else if (method == "PUT") {
    esp_http_client_set_method(client, HTTP_METHOD_PUT);
  } else {
    esp_http_client_set_method(client, HTTP_METHOD_GET);
  }

  if (body) {
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body->c_str(), body->size());
  }

  esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  KOReaderSyncClient::lastHttpCode = status;
  if (err != ESP_OK) {
    Serial.printf("[%lu] [KOSync] perform failed: %s status=%d\n", millis(), esp_err_to_name(err), status);
    esp_http_client_cleanup(client);
    return -1;
  }

  esp_http_client_cleanup(client);
  return status;
}

#else

int doRequest(const std::string& url, const std::string& method, const std::string* body, std::string* responseBody) {
  (void)url;
  (void)method;
  (void)body;
  (void)responseBody;
  Serial.printf("[%lu] [KOSync] Simulator does not support KOReaderSync HTTP requests\n", millis());
  return -1;
}

#endif

}  // namespace

KOReaderSyncClient::Error KOReaderSyncClient::authenticate() {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    Serial.printf("[%lu] [KOSync] No credentials configured\n", millis());
    return NO_CREDENTIALS;
  }

  std::string url = KOREADER_STORE.getBaseUrl() + "/users/auth";
  const uint32_t freeHeap = ESP.getFreeHeap();
  Serial.printf("[%lu] [KOSync] Authenticating: %s heap=%u\n", millis(), url.c_str(), (unsigned)freeHeap);
  if (freeHeap < MIN_HEAP_FOR_TLS) {
    Serial.printf("[%lu] [KOSync] Insufficient heap for TLS: %u bytes free\n", millis(), (unsigned)freeHeap);
    return LOW_MEMORY;
  }

  const int httpCode = doRequest(url, "GET", nullptr, nullptr);
  Serial.printf("[%lu] [KOSync] Auth response: %d\n", millis(), httpCode);

  if (httpCode == 200) {
    return OK;
  } else if (httpCode == 401) {
    return AUTH_FAILED;
  } else if (httpCode == 404) {
    return NOT_FOUND;
  } else if (httpCode < 0) {
    return NETWORK_ERROR;
  }
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& documentHash,
                                                          KOReaderProgress& outProgress) {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    Serial.printf("[%lu] [KOSync] No credentials configured\n", millis());
    return NO_CREDENTIALS;
  }

  std::string url = KOREADER_STORE.getBaseUrl() + "/syncs/progress/" + documentHash;
  const uint32_t freeHeap = ESP.getFreeHeap();
  Serial.printf("[%lu] [KOSync] Getting progress: %s heap=%u\n", millis(), url.c_str(), (unsigned)freeHeap);
  if (freeHeap < MIN_HEAP_FOR_TLS) {
    Serial.printf("[%lu] [KOSync] Insufficient heap for TLS: %u bytes free\n", millis(), (unsigned)freeHeap);
    return LOW_MEMORY;
  }

  std::string responseBody;
  const int httpCode = doRequest(url, "GET", nullptr, &responseBody);

  if (httpCode == 200) {
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, responseBody);

    if (error) {
      Serial.printf("[%lu] [KOSync] JSON parse failed: %s\n", millis(), error.c_str());
      return JSON_ERROR;
    }

    outProgress.document = documentHash;
    outProgress.progress = doc["progress"].as<std::string>();
    outProgress.percentage = doc["percentage"].as<float>();
    outProgress.device = doc["device"].as<std::string>();
    outProgress.deviceId = doc["device_id"].as<std::string>();
    outProgress.timestamp = doc["timestamp"].as<int64_t>();

    Serial.printf("[%lu] [KOSync] Got progress: %.2f%% at %s\n", millis(), outProgress.percentage * 100,
                  outProgress.progress.c_str());
    return OK;
  }

  Serial.printf("[%lu] [KOSync] Get progress response: %d\n", millis(), httpCode);

  if (httpCode == 401) {
    return AUTH_FAILED;
  } else if (httpCode == 404) {
    return NOT_FOUND;
  } else if (httpCode < 0) {
    return NETWORK_ERROR;
  }
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::updateProgress(const KOReaderProgress& progress) {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    Serial.printf("[%lu] [KOSync] No credentials configured\n", millis());
    return NO_CREDENTIALS;
  }

  std::string url = KOREADER_STORE.getBaseUrl() + "/syncs/progress";
  const uint32_t freeHeap = ESP.getFreeHeap();
  Serial.printf("[%lu] [KOSync] Updating progress: %s heap=%u\n", millis(), url.c_str(), (unsigned)freeHeap);
  if (freeHeap < MIN_HEAP_FOR_TLS) {
    Serial.printf("[%lu] [KOSync] Insufficient heap for TLS: %u bytes free\n", millis(), (unsigned)freeHeap);
    return LOW_MEMORY;
  }

  JsonDocument doc;
  doc["document"] = progress.document;
  doc["progress"] = progress.progress;
  doc["percentage"] = progress.percentage;
  doc["device"] = DEVICE_NAME;
  doc["device_id"] = DEVICE_ID;

  std::string body;
  serializeJson(doc, body);

  Serial.printf("[%lu] [KOSync] Request body: %s\n", millis(), body.c_str());

  const int httpCode = doRequest(url, "PUT", &body, nullptr);
  Serial.printf("[%lu] [KOSync] Update progress response: %d\n", millis(), httpCode);

  if (httpCode == 200 || httpCode == 202) {
    return OK;
  } else if (httpCode == 401) {
    return AUTH_FAILED;
  } else if (httpCode == 404) {
    return NOT_FOUND;
  } else if (httpCode < 0) {
    return NETWORK_ERROR;
  }
  return SERVER_ERROR;
}

const char* KOReaderSyncClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NO_CREDENTIALS:
      return "No credentials configured";
    case NETWORK_ERROR:
      return "Network error";
    case AUTH_FAILED:
      return "Authentication failed";
    case SERVER_ERROR:
      return "Server error (try again later)";
    case JSON_ERROR:
      return "JSON parse error";
    case NOT_FOUND:
      return "No progress found (first time reading this book?)";
    case LOW_MEMORY:
      return "Not enough memory for sync - please retry";
    default:
      return "Unknown error";
  }
}
