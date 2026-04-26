/**
 * @file BleRemoteStore.cpp
 * @brief Persist one BLE HID remote (address + learned report prefixes).
 */

#include "BleRemoteStore.h"

#include <SDCardManager.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr const char* kPath = "/.system/ble_remote.bin";
constexpr uint32_t kMagic = 0x424C5231;  // 'BLR1'

struct FileLayout {
  uint32_t magic = 0;
  uint32_t version = 1;
  uint8_t addr[6]{};
  uint8_t hasAddr = 0;
  BleRemoteBindings bindings{};
};

}  // namespace

bool bleRemoteHasSaved() { return SdMan.exists(kPath); }

bool bleRemoteLoad(uint8_t outAddr[6], BleRemoteBindings& outBindings) {
  if (!outAddr) {
    return false;
  }
  FsFile f;
  if (!SdMan.openFileForRead("BLECFG", kPath, f)) {
    return false;
  }
  FileLayout file{};
  const size_t n = f.read(&file, sizeof(file));
  f.close();
  if (n != sizeof(file) || file.magic != kMagic || file.version != 1 || !file.hasAddr) {
    return false;
  }
  memcpy(outAddr, file.addr, 6);
  outBindings = file.bindings;
  return true;
}

void bleRemoteSave(const uint8_t addr[6], const BleRemoteBindings& bindings) {
  if (!addr) {
    return;
  }
  SdMan.mkdir("/.system");
  FileLayout file{};
  file.magic = kMagic;
  file.version = 1;
  file.hasAddr = 1;
  memcpy(file.addr, addr, 6);
  file.bindings = bindings;

  const std::string tmp = std::string(kPath) + ".tmp";
  FsFile out;
  if (!SdMan.openFileForWrite("BLECFG", tmp.c_str(), out)) {
    return;
  }
  const size_t w = out.write(&file, sizeof(file));
  out.close();
  if (w != sizeof(file)) {
    SdMan.remove(tmp.c_str());
    return;
  }
  SdMan.remove(kPath);
  SdMan.rename(tmp.c_str(), kPath);
}

void bleRemoteClear() { SdMan.remove(kPath); }

bool bleRemoteAddrBytesToAscii(const uint8_t addr[6], std::string& out) {
  if (!addr) {
    return false;
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
  out.assign(buf);
  return true;
}

bool bleRemoteParseAsciiAddr(const std::string& ascii, uint8_t outAddr[6]) {
  if (!outAddr) {
    return false;
  }
  int v[6] = {0};
  if (std::sscanf(ascii.c_str(), "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; ++i) {
    if (v[i] < 0 || v[i] > 255) {
      return false;
    }
    outAddr[i] = static_cast<uint8_t>(v[i]);
  }
  return true;
}
