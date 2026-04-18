#include "BluetoothManager.h"

#include <HalGPIO.h>
#include <NimBLEDevice.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "state/SystemSetting.h"

#define LOG(fmt, ...) printf("[BT] " fmt "\n", ##__VA_ARGS__)

namespace {
struct FrontLayoutMap {
  uint8_t back;
  uint8_t confirm;
  uint8_t left;
  uint8_t right;
};

constexpr FrontLayoutMap kBleFrontLayouts[] = {
    {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT},
    {HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT, HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM},
    {HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_BACK, HalGPIO::BTN_RIGHT},
    {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_RIGHT, HalGPIO::BTN_LEFT},
};

void bootKeyPresence(const uint8_t boot[8], bool has[256]) {
  memset(has, 0, 256);
  for (int i = 2; i < 8; ++i) {
    const uint8_t k = boot[i];
    if (k != 0) {
      has[k] = true;
    }
  }
}

uint8_t readerPagePrevHal() {
  using SM = SystemSetting::READER_DIRECTION_MAPPING;
  const SM map = static_cast<SM>(SETTINGS.readerDirectionMapping);
  const auto fl = static_cast<SystemSetting::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout);
  const size_t idx = fl < SystemSetting::FRONT_BUTTON_LAYOUT_COUNT ? static_cast<size_t>(fl) : 0u;
  const FrontLayoutMap& f = kBleFrontLayouts[idx];

  switch (map) {
    case SM::MAP_NONE:
    case SM::MAP_UP_DOWN:
      return HalGPIO::BTN_UP;
    case SM::MAP_DOWN_UP:
      return HalGPIO::BTN_DOWN;
    case SM::MAP_RIGHT_LEFT:
      return f.right;
    case SM::MAP_LEFT_RIGHT:
    default:
      return f.left;
  }
}

uint8_t readerPageNextHal() {
  using SM = SystemSetting::READER_DIRECTION_MAPPING;
  const SM map = static_cast<SM>(SETTINGS.readerDirectionMapping);
  const auto fl = static_cast<SystemSetting::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout);
  const size_t idx = fl < SystemSetting::FRONT_BUTTON_LAYOUT_COUNT ? static_cast<size_t>(fl) : 0u;
  const FrontLayoutMap& f = kBleFrontLayouts[idx];

  switch (map) {
    case SM::MAP_NONE:
    case SM::MAP_UP_DOWN:
      return HalGPIO::BTN_DOWN;
    case SM::MAP_DOWN_UP:
      return HalGPIO::BTN_UP;
    case SM::MAP_RIGHT_LEFT:
      return f.left;
    case SM::MAP_LEFT_RIGHT:
    default:
      return f.right;
  }
}

uint8_t hidKeyToNavHalPress(uint8_t k) {
  switch (k) {
    case 0x28:
      return HalGPIO::BTN_CONFIRM;
    case 0x29:
      return HalGPIO::BTN_BACK;
    case 0x52:
      return HalGPIO::BTN_UP;
    case 0x51:
      return HalGPIO::BTN_DOWN;
    case 0x50:
      return HalGPIO::BTN_LEFT;
    case 0x4F:
      return HalGPIO::BTN_RIGHT;
    default:
      return 0xFF;
  }
}

static std::atomic<bool> s_readerPageTurnerConnectBusy{false};

void readerPageTurnerConnectTask(void* /*param*/) {
  BluetoothManager& m = BluetoothManager::getInstance();
  const std::string addr(SETTINGS.bleSavedAddress);
  if (addr.empty()) {
    s_readerPageTurnerConnectBusy = false;
    vTaskDelete(nullptr);
    return;
  }
  if (!m.isEnabled()) {
    m.enable();
  }
  if (m.connectToDevice(addr)) {
    m.setReaderPageTurnerSession(true);
  }
  s_readerPageTurnerConnectBusy = false;
  vTaskDelete(nullptr);
}
}  // namespace

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
  memset(m_prevHidBoot, 0, sizeof(m_prevHidBoot));
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

void BluetoothManager::startReaderPageTurnerConnectTask() {
  if (s_readerPageTurnerConnectBusy.exchange(true)) {
    return;
  }
  if (xTaskCreate(readerPageTurnerConnectTask, "BTreadPT", 5120, nullptr, 1, nullptr) != pdPASS) {
    s_readerPageTurnerConnectBusy = false;
  }
}

void BluetoothManager::activateReaderPageTurnerFromBookDrawer() {
  const std::string addr(SETTINGS.bleSavedAddress);
  if (addr.empty()) {
    return;
  }
  if (isConnected(addr)) {
    setReaderPageTurnerSession(true);
    return;
  }
  startReaderPageTurnerConnectTask();
}

void BluetoothManager::onHidReport(const uint8_t* data, size_t len) {
  if (!m_halGpio || data == nullptr || len < 2) {
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

  uint8_t normBoot[8];
  memcpy(normBoot, boot, 8);

  bool prevHas[256];
  bool currHas[256];
  bootKeyPresence(m_prevHidBoot, prevHas);
  bootKeyPresence(normBoot, currHas);

  for (unsigned k = 1; k < 256; ++k) {
    if (currHas[k] && !prevHas[k]) {
      if (m_keyCallback) {
        m_keyCallback(static_cast<uint8_t>(k), true);
      }
      if (k == SETTINGS.bleHidPagePrevKey || k == SETTINGS.bleHidPageNextKey) {
        continue;
      }
      const uint8_t btn = hidKeyToNavHalPress(static_cast<uint8_t>(k));
      if (btn != 0xFF) {
        m_halGpio->injectOneShotPress(btn);
      }
    } else if (prevHas[k] && !currHas[k]) {
      if (m_keyCallback) {
        m_keyCallback(static_cast<uint8_t>(k), false);
      }
      if (k == SETTINGS.bleHidPagePrevKey) {
        m_halGpio->injectOneShotRelease(readerPagePrevHal());
      } else if (k == SETTINGS.bleHidPageNextKey) {
        m_halGpio->injectOneShotRelease(readerPageNextHal());
      } else {
        const uint8_t btn = hidKeyToNavHalPress(static_cast<uint8_t>(k));
        if (btn != 0xFF) {
          m_halGpio->injectOneShotRelease(btn);
        }
      }
    }
  }

  memcpy(m_prevHidBoot, normBoot, sizeof(m_prevHidBoot));
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
  memset(m_prevHidBoot, 0, sizeof(m_prevHidBoot));
  m_readerPageTurnerSession = false;
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

  strncpy(SETTINGS.bleSavedAddress, address.c_str(), sizeof(SETTINGS.bleSavedAddress) - 1);
  SETTINGS.bleSavedAddress[sizeof(SETTINGS.bleSavedAddress) - 1] = '\0';
  for (const auto& d : m_devices) {
    if (d.address == address) {
      strncpy(SETTINGS.bleSavedName, d.name.c_str(), sizeof(SETTINGS.bleSavedName) - 1);
      SETTINGS.bleSavedName[sizeof(SETTINGS.bleSavedName) - 1] = '\0';
      break;
    }
  }
  SETTINGS.saveToFile();
  memset(m_prevHidBoot, 0, sizeof(m_prevHidBoot));

  LOG("=== Successfully connected to %s ===", address.c_str());
  return true;
}

void BluetoothManager::disconnectAll() {
  LOG("Disconnecting all devices...");
  m_readerPageTurnerSession = false;
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
