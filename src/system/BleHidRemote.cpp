/**
 * @file BleHidRemote.cpp
 * @brief BLE HID central: scan, connect, subscribe to HID reports (NimBLE).
 */

#include "BleHidRemote.h"

#include <NimBLEDevice.h>

#include <cstring>
#include <unordered_set>

static int g_stackUsers = 0;

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
  if (g_stackUsers++ == 0) {
    if (!NimBLEDevice::init("InxBLE")) {
      g_stackUsers = 0;
      return false;
    }
    NimBLEDevice::setPower(3);
    NimBLEDevice::setSecurityAuth(true, false, true);
  }
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

void bleRemoteMatchReport(const uint8_t* data, const size_t len, const BleRemoteBindings& bindings, bool& outPrev,
                          bool& outNext) {
  outPrev = false;
  outNext = false;
  // Prefer "next" when both patterns match (shared HID prefix); otherwise prev wins in the reader loop.
  if (bindings.nextLen > 0 && reportMatchesPrefix(data, len, bindings.nextPat, bindings.nextLen)) {
    outNext = true;
  } else if (bindings.prevLen > 0 && reportMatchesPrefix(data, len, bindings.prevPat, bindings.prevLen)) {
    outPrev = true;
  }
}
