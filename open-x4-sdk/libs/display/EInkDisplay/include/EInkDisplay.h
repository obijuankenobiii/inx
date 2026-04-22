#pragma once

/**
 * @file EInkDisplay.h
 * @brief Public interface and types for EInkDisplay.
 */

#include <Arduino.h>
#include <SPI.h>

class EInkDisplay {
 public:
  
  EInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy);

  
  ~EInkDisplay() = default;

  
  enum RefreshMode {
    FULL_REFRESH,  
    HALF_REFRESH,  
    FAST_REFRESH   
  };

  
  void begin();

  
  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool fromProgmem = false) const;

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  void swapBuffers();
#endif
  void setFramebuffer(const uint8_t* bwBuffer) const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);
#endif

  void displayBuffer(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);
  
  void displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool turnOffScreen = false);
  void displayGrayBuffer(bool turnOffScreen = false);

  void refreshDisplay(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);

  
  void grayscaleRevert();

  
  void setCustomLUT(bool enabled, const unsigned char* lutData = nullptr);

  
  void deepSleep();

  
  uint8_t* getFrameBuffer() const {
    return frameBuffer;
  }

  
  void saveFrameBufferAsPBM(const char* filename);

 private:
  
  int8_t _sclk, _mosi, _cs, _dc, _rst, _busy;

  
  uint8_t frameBuffer0[BUFFER_SIZE];
  uint8_t* frameBuffer;
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  uint8_t frameBuffer1[BUFFER_SIZE];
  uint8_t* frameBufferActive;
#endif

  
  SPISettings spiSettings;

  
  bool isScreenOn;
  bool customLutActive;
  bool inGrayscaleMode;
  bool drawGrayscale;

  
  void resetDisplay();
  void sendCommand(uint8_t command);
  void sendData(uint8_t data);
  void sendData(const uint8_t* data, uint16_t length);
  void waitWhileBusy(const char* comment = nullptr);
  void initDisplayController();

  
  void setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size);
};
