#ifdef INX_SIMULATOR_WEB_ONLY

#include <Arduino.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <WiFi.h>

#include "network/LocalServer.h"
#include "state/NetworkCredential.h"
#include "state/SystemSetting.h"
#include "KOReaderCredentialStore.h"

namespace {
LocalServer server;
}

GfxRenderer renderer;

void setup() {
  Serial.begin(115200);

  SdMan.begin();
  SETTINGS.loadFromFile();
  WIFI_STORE.loadFromFile();
  KOREADER_STORE.loadFromFile();

  WiFi.mode(WIFI_STA);
  WiFi.begin("Simulator WiFi (fake)");

  server.begin();
  Serial.printf("[%lu] [SIM] Inx web dashboard simulator ready at http://127.0.0.1:8080/settings\n", millis());
}

void loop() {
  server.handleClient();
  delay(10);
}

#endif
