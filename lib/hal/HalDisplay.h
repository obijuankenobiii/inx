#pragma once

/**
 * @file HalDisplay.h
 * @brief Public interface and types for HalDisplay.
 */

#include <Arduino.h>
#include <EInkDisplay.h>

class HalDisplay {
 public:
  
  HalDisplay();

  
  ~HalDisplay();

  
  enum RefreshMode {
    FULL_REFRESH,  
    HALF_REFRESH,  
    FAST_REFRESH   
  };

  
  void begin();

  
  static constexpr uint16_t DISPLAY_WIDTH = EInkDisplay::DISPLAY_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = EInkDisplay::DISPLAY_HEIGHT;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH);
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  void setSunlightFadeFixEnabled(bool enabled);

  
  void deepSleep();

  
  uint8_t* getFrameBuffer() const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  void displayGrayBuffer();

 private:
  EInkDisplay einkDisplay;
};
