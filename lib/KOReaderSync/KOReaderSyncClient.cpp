/**
 * @file KOReaderSyncClient.cpp
 * @brief Definitions for KOReaderSyncClient using native esp_http_client.
 */

#include "KOReaderSyncClient.h"

#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <base64.h>

#include <ctime>

#include "KOReaderCredentialStore.h"
#include "esp_http_client.h"

extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

namespace {

constexpr char DEVICE_NAME[] = "CrossPoint";
constexpr char DEVICE_ID[] = "crosspoint-reader";

bool isHttpsUrl(const std::string& url) { return url.rfind("https://", 0) == 0; }

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

int doRequest(const std::string& url, const std::string& method,
              const std::string* body, std::string* responseBody) {
  KoreaderCtx ctx = {responseBody};
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.event_handler = koreaderEventHandler;
  cfg.user_data = &ctx;
  cfg.timeout_ms = 15000;
  cfg.skip_cert_common_name_check = true;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.keep_alive_enable = false;
  cfg.buffer_size = 2048;
  cfg.buffer_size_tx = 1024;
  cfg.disable_auto_redirect = false;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    Serial.printf("[%lu] [KOSync] Failed to init HTTP client\n", millis());
    return -1;
  }

  esp_http_client_set_header(client, "Accept", "application/vnd.koreader.v1+json");
  esp_http_client_set_header(client, "x-auth-user", KOREADER_STORE.getUsername().c_str());
  esp_http_client_set_header(client, "x-auth-key", KOREADER_STORE.getMd5Password().c_str());

  std::string credentials = KOREADER_STORE.getUsername() + ":" + KOREADER_STORE.getPassword();
  String encoded = base64::encode(credentials.c_str());
  esp_http_client_set_header(client, "Authorization", ("Basic " + encoded).c_str());

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
  if (err != ESP_OK) {
    Serial.printf("[%lu] [KOSync] perform failed: %s\n", millis(), esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return -1;
  }

  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  return status;
}

}  // namespace

KOReaderSyncClient::Error KOReaderSyncClient::authenticate() {
  if (!KOREADER_STORE.hasCredentials()) {
    Serial.printf("[%lu] [KOSync] No credentials configured\n", millis());
    return NO_CREDENTIALS;
  }

  std::string url = KOREADER_STORE.getBaseUrl() + "/users/auth";
  Serial.printf("[%lu] [KOSync] Authenticating: %s\n", millis(), url.c_str());

  const int httpCode = doRequest(url, "GET", nullptr, nullptr);
  Serial.printf("[%lu] [KOSync] Auth response: %d\n", millis(), httpCode);

  if (httpCode == 200) {
    return OK;
  } else if (httpCode == 401) {
    return AUTH_FAILED;
  } else if (httpCode < 0) {
    return NETWORK_ERROR;
  }
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& documentHash,
                                                           KOReaderProgress& outProgress) {
  if (!KOREADER_STORE.hasCredentials()) {
    Serial.printf("[%lu] [KOSync] No credentials configured\n", millis());
    return NO_CREDENTIALS;
  }

  std::string url = KOREADER_STORE.getBaseUrl() + "/syncs/progress/" + documentHash;
  Serial.printf("[%lu] [KOSync] Getting progress: %s\n", millis(), url.c_str());

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
  if (!KOREADER_STORE.hasCredentials()) {
    Serial.printf("[%lu] [KOSync] No credentials configured\n", millis());
    return NO_CREDENTIALS;
  }

  std::string url = KOREADER_STORE.getBaseUrl() + "/syncs/progress";
  Serial.printf("[%lu] [KOSync] Updating progress: %s\n", millis(), url.c_str());

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
      return "No progress found";
    default:
      return "Unknown error";
  }
}
