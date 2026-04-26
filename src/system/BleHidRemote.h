#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "state/BleRemoteStore.h"

struct BleScannedRow {
  std::string name;
  std::string addrHex;
};

/**
 * NimBLE init refcount; call Release when leaving reader or setup.
 * For zero BLE RAM/flash use PlatformIO env `no_ble_remote` (INX_BLE_REMOTE=0, NimBLE not linked).
 */
bool bleRemoteStackAcquire();
void bleRemoteStackRelease();

bool bleRemoteScanForDevices(uint32_t durationMs, std::vector<BleScannedRow>& out);

using BleRemoteReportHandler = std::function<void(const uint8_t*, size_t len)>;

/** Returns opaque client pointer for disconnect, or nullptr on failure. */
void* bleRemoteConnectHid(const std::string& addrHex, const BleRemoteReportHandler& onReport, std::string& errMsg);

void bleRemoteDisconnectClient(void* clientOpaque);

/** Map one HID notification using learned prefixes (reader thread / loop). */
void bleRemoteMatchReport(const uint8_t* data, size_t len, const BleRemoteBindings& bindings, bool& outPrev,
                          bool& outNext);

/** Thread-safe pop of one HID report pushed by the BLE stack (reader mode). */
bool bleRemotePopReport(uint8_t out[16], size_t& outLen);
