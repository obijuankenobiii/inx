#include "WifiPower.h"

#include <Arduino.h>
#include <WiFi.h>

namespace WifiPower {

void off() {
  WiFi.scanDelete();
  WiFi.disconnect(false);
  delay(30);
  WiFi.mode(WIFI_OFF);
  delay(30);
}

}  // namespace WifiPower
