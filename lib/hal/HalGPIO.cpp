/**
 * @file HalGPIO.cpp
 * @brief Definitions for HalGPIO.
 */

#include <HalGPIO.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <time.h>

namespace X3GPIO {

struct X3ProbeResult {
  bool bq27220 = false;
  bool ds3231 = false;
  bool qmi8658 = false;

  uint8_t score() const {
    return static_cast<uint8_t>(bq27220) + static_cast<uint8_t>(ds3231) + static_cast<uint8_t>(qmi8658);
  }
};

bool readI2CReg8(uint8_t addr, uint8_t reg, uint8_t* outValue) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) < 1) {
    return false;
  }
  *outValue = Wire.read();
  return true;
}

bool readI2CReg16LE(uint8_t addr, uint8_t reg, uint16_t* outValue) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) < 2) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  *outValue = (static_cast<uint16_t>(hi) << 8) | lo;
  return true;
}

bool readI2CRegs(uint8_t addr, uint8_t reg, uint8_t* out, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, len, static_cast<uint8_t>(true)) < len) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  for (uint8_t i = 0; i < len; ++i) {
    out[i] = Wire.read();
  }
  return true;
}

bool writeI2CRegs(uint8_t addr, uint8_t reg, const uint8_t* data, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  for (uint8_t i = 0; i < len; ++i) {
    Wire.write(data[i]);
  }
  return Wire.endTransmission(true) == 0;
}

bool readBQ27220CurrentMA(int16_t* outCurrent) {
  uint16_t raw = 0;
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_CUR_REG, &raw)) {
    return false;
  }
  *outCurrent = static_cast<int16_t>(raw);
  return true;
}

bool readBQ27220StateOfCharge(uint16_t* outSoc) {
  return readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_SOC_REG, outSoc);
}

void beginX3I2C() {
  Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
  Wire.setTimeOut(6);
}

void endX3I2C() {
  Wire.end();
  pinMode(X3_I2C_SDA, INPUT);
  pinMode(X3_I2C_SCL, INPUT);
}

bool probeBQ27220Signature() {
  uint16_t soc = 0;
  uint16_t voltageMv = 0;
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_SOC_REG, &soc) || soc > 100) {
    return false;
  }
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_VOLT_REG, &voltageMv)) {
    return false;
  }
  return voltageMv >= 2500 && voltageMv <= 5000;
}

bool probeDS3231Signature() {
  uint8_t sec = 0;
  if (!readI2CReg8(I2C_ADDR_DS3231, DS3231_SEC_REG, &sec)) {
    return false;
  }
  const uint8_t tensDigit = (sec >> 4) & 0x07;
  const uint8_t onesDigit = sec & 0x0F;
  return tensDigit <= 5 && onesDigit <= 9;
}

bool probeQMI8658Signature() {
  uint8_t whoami = 0;
  if (readI2CReg8(I2C_ADDR_QMI8658, QMI8658_WHO_AM_I_REG, &whoami) && whoami == QMI8658_WHO_AM_I_VALUE) {
    return true;
  }
  if (readI2CReg8(I2C_ADDR_QMI8658_ALT, QMI8658_WHO_AM_I_REG, &whoami) && whoami == QMI8658_WHO_AM_I_VALUE) {
    return true;
  }
  return false;
}

X3ProbeResult runX3ProbePass() {
  X3ProbeResult result;
  beginX3I2C();

  result.bq27220 = probeBQ27220Signature();
  result.ds3231 = probeDS3231Signature();
  result.qmi8658 = probeQMI8658Signature();

  endX3I2C();
  return result;
}

}  // namespace X3GPIO

namespace {

constexpr char HW_NAMESPACE[] = "inxhw";
constexpr char NVS_KEY_DEV_OVERRIDE[] = "dev_ovr";
constexpr char NVS_KEY_DEV_CACHED[] = "dev_det";

enum class NvsDeviceValue : uint8_t { Unknown = 0, X4 = 1, X3 = 2 };

NvsDeviceValue readNvsDeviceValue(const char* key, NvsDeviceValue defaultValue) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, true)) {
    return defaultValue;
  }
  const uint8_t raw = prefs.getUChar(key, static_cast<uint8_t>(defaultValue));
  prefs.end();
  if (raw > static_cast<uint8_t>(NvsDeviceValue::X3)) {
    return defaultValue;
  }
  return static_cast<NvsDeviceValue>(raw);
}

void writeNvsDeviceValue(const char* key, NvsDeviceValue value) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, false)) {
    return;
  }
  prefs.putUChar(key, static_cast<uint8_t>(value));
  prefs.end();
}

HalGPIO::DeviceType nvsToDeviceType(NvsDeviceValue value) {
  return value == NvsDeviceValue::X3 ? HalGPIO::DeviceType::X3 : HalGPIO::DeviceType::X4;
}

HalGPIO::DeviceType detectDeviceTypeWithFingerprint() {
  const NvsDeviceValue overrideValue = readNvsDeviceValue(NVS_KEY_DEV_OVERRIDE, NvsDeviceValue::Unknown);
  if (overrideValue == NvsDeviceValue::X3 || overrideValue == NvsDeviceValue::X4) {
    return nvsToDeviceType(overrideValue);
  }

  const NvsDeviceValue cachedValue = readNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::Unknown);
  if (cachedValue == NvsDeviceValue::X3 || cachedValue == NvsDeviceValue::X4) {
    return nvsToDeviceType(cachedValue);
  }

  const X3GPIO::X3ProbeResult pass1 = X3GPIO::runX3ProbePass();
  delay(2);
  const X3GPIO::X3ProbeResult pass2 = X3GPIO::runX3ProbePass();
  const bool x3Confirmed = pass1.score() >= 2 && pass2.score() >= 2;
  const bool x4Confirmed = pass1.score() == 0 && pass2.score() == 0;

  if (x3Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X3);
    return HalGPIO::DeviceType::X3;
  }
  if (x4Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X4);
    return HalGPIO::DeviceType::X4;
  }
  return HalGPIO::DeviceType::X4;
}

uint8_t bcdToDec(uint8_t value) { return ((value >> 4) * 10) + (value & 0x0F); }

uint8_t decToBcd(uint8_t value) { return static_cast<uint8_t>(((value / 10) << 4) | (value % 10)); }

bool validDateTime(const HalGPIO::DateTime& dt) {
  return dt.year >= 2024 && dt.year <= 2099 && dt.month >= 1 && dt.month <= 12 && dt.day >= 1 && dt.day <= 31 &&
         dt.hour <= 23 && dt.minute <= 59 && dt.second <= 59;
}

}  // namespace

void HalGPIO::begin() {
  inputMgr.begin();
  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);
  deviceType = detectDeviceTypeWithFingerprint();
  if (deviceIsX4()) {
    pinMode(BAT_GPIO0, INPUT);
    pinMode(UART0_RXD, INPUT);
  }
}

void HalGPIO::update() { inputMgr.update(); }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return inputMgr.wasPressed(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed(); }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return inputMgr.wasReleased(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased(); }

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

void HalGPIO::startDeepSleep(uint32_t timerWakeSeconds) {
  
  while (inputMgr.isPressed(BTN_POWER)) {
    delay(50);
    inputMgr.update();
  }
  
  esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  if (timerWakeSeconds > 0) {
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(timerWakeSeconds) * 1000000ULL);
  }
  
  esp_deep_sleep_start();
}

int HalGPIO::getBatteryPercentage() const {
  if (deviceIsX3()) {
    const unsigned long now = millis();
    if (batteryLastPollMs != 0 && (now - batteryLastPollMs) < BATTERY_POLL_MS) {
      return batteryCachedPercent;
    }

    uint16_t soc = 0;
    X3GPIO::beginX3I2C();
    const bool ok = X3GPIO::readBQ27220StateOfCharge(&soc);
    X3GPIO::endX3I2C();
    if (ok && soc <= 100) {
      batteryCachedPercent = static_cast<int>(soc);
      batteryLastPollMs = now;
      return batteryCachedPercent;
    }
    batteryLastPollMs = now;
    return batteryCachedPercent;
  }
  static const BatteryMonitor battery = BatteryMonitor(BAT_GPIO0);
  return battery.readPercentage();
}

bool HalGPIO::isUsbConnected() const {
  if (deviceIsX3()) {
    X3GPIO::beginX3I2C();
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
      int16_t currentMa = 0;
      if (X3GPIO::readBQ27220CurrentMA(&currentMa)) {
        X3GPIO::endX3I2C();
        return currentMa > 0;
      }
      delay(2);
    }
    X3GPIO::endX3I2C();
    return false;
  }
  return digitalRead(UART0_RXD) == HIGH;
}

bool HalGPIO::readDateTime(DateTime& outDateTime) const {
  if (!deviceIsX3()) {
    return false;
  }

  uint8_t regs[7] = {};
  X3GPIO::beginX3I2C();
  const bool ok = X3GPIO::readI2CRegs(I2C_ADDR_DS3231, DS3231_SEC_REG, regs, sizeof(regs));
  X3GPIO::endX3I2C();
  if (!ok) {
    return false;
  }

  DateTime dt;
  dt.second = bcdToDec(regs[0] & 0x7F);
  dt.minute = bcdToDec(regs[1] & 0x7F);
  dt.hour = bcdToDec(regs[2] & 0x3F);
  dt.weekday = bcdToDec(regs[3] & 0x07);
  dt.day = bcdToDec(regs[4] & 0x3F);
  dt.month = bcdToDec(regs[5] & 0x1F);
  dt.year = static_cast<uint16_t>(2000 + bcdToDec(regs[6]));

  if (!validDateTime(dt)) {
    return false;
  }

  outDateTime = dt;
  return true;
}

bool HalGPIO::writeDateTime(const DateTime& dateTime) const {
  if (!deviceIsX3() || !validDateTime(dateTime)) {
    return false;
  }

  const uint8_t regs[7] = {decToBcd(dateTime.second),
                           decToBcd(dateTime.minute),
                           decToBcd(dateTime.hour),
                           decToBcd(dateTime.weekday >= 1 && dateTime.weekday <= 7 ? dateTime.weekday : 1),
                           decToBcd(dateTime.day),
                           decToBcd(dateTime.month),
                           decToBcd(static_cast<uint8_t>(dateTime.year - 2000))};
  X3GPIO::beginX3I2C();
  const bool ok = X3GPIO::writeI2CRegs(I2C_ADDR_DS3231, DS3231_SEC_REG, regs, sizeof(regs));
  X3GPIO::endX3I2C();
  return ok;
}

bool HalGPIO::syncRtcFromSystemTime() const {
  if (!deviceIsX3()) {
    return false;
  }

  const time_t now = time(nullptr);
  if (now < 1704067200) {
    return false;
  }

  struct tm localTime {};
  if (localtime_r(&now, &localTime) == nullptr) {
    return false;
  }

  DateTime dt;
  dt.year = static_cast<uint16_t>(localTime.tm_year + 1900);
  dt.month = static_cast<uint8_t>(localTime.tm_mon + 1);
  dt.day = static_cast<uint8_t>(localTime.tm_mday);
  dt.hour = static_cast<uint8_t>(localTime.tm_hour);
  dt.minute = static_cast<uint8_t>(localTime.tm_min);
  dt.second = static_cast<uint8_t>(localTime.tm_sec);
  dt.weekday = static_cast<uint8_t>(localTime.tm_wday == 0 ? 7 : localTime.tm_wday);
  return writeDateTime(dt);
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  if (wakeupCause == ESP_SLEEP_WAKEUP_TIMER && resetReason == ESP_RST_DEEPSLEEP) {
    return WakeupReason::Timer;
  }

  const bool usbConnected = isUsbConnected();

  if ((wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected) ||
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO && resetReason == ESP_RST_DEEPSLEEP && usbConnected)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}
