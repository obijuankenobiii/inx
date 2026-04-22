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

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;
#endif

 public:
  HalGPIO() = default;

  
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
