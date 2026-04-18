#include "BluetoothManager.h"

#include <NimBLEDevice.h>

#include <cstdio>

#define LOG(fmt, ...) printf("[BT] " fmt "\n", ##__VA_ARGS__)

static const char* HID_SERVICE = "1812";
static const uint16_t BLE_CONN_MIN_INTERVAL = 12;
static const uint16_t BLE_CONN_MAX_INTERVAL = 24;
static const uint16_t BLE_CONN_LATENCY = 0;
static const uint16_t BLE_CONN_TIMEOUT = 600;
static const uint16_t BLE_CONN_SCAN_INTERVAL = 60;
static const uint16_t BLE_CONN_SCAN_WINDOW = 30;
static const uint32_t BLE_CONNECT_TIMEOUT_MS = 15000;

BluetoothManager* BluetoothManager::s_instance = nullptr;

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    BluetoothManager* mgr = BluetoothManager::getInstancePtr();
    if (mgr) {
      mgr->onScanResult(const_cast<NimBLEAdvertisedDevice*>(advertisedDevice));
    }
  }
};

static ScanCallbacks scanCallbacks;

class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) override {
    LOG("Connected to %s", pClient->getPeerAddress().toString().c_str());
  }
  void onDisconnect(NimBLEClient* pClient, int reason) override {
    LOG("Disconnected from %s (reason: %d)", pClient->getPeerAddress().toString().c_str(), reason);
    if (BluetoothManager* mgr = BluetoothManager::getInstancePtr()) {
      mgr->handleClientDisconnected(pClient);
    }
  }
};

static ClientCallbacks clientCallbacks;

BluetoothManager& BluetoothManager::getInstance() {
  if (!s_instance) {
    s_instance = new BluetoothManager();
  }
  return *s_instance;
}

BluetoothManager* BluetoothManager::getInstancePtr() {
  return s_instance;
}

BluetoothManager::BluetoothManager() : m_enabled(false), m_scanning(false) {
  LOG("Manager created");
}

BluetoothManager::~BluetoothManager() {
  s_instance = nullptr;
}

bool BluetoothManager::isEnabled() const {
  return m_enabled;
}

bool BluetoothManager::isConnected(const std::string& address) const {
  const auto it = m_clientsByAddr.find(address);
  if (it == m_clientsByAddr.end() || it->second == nullptr) {
    return false;
  }
  return it->second->isConnected();
}

bool BluetoothManager::enable() {
  if (m_enabled) {
    return true;
  }

  LOG("Enabling Bluetooth...");
  NimBLEDevice::init("ESP32");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  m_enabled = true;
  LOG("Bluetooth enabled");
  return true;
}

bool BluetoothManager::disable() {
  if (!m_enabled) {
    return true;
  }

  LOG("Disabling Bluetooth...");
  if (m_scanning) {
    stopScan();
  }
  disconnectAll();
  NimBLEDevice::deinit(false);
  m_enabled = false;
  LOG("Bluetooth disabled");
  return true;
}

void BluetoothManager::startScan(uint32_t durationMs) {
  if (!m_enabled || m_scanning) {
    return;
  }

  LOG("Starting scan for %lu ms", durationMs);
  m_devices.clear();
  m_scanning = true;

  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCallbacks, false);
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);

  pScan->start(durationMs, false);

  m_scanning = false;
  LOG("Scan completed, found %d devices", (int)m_devices.size());
}

void BluetoothManager::stopScan() {
  if (!m_scanning) {
    return;
  }
  LOG("Stopping scan");
  NimBLEDevice::getScan()->stop();
  m_scanning = false;
}

void BluetoothManager::onScanResult(void* device) {
  NimBLEAdvertisedDevice* advDevice = (NimBLEAdvertisedDevice*)device;
  if (!advDevice) {
    return;
  }

  std::string address = advDevice->getAddress().toString();
  std::string name = advDevice->getName();
  if (name.empty()) {
    name = "Unknown";
  }
  int rssi = advDevice->getRSSI();
  bool isHID = advDevice->isAdvertisingService(NimBLEUUID(HID_SERVICE));

  for (const auto& d : m_devices) {
    if (d.address == address) {
      return;
    }
  }

  Device dev;
  dev.address = address;
  dev.name = name;
  dev.rssi = rssi;
  dev.isHID = isHID;
  m_devices.push_back(dev);

  LOG("Found: %s (%s) RSSI:%d HID:%d", name.c_str(), address.c_str(), rssi, isHID);
}

void BluetoothManager::trySubscribeHid(NimBLEClient* pClient) {
  if (!pClient->discoverAttributes()) {
    LOG("GATT discovery failed — HID input disabled for this session");
    return;
  }

  NimBLERemoteService* pSvc = pClient->getService(NimBLEUUID((uint16_t)0x1812));
  if (!pSvc) {
    LOG("HID service 0x1812 not found — BLE keyboard reports won't be received");
    return;
  }

  for (NimBLERemoteCharacteristic* c : pSvc->getCharacteristics(true)) {
    if (c == nullptr || !c->canNotify()) {
      continue;
    }
    if (c->subscribe(true,
                     [](NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool /*isNotify*/) {
                       if (BluetoothManager* m = BluetoothManager::getInstancePtr()) {
                         m->onHidReport(data, length);
                       }
                     },
                     true)) {
      LOG("HID notify subscribed (%s)", c->getUUID().toString().c_str());
      return;
    }
  }
  LOG("No notifiable HID characteristic found");
}

void BluetoothManager::onHidReport(const uint8_t* data, size_t len) {
  if (!m_bleButtonSink || data == nullptr || len < 2) {
    return;
  }

  const uint8_t* boot = data;
  size_t blen = len;
  if (len == 9) {
    boot = data + 1;
    blen = 8;
  }
  if (blen < 8) {
    return;
  }

  for (int i = 2; i < 8; ++i) {
    const uint8_t k = boot[i];
    if (k == 0) {
      continue;
    }
    if (m_keyCallback) {
      m_keyCallback(k, true);
    }

    uint8_t btn = 0xFF;
    switch (k) {
      case 0x28:  // Return / Enter
        btn = 1;
        break;
      case 0x29:  // Escape
        btn = 0;
        break;
      case 0x52:  // Up arrow
        btn = 4;
        break;
      case 0x51:  // Down arrow
        btn = 5;
        break;
      case 0x50:  // Left arrow
        btn = 2;
        break;
      case 0x4F:  // Right arrow
        btn = 3;
        break;
      default:
        break;
    }
    if (btn != 0xFF) {
      m_bleButtonSink(btn);
    }
  }
}

void BluetoothManager::handleClientDisconnected(NimBLEClient* pClient) {
  if (pClient == nullptr) {
    return;
  }

  std::string erasedAddr;
  for (auto it = m_clientsByAddr.begin(); it != m_clientsByAddr.end(); ++it) {
    if (it->second == pClient) {
      erasedAddr = it->first;
      m_clientsByAddr.erase(it);
      break;
    }
  }

  if (erasedAddr.empty()) {
    // disconnectAll() already cleared the map; outer deleteClient owns teardown.
    return;
  }

  m_connected.erase(std::remove(m_connected.begin(), m_connected.end(), erasedAddr), m_connected.end());
  NimBLEDevice::deleteClient(pClient);
}

bool BluetoothManager::connectToDevice(const std::string& address) {
  LOG("=== connectToDevice called for %s ===", address.c_str());

  if (!m_enabled) {
    LOG("Cannot connect - Bluetooth not enabled");
    return false;
  }

  if (isConnected(address)) {
    LOG("Already connected to %s", address.c_str());
    return true;
  }

  NimBLEAddress bleAddress(address, BLE_ADDR_PUBLIC);

  LOG("Creating BLE client...");
  NimBLEClient* pClient = NimBLEDevice::createClient(bleAddress);
  if (!pClient) {
    LOG("Failed to create BLE client");
    return false;
  }

  pClient->setClientCallbacks(&clientCallbacks, false);
  pClient->setConnectTimeout(BLE_CONNECT_TIMEOUT_MS);
  pClient->setConnectionParams(BLE_CONN_MIN_INTERVAL, BLE_CONN_MAX_INTERVAL, BLE_CONN_LATENCY, BLE_CONN_TIMEOUT,
                               BLE_CONN_SCAN_INTERVAL, BLE_CONN_SCAN_WINDOW);

  LOG("Attempting to connect to %s...", address.c_str());

  if (!pClient->connect(bleAddress)) {
    LOG("Failed to connect to %s", address.c_str());
    NimBLEDevice::deleteClient(pClient);
    return false;
  }

  LOG("Connected successfully!");
  trySubscribeHid(pClient);

  m_clientsByAddr[address] = pClient;
  m_connected.push_back(address);
  LOG("=== Successfully connected to %s ===", address.c_str());
  return true;
}

void BluetoothManager::disconnectAll() {
  LOG("Disconnecting all devices...");
  std::vector<NimBLEClient*> clients;
  clients.reserve(m_clientsByAddr.size());
  for (auto& kv : m_clientsByAddr) {
    if (kv.second != nullptr) {
      clients.push_back(kv.second);
    }
  }
  m_clientsByAddr.clear();
  m_connected.clear();

  for (NimBLEClient* c : clients) {
    if (c != nullptr) {
      NimBLEDevice::deleteClient(c);
    }
  }
}
