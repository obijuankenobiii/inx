#include "state/BleDeviceStore.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "state/SystemSetting.h"

namespace {
constexpr char kBleDevicesFile[] = "/.system/ble_devices.bin";
constexpr uint8_t kLegacyFileVersion = 1;

uint8_t sanitizeAddrType(uint8_t t) {
  if (t > 3) {
    return 1;
  }
  return t;
}
}  // namespace

constexpr uint8_t BleDeviceStore::kFileVersion;
constexpr size_t BleDeviceStore::kMaxDevices;

void BleDeviceStore::toLowerInPlace(std::string& s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
}

std::string BleDeviceStore::addressKey(const std::string& address) {
  std::string k = address;
  toLowerInPlace(k);
  return k;
}

BleDeviceStore BleDeviceStore::instance;

bool BleDeviceStore::isDisplayableName(const std::string& name) {
  size_t start = 0;
  while (start < name.size() && std::isspace(static_cast<unsigned char>(name[start]))) {
    ++start;
  }
  size_t end = name.size();
  while (end > start && std::isspace(static_cast<unsigned char>(name[end - 1]))) {
    --end;
  }
  if (start >= end) {
    return false;
  }
  std::string trimmed = name.substr(start, end - start);
  std::string lower = trimmed;
  toLowerInPlace(lower);
  if (lower == "unknown") {
    return false;
  }
  return true;
}

bool BleDeviceStore::loadFromFile() {
  m_devices.clear();

  FsFile inputFile;
  if (SdMan.openFileForRead("BDS", kBleDevicesFile, inputFile)) {
    uint8_t version = 0;
    serialization::readPod(inputFile, version);
    if (version == kFileVersion || version == kLegacyFileVersion) {
      uint8_t count = 0;
      serialization::readPod(inputFile, count);
      if (count > kMaxDevices) {
        count = static_cast<uint8_t>(kMaxDevices);
      }
      for (uint8_t i = 0; i < count; ++i) {
        std::string addr;
        std::string nm;
        serialization::readString(inputFile, addr);
        serialization::readString(inputFile, nm);
        uint8_t typ = 1;
        if (version == kFileVersion) {
          serialization::readPod(inputFile, typ);
          typ = sanitizeAddrType(typ);
        }
        if (!addr.empty() && isDisplayableName(nm)) {
          m_devices.emplace_back(std::move(addr), std::move(nm), typ);
        }
      }
    }
    inputFile.close();
  }

  if (m_devices.empty() && SETTINGS.bleSavedAddress[0] != '\0' && isDisplayableName(std::string(SETTINGS.bleSavedName))) {
    m_devices.emplace_back(std::string(SETTINGS.bleSavedAddress), std::string(SETTINGS.bleSavedName), 1);
    saveToFile();
  }

  Serial.printf("[%lu] [BDS] Loaded %zu BLE device(s)\n", millis(), m_devices.size());
  return true;
}

bool BleDeviceStore::saveToFile() {
  SdMan.mkdir("/.system");
  FsFile outputFile;
  if (!SdMan.openFileForWrite("BDS", kBleDevicesFile, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, kFileVersion);
  const uint8_t count = static_cast<uint8_t>(std::min(m_devices.size(), kMaxDevices));
  serialization::writePod(outputFile, count);
  for (uint8_t i = 0; i < count; ++i) {
    serialization::writeString(outputFile, m_devices[i].address);
    serialization::writeString(outputFile, m_devices[i].name);
    serialization::writePod(outputFile, sanitizeAddrType(m_devices[i].addrType));
  }
  outputFile.close();
  Serial.printf("[%lu] [BDS] Saved %u BLE device(s)\n", millis(), count);
  return true;
}

bool BleDeviceStore::addOrUpdate(const std::string& address, const std::string& name, uint8_t addrType) {
  if (address.empty() || !isDisplayableName(name)) {
    return false;
  }
  const uint8_t typ = sanitizeAddrType(addrType);
  const std::string key = addressKey(address);
  for (auto& d : m_devices) {
    if (addressKey(d.address) == key) {
      const bool nameChg = (d.name != name);
      const bool typChg = (d.addrType != typ);
      if (nameChg || typChg) {
        d.name = name;
        d.addrType = typ;
        saveToFile();
        return true;
      }
      return false;
    }
  }
  if (m_devices.size() >= kMaxDevices) {
    m_devices.erase(m_devices.begin());
  }
  m_devices.emplace_back(address, name, typ);
  saveToFile();
  return true;
}

void BleDeviceStore::removeAt(size_t index) {
  if (index >= m_devices.size()) {
    return;
  }
  const std::string removedAddr = m_devices[index].address;
  m_devices.erase(m_devices.begin() + static_cast<std::ptrdiff_t>(index));
  if (addressKey(std::string(SETTINGS.bleSavedAddress)) == addressKey(removedAddr)) {
    SETTINGS.bleSavedAddress[0] = '\0';
    SETTINGS.bleSavedName[0] = '\0';
    SETTINGS.saveToFile();
  }
  saveToFile();
}

void BleDeviceStore::applyPreferred(const std::string& address, const std::string& name, uint8_t addrType) {
  if (address.empty()) {
    return;
  }
  strncpy(SETTINGS.bleSavedAddress, address.c_str(), sizeof(SETTINGS.bleSavedAddress) - 1);
  SETTINGS.bleSavedAddress[sizeof(SETTINGS.bleSavedAddress) - 1] = '\0';
  if (isDisplayableName(name)) {
    strncpy(SETTINGS.bleSavedName, name.c_str(), sizeof(SETTINGS.bleSavedName) - 1);
    SETTINGS.bleSavedName[sizeof(SETTINGS.bleSavedName) - 1] = '\0';
  } else {
    SETTINGS.bleSavedName[0] = '\0';
  }
  SETTINGS.saveToFile();
  if (isDisplayableName(name)) {
    addOrUpdate(address, name, addrType);
  }
}

int BleDeviceStore::findIndexByAddress(const std::string& address) const {
  const std::string key = addressKey(address);
  for (size_t i = 0; i < m_devices.size(); ++i) {
    if (addressKey(m_devices[i].address) == key) {
      return static_cast<int>(i);
    }
  }
  return -1;
}
