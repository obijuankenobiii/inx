#pragma once

#ifdef SIMULATOR

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>

#ifndef EPD_SCLK
#define EPD_SCLK 8
#endif
#ifndef EPD_MOSI
#define EPD_MOSI 10
#endif
#ifndef EPD_CS
#define EPD_CS 21
#endif
#ifndef EPD_DC
#define EPD_DC 4
#endif
#ifndef EPD_RST
#define EPD_RST 5
#endif
#ifndef EPD_BUSY
#define EPD_BUSY 6
#endif

#define SPI_MISO 7

#define BAT_GPIO0 0

#define UART0_RXD 20

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;
#endif

 public:
  enum class DeviceType : uint8_t { X4, X3 };
  enum class MotionGesture : uint8_t { None, Previous, Next };

 private:
  DeviceType _deviceType = DeviceType::X4;

 public:
  HalGPIO() = default;

  inline bool deviceIsX3() const { return _deviceType == DeviceType::X3; }
  inline bool deviceIsX4() const { return _deviceType == DeviceType::X4; }

  void begin();
  void beginFrame();

  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  unsigned long getPowerButtonHeldTime() const;
  bool consumeSimulatorSleepRequest();

  void startDeepSleep();
  void verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed);

  bool isUsbConnected() const;
  bool wasUsbStateChanged() const;

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

#else
#error "src/simulator/HalGPIO.h is only for simulator builds"
#endif
