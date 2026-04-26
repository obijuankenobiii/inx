/**
 * @file BleHidRemote.cpp
 * @brief BLE HID central: scan, connect, subscribe to HID reports (NimBLE).
 *
 * Set INX_BLE_REMOTE=0 and use PlatformIO env `no_ble_remote` (lib_ignore NimBLE-Arduino)
 * for firmware with no Bluetooth stack in RAM/flash. Default is INX_BLE_REMOTE=1.
 */

#include "BleHidRemote.h"

#ifndef INX_BLE_REMOTE
#define INX_BLE_REMOTE 1
#endif

#include <cstring>

#if INX_BLE_REMOTE
#include <NimBLEDevice.h>
#endif

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if INX_BLE_REMOTE
#include <esp_heap_caps.h>
#endif
#endif

#if INX_BLE_REMOTE
#include <unordered_set>
#endif

#if INX_BLE_REMOTE
static int g_stackUsers = 0;
#endif

static portMUX_TYPE g_reportMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t g_reportBuf[16]{};
static size_t g_reportLen = 0;
static volatile bool g_reportPending = false;

static void pushReport(const uint8_t* data, size_t len) {
  if (!data || len == 0) {
    return;
  }
  portENTER_CRITICAL(&g_reportMux);
  g_reportLen = len > sizeof(g_reportBuf) ? sizeof(g_reportBuf) : len;
  memcpy(g_reportBuf, data, g_reportLen);
  g_reportPending = true;
  portEXIT_CRITICAL(&g_reportMux);
}

bool bleRemotePopReport(uint8_t out[16], size_t& outLen) {
  if (!out) {
    return false;
  }
  bool got = false;
  portENTER_CRITICAL(&g_reportMux);
  if (g_reportPending) {
    outLen = g_reportLen;
    memcpy(out, g_reportBuf, g_reportLen);
    g_reportPending = false;
    got = true;
  }
  portEXIT_CRITICAL(&g_reportMux);
  return got;
}

static bool reportMatchesPrefix(const uint8_t* data, size_t len, const uint8_t* pat, uint8_t patLen) {
  if (!data || patLen == 0 || patLen > sizeof(BleRemoteBindings::nextPat)) {
    return false;
  }
  if (len < patLen) {
    return false;
  }
  return memcmp(data, pat, patLen) == 0;
}

void bleRemoteMatchReport(const uint8_t* data, const size_t len, const BleRemoteBindings& bindings, bool& outPrev,
                          bool& outNext) {
  outPrev = false;
  outNext = false;
  if (bindings.nextLen > 0 && reportMatchesPrefix(data, len, bindings.nextPat, bindings.nextLen)) {
    outNext = true;
  } else if (bindings.prevLen > 0 && reportMatchesPrefix(data, len, bindings.prevPat, bindings.prevLen)) {
    outPrev = true;
  }
}

#if !INX_BLE_REMOTE

bool bleRemoteStackAcquire() { return false; }

void bleRemoteStackRelease() {}

bool bleRemoteScanForDevices(const uint32_t /*durationMs*/, std::vector<BleScannedRow>& out) {
  out.clear();
  return false;
}

void* bleRemoteConnectHid(const std::string&, const BleRemoteReportHandler&, std::string& errMsg) {
  errMsg = "Bluetooth remote not in this build (use default env for BLE)";
  return nullptr;
}

void bleRemoteDisconnectClient(void* /*clientOpaque*/) {}

#else

static bool subscribeHidNotifications(NimBLEClient* client, const BleRemoteReportHandler& onReport, std::string& err) {
  if (!client->discoverAttributes()) {
    err = "Discover failed";
    return false;
  }
  NimBLERemoteService* hid = client->getService(NimBLEUUID(static_cast<uint16_t>(0x1812)));
  if (!hid) {
    err = "No HID service";
    return false;
  }
  bool any = false;
  for (NimBLERemoteCharacteristic* chr : hid->getCharacteristics(true)) {
    if (chr->canNotify()) {
      if (chr->subscribe(true,
                         [onReport](NimBLERemoteCharacteristic*, uint8_t* pData, size_t length, bool /*isNotify*/) {
                           if (onReport && pData && length > 0) {
                             onReport(pData, length);
                           }
                         },
                         true)) {
        any = true;
      }
    }
  }
  if (!any) {
    err = "No notify chars";
    return false;
  }
  return true;
}

bool bleRemoteStackAcquire() {
  if (g_stackUsers > 0) {
    ++g_stackUsers;
    return true;
  }

#if defined(ARDUINO_ARCH_ESP32)
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t free8 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (largest < (28 * 1024) || free8 < (70 * 1024)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(20));
#endif

  if (!NimBLEDevice::init("InxBLE")) {
    return false;
  }
  NimBLEDevice::setPower(3);
  NimBLEDevice::setSecurityAuth(false, false, false);
  g_stackUsers = 1;
  return true;
}

void bleRemoteStackRelease() {
  if (g_stackUsers <= 0) {
    g_stackUsers = 0;
    return;
  }
  if (--g_stackUsers == 0) {
    NimBLEDevice::deinit(true);
  }
}

bool bleRemoteScanForDevices(const uint32_t durationMs, std::vector<BleScannedRow>& out) {
  out.clear();
  if (!NimBLEDevice::getScan()) {
    return false;
  }
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults results = scan->getResults(durationMs, false);
  std::unordered_set<std::string> seen;
  for (int i = 0; i < results.getCount(); ++i) {
    const NimBLEAdvertisedDevice* d = results.getDevice(static_cast<uint32_t>(i));
    if (!d) {
      continue;
    }
    const std::string key = d->getAddress().toString();
    if (seen.count(key) != 0) {
      continue;
    }
    seen.insert(key);
    BleScannedRow row;
    row.addrHex = key;
    row.name = d->haveName() ? std::string(d->getName().c_str()) : std::string("(no name)");
    out.push_back(std::move(row));
    if (out.size() >= 12) {
      break;
    }
  }
  scan->clearResults();
  return true;
}

void* bleRemoteConnectHid(const std::string& addrHex, const BleRemoteReportHandler& onReport, std::string& errMsg) {
  errMsg.clear();
  constexpr uint8_t kAddrPublic = 0;
  NimBLEAddress addr(addrHex, kAddrPublic);
  NimBLEClient* client = NimBLEDevice::createClient();
  if (!client) {
    errMsg = "No client";
    return nullptr;
  }
  if (!client->connect(addr, false, false, true)) {
    errMsg = "Connect failed";
    NimBLEDevice::deleteClient(client);
    return nullptr;
  }
  const BleRemoteReportHandler dispatch = onReport ? onReport : [](const uint8_t* p, size_t n) { pushReport(p, n); };
  if (!subscribeHidNotifications(client, dispatch, errMsg)) {
    client->disconnect();
    NimBLEDevice::deleteClient(client);
    return nullptr;
  }
  return client;
}

void bleRemoteDisconnectClient(void* clientOpaque) {
  auto* c = static_cast<NimBLEClient*>(clientOpaque);
  if (!c) {
    return;
  }
  c->disconnect();
  NimBLEDevice::deleteClient(c);
}

#endif
