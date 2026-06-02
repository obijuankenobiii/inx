#pragma once

/**
 * @file HalGPIO.h
 * @brief Public interface and types for HalGPIO.
 */

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>


#define EPD_SCLK 8   
#define EPD_MOSI 10  
#define EPD_CS 21    
#define EPD_DC 4     
#define EPD_RST 5    
#define EPD_BUSY 6   

#define SPI_MISO 7  

#define BAT_GPIO0 0  

#define UART0_RXD 20  

#define X3_I2C_SDA 20
#define X3_I2C_SCL 0
#define X3_I2C_FREQ 400000

#define I2C_ADDR_BQ27220 0x55
#define BQ27220_SOC_REG 0x2C
#define BQ27220_CUR_REG 0x0C
#define BQ27220_VOLT_REG 0x08

#define I2C_ADDR_DS3231 0x68
#define DS3231_SEC_REG 0x00

#define I2C_ADDR_QMI8658 0x6B
#define I2C_ADDR_QMI8658_ALT 0x6A
#define QMI8658_WHO_AM_I_REG 0x00
#define QMI8658_WHO_AM_I_VALUE 0x05

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;
#endif

 public:
  enum class DeviceType : uint8_t { X4, X3 };

 private:
  DeviceType deviceType = DeviceType::X4;

 public:
  HalGPIO() = default;

  bool deviceIsX3() const { return deviceType == DeviceType::X3; }
  bool deviceIsX4() const { return deviceType == DeviceType::X4; }

  
  void begin();

  
  void update();
  void injectOneShotPress(uint8_t buttonIndex) {
#if CROSSPOINT_EMULATED == 0
    inputMgr.injectOneShotPress(buttonIndex);
#else
    (void)buttonIndex;
#endif
  }
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;

  
  void startDeepSleep();

  
  int getBatteryPercentage() const;

  
  bool isUsbConnected() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
};

extern HalGPIO gpio;
