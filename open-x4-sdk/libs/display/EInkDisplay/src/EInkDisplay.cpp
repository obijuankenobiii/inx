/**
 * @file EInkDisplay.cpp
 * @brief Definitions for EInkDisplay.
 */

#include "EInkDisplay.h"

#include <cstring>
#include <fstream>
#include <vector>



#define CMD_SOFT_RESET 0x12             
#define CMD_BOOSTER_SOFT_START 0x0C     
#define CMD_DRIVER_OUTPUT_CONTROL 0x01  
#define CMD_BORDER_WAVEFORM 0x3C        
#define CMD_TEMP_SENSOR_CONTROL 0x18    


#define CMD_DATA_ENTRY_MODE 0x11     
#define CMD_SET_RAM_X_RANGE 0x44     
#define CMD_SET_RAM_Y_RANGE 0x45     
#define CMD_SET_RAM_X_COUNTER 0x4E   
#define CMD_SET_RAM_Y_COUNTER 0x4F   
#define CMD_WRITE_RAM_BW 0x24        
#define CMD_WRITE_RAM_RED 0x26       
#define CMD_AUTO_WRITE_BW_RAM 0x46   
#define CMD_AUTO_WRITE_RED_RAM 0x47  


#define CMD_DISPLAY_UPDATE_CTRL1 0x21  
#define CMD_DISPLAY_UPDATE_CTRL2 0x22  
#define CMD_MASTER_ACTIVATION 0x20     
#define CTRL1_NORMAL 0x00              
#define CTRL1_BYPASS_RED 0x40          


#define CMD_WRITE_LUT 0x32       
#define CMD_GATE_VOLTAGE 0x03    
#define CMD_SOURCE_VOLTAGE 0x04  
#define CMD_WRITE_VCOM 0x2C      
#define CMD_WRITE_TEMP 0x1A      


#define CMD_DEEP_SLEEP 0x10  

namespace {
struct DriveVoltages {
  uint8_t gate;
  uint8_t source1;
  uint8_t source2;
  uint8_t source3;
  uint8_t vcom;
};

constexpr DriveVoltages kNormalDriveVoltages{0x17, 0x41, 0xA8, 0x32, 0x30};
constexpr DriveVoltages kSunlightFadeDriveVoltages{0x15, 0x3F, 0xA0, 0x2E, 0x2C};
}  


const unsigned char lut_grayscale[] PROGMEM = {
    
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    
    0x54, 0x54, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    
    0xAA, 0xA0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    
    0xA2, 0x22, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    
    0x01, 0x01, 0x01, 0x01, 0x00,  
    0x01, 0x01, 0x01, 0x01, 0x00,  
    0x01, 0x01, 0x01, 0x01, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  

    
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,

    
    0x17, 0x41, 0xA8, 0x32, 0x30,

    
    0x00, 0x00};

const unsigned char lut_grayscale_revert[] PROGMEM = {
    
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    
    0x54, 0x54, 0x54, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    
    0xA8, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    
    0xFC, 0xFC, 0xFC, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    
    0x01, 0x01, 0x01, 0x01, 0x01,  
    0x01, 0x01, 0x01, 0x01, 0x01,  
    0x01, 0x01, 0x01, 0x01, 0x00,  
    0x01, 0x01, 0x01, 0x01, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  
    0x00, 0x00, 0x00, 0x00, 0x00,  

    
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,

    
    0x17, 0x41, 0xA8, 0x32, 0x30,

    
    0x00, 0x00};

EInkDisplay::EInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy)
    : _sclk(sclk),
      _mosi(mosi),
      _cs(cs),
      _dc(dc),
      _rst(rst),
      _busy(busy),
      frameBuffer(nullptr),
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
      frameBufferActive(nullptr),
#endif
      customLutActive(false),
      sunlightFadeFixEnabled(false),
      sunlightFadeVoltagesApplied(false),
      inGrayscaleMode(false),
      drawGrayscale(false) {
  if (Serial) Serial.printf("[%lu] EInkDisplay: Constructor called\n", millis());
  if (Serial) Serial.printf("[%lu]   SCLK=%d, MOSI=%d, CS=%d, DC=%d, RST=%d, BUSY=%d\n", millis(), sclk, mosi, cs, dc, rst, busy);
}

void EInkDisplay::begin() {
  if (Serial) Serial.printf("[%lu] EInkDisplay: begin() called\n", millis());

  frameBuffer = frameBuffer0;
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  frameBufferActive = frameBuffer1;
#endif

  
  memset(frameBuffer0, 0xFF, BUFFER_SIZE);
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  if (Serial) Serial.printf("[%lu]   Static frame buffer (%lu bytes = 48KB)\n", millis(), BUFFER_SIZE);
#else
  memset(frameBuffer1, 0xFF, BUFFER_SIZE);
  if (Serial) Serial.printf("[%lu]   Static frame buffers (2 x %lu bytes = 96KB)\n", millis(), BUFFER_SIZE);
#endif

  if (Serial) Serial.printf("[%lu]   Initializing e-ink display driver...\n", millis());

  
  SPI.begin(_sclk, -1, _mosi, _cs);
  spiSettings = SPISettings(40000000, MSBFIRST, SPI_MODE0);  
  if (Serial) Serial.printf("[%lu]   SPI initialized at 40 MHz, Mode 0\n", millis());

  
  pinMode(_cs, OUTPUT);
  pinMode(_dc, OUTPUT);
  pinMode(_rst, OUTPUT);
  pinMode(_busy, INPUT);

  digitalWrite(_cs, HIGH);
  digitalWrite(_dc, HIGH);

  if (Serial) Serial.printf("[%lu]   GPIO pins configured\n", millis());

  
  resetDisplay();

  
  initDisplayController();

  if (Serial) Serial.printf("[%lu]   E-ink display driver initialized\n", millis());
}





void EInkDisplay::resetDisplay() {
  if (Serial) Serial.printf("[%lu]   Resetting display...\n", millis());
  digitalWrite(_rst, HIGH);
  delay(20);
  digitalWrite(_rst, LOW);
  delay(2);
  digitalWrite(_rst, HIGH);
  delay(20);
  if (Serial) Serial.printf("[%lu]   Display reset complete\n", millis());
}

void EInkDisplay::sendCommand(uint8_t command) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, LOW);  
  digitalWrite(_cs, LOW);  
  SPI.transfer(command);
  digitalWrite(_cs, HIGH);  
  SPI.endTransaction();
}

void EInkDisplay::sendData(uint8_t data) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);  
  digitalWrite(_cs, LOW);   
  SPI.transfer(data);
  digitalWrite(_cs, HIGH);  
  SPI.endTransaction();
}

void EInkDisplay::sendData(const uint8_t* data, uint16_t length) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);       
  digitalWrite(_cs, LOW);        
  SPI.writeBytes(data, length);  
  digitalWrite(_cs, HIGH);       
  SPI.endTransaction();
}

void EInkDisplay::waitWhileBusy(const char* comment) {
  unsigned long start = millis();
  while (digitalRead(_busy) == HIGH) {
    delay(1);
    if (millis() - start > 10000) {
      if (Serial) Serial.printf("[%lu]   Timeout waiting for busy%s\n", millis(), comment ? comment : "");
      break;
    }
  }
  if (comment) {
    if (Serial) Serial.printf("[%lu]   Wait complete: %s (%lu ms)\n", millis(), comment, millis() - start);
  }
}

void EInkDisplay::initDisplayController() {
  if (Serial) Serial.printf("[%lu]   Initializing SSD1677 controller...\n", millis());

  const uint8_t TEMP_SENSOR_INTERNAL = 0x80;

  
  sendCommand(CMD_SOFT_RESET);
  waitWhileBusy(" CMD_SOFT_RESET");

  
  sendCommand(CMD_TEMP_SENSOR_CONTROL);
  sendData(TEMP_SENSOR_INTERNAL);

  
  sendCommand(CMD_BOOSTER_SOFT_START);
  sendData(0xAE);
  sendData(0xC7);
  sendData(0xC3);
  sendData(0xC0);
  sendData(0x40);

  
  const uint16_t HEIGHT = 480;
  sendCommand(CMD_DRIVER_OUTPUT_CONTROL);
  sendData((HEIGHT - 1) % 256);  
  sendData((HEIGHT - 1) / 256);  
  sendData(0x02);                

  
  sendCommand(CMD_BORDER_WAVEFORM);
  sendData(0x01);

  
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

  if (Serial) Serial.printf("[%lu]   Clearing RAM buffers...\n", millis());
  sendCommand(CMD_AUTO_WRITE_BW_RAM);  
  sendData(0xF7);
  waitWhileBusy(" CMD_AUTO_WRITE_BW_RAM");

  sendCommand(CMD_AUTO_WRITE_RED_RAM);  
  sendData(0xF7);                       
  waitWhileBusy(" CMD_AUTO_WRITE_RED_RAM");

  if (Serial) Serial.printf("[%lu]   SSD1677 controller initialized\n", millis());
}

void EInkDisplay::setRamArea(const uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  constexpr uint8_t DATA_ENTRY_X_INC_Y_DEC = 0x01;

  
  y = DISPLAY_HEIGHT - y - h;

  
  sendCommand(CMD_DATA_ENTRY_MODE);
  sendData(DATA_ENTRY_X_INC_Y_DEC);

  
  sendCommand(CMD_SET_RAM_X_RANGE);
  sendData(x % 256);            
  sendData(x / 256);            
  sendData((x + w - 1) % 256);  
  sendData((x + w - 1) / 256);  

  
  sendCommand(CMD_SET_RAM_Y_RANGE);
  sendData((y + h - 1) % 256);  
  sendData((y + h - 1) / 256);  
  sendData(y % 256);            
  sendData(y / 256);            

  
  sendCommand(CMD_SET_RAM_X_COUNTER);
  sendData(x % 256);  
  sendData(x / 256);  

  
  sendCommand(CMD_SET_RAM_Y_COUNTER);
  sendData((y + h - 1) % 256);  
  sendData((y + h - 1) / 256);  
}

void EInkDisplay::clearScreen(const uint8_t color) const {
  memset(frameBuffer, color, BUFFER_SIZE);
}

void EInkDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h,
                            const bool fromProgmem) const {
  if (!frameBuffer) {
    if (Serial) Serial.printf("[%lu]   ERROR: Frame buffer not allocated!\n", millis());
    return;
  }

  
  const uint16_t imageWidthBytes = w / 8;

  
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT)
      break;

    const uint16_t destOffset = destY * DISPLAY_WIDTH_BYTES + (x / 8);
    const uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((x / 8 + col) >= DISPLAY_WIDTH_BYTES)
        break;

      if (fromProgmem) {
        frameBuffer[destOffset + col] = pgm_read_byte(&imageData[srcOffset + col]);
      } else {
        frameBuffer[destOffset + col] = imageData[srcOffset + col];
      }
    }
  }

  if (Serial) Serial.printf("[%lu]   Image drawn to frame buffer\n", millis());
}

void EInkDisplay::writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size) {
  const char* bufferName = (ramBuffer == CMD_WRITE_RAM_BW) ? "BW" : "RED";
  const unsigned long startTime = millis();
  if (Serial) Serial.printf("[%lu]   Writing frame buffer to %s RAM (%lu bytes)...\n", startTime, bufferName, size);

  sendCommand(ramBuffer);
  sendData(data, size);

  const unsigned long duration = millis() - startTime;
  if (Serial) Serial.printf("[%lu]   %s RAM write complete (%lu ms)\n", millis(), bufferName, duration);
}

void EInkDisplay::setFramebuffer(const uint8_t* bwBuffer) const {
  memcpy(frameBuffer, bwBuffer, BUFFER_SIZE);
}

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
void EInkDisplay::swapBuffers() {
  uint8_t* temp = frameBuffer;
  frameBuffer = frameBufferActive;
  frameBufferActive = temp;
}
#endif

void EInkDisplay::grayscaleRevert() {
  if (!inGrayscaleMode) {
    return;
  }

  inGrayscaleMode = false;

  
  setCustomLUT(true, lut_grayscale_revert);
  refreshDisplay(FAST_REFRESH);
  setCustomLUT(false);
}

void EInkDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  writeRamBuffer(CMD_WRITE_RAM_BW, lsbBuffer, BUFFER_SIZE);
}

void EInkDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  writeRamBuffer(CMD_WRITE_RAM_RED, msbBuffer, BUFFER_SIZE);
}

void EInkDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  writeRamBuffer(CMD_WRITE_RAM_BW, lsbBuffer, BUFFER_SIZE);
  writeRamBuffer(CMD_WRITE_RAM_RED, msbBuffer, BUFFER_SIZE);
}

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
/**
 * In single buffer mode, this should be called with the previously written BW buffer
 * to reconstruct the RED buffer for proper differential fast refreshes following a
 * grayscale display.
 */
void EInkDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  writeRamBuffer(CMD_WRITE_RAM_RED, bwBuffer, BUFFER_SIZE);
}
#endif

void EInkDisplay::displayBuffer(RefreshMode mode, const bool turnOffScreen) {
  if (!isScreenOn && !turnOffScreen)
  {
    
    mode = HALF_REFRESH;
  }

  
  if (inGrayscaleMode) {
    inGrayscaleMode = false;
    grayscaleRevert();
  }

  
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

  if (mode != FAST_REFRESH) {
    
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, BUFFER_SIZE);
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, BUFFER_SIZE);
  } else {
    
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, BUFFER_SIZE);
    
    
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBufferActive, BUFFER_SIZE);
#endif
  }

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  swapBuffers();
#endif

  
  refreshDisplay(mode, turnOffScreen);

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  
  
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, BUFFER_SIZE);
#endif
}




void EInkDisplay::displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const bool turnOffScreen) {
  if (Serial) Serial.printf("[%lu]   Displaying window at (%d,%d) size (%dx%d)\n", millis(), x, y, w, h);

  
  if (x + w > DISPLAY_WIDTH || y + h > DISPLAY_HEIGHT) {
    if (Serial) Serial.printf("[%lu]   ERROR: Window bounds exceed display dimensions!\n", millis());
    return;
  }

  
  if (x % 8 != 0 || w % 8 != 0) {
    if (Serial) Serial.printf("[%lu]   ERROR: Window x and width must be byte-aligned (multiples of 8)!\n", millis());
    return;
  }

  if (!frameBuffer) {
    if (Serial) Serial.printf("[%lu]   ERROR: Frame buffer not allocated!\n", millis());
    return;
  }

  
  if (inGrayscaleMode) {
    inGrayscaleMode = false;
    grayscaleRevert();
  }

  
  const uint16_t windowWidthBytes = w / 8;
  const uint32_t windowBufferSize = windowWidthBytes * h;

  if (Serial) Serial.printf("[%lu]   Window buffer size: %lu bytes (%d x %d pixels)\n", millis(), windowBufferSize, w, h);

  
  std::vector<uint8_t> windowBuffer(windowBufferSize);

  
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t srcY = y + row;
    const uint16_t srcOffset = srcY * DISPLAY_WIDTH_BYTES + (x / 8);
    const uint16_t dstOffset = row * windowWidthBytes;
    memcpy(&windowBuffer[dstOffset], &frameBuffer[srcOffset], windowWidthBytes);
  }

  
  setRamArea(x, y, w, h);

  
  writeRamBuffer(CMD_WRITE_RAM_BW, windowBuffer.data(), windowBufferSize);

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  
  std::vector<uint8_t> previousWindowBuffer(windowBufferSize);
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t srcY = y + row;
    const uint16_t srcOffset = srcY * DISPLAY_WIDTH_BYTES + (x / 8);
    const uint16_t dstOffset = row * windowWidthBytes;
    memcpy(&previousWindowBuffer[dstOffset], &frameBufferActive[srcOffset], windowWidthBytes);
  }
  writeRamBuffer(CMD_WRITE_RAM_RED, previousWindowBuffer.data(), windowBufferSize);
#endif

  
  refreshDisplay(FAST_REFRESH, turnOffScreen);

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  
  setRamArea(x, y, w, h);
  writeRamBuffer(CMD_WRITE_RAM_RED, windowBuffer.data(), windowBufferSize);
#endif

  if (Serial) Serial.printf("[%lu]   Window display complete\n", millis());
}

void EInkDisplay::displayGrayBuffer(const bool turnOffScreen) {
  drawGrayscale = false;
  inGrayscaleMode = true;

  
  setCustomLUT(true, lut_grayscale);
  refreshDisplay(FAST_REFRESH, turnOffScreen);
  setCustomLUT(false);
}

void EInkDisplay::refreshDisplay(const RefreshMode mode, const bool turnOffScreen) {
  
  sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
  sendData((mode == FAST_REFRESH) ? CTRL1_NORMAL : CTRL1_BYPASS_RED);  

  uint8_t displayMode = 0x00;

  
  if (!isScreenOn) {
    isScreenOn = true;
    displayMode |= 0xC0;  
  }

  
  if (turnOffScreen) {
    isScreenOn = false;
    displayMode |= 0x03;  
  }

  if (mode == FULL_REFRESH) {
    displayMode |= 0x34;
  } else if (mode == HALF_REFRESH) {
    
    sendCommand(CMD_WRITE_TEMP);
    sendData(0x5A);
    displayMode |= 0xD4;
  } else {  
    displayMode |= customLutActive ? 0x0C : 0x1C;
  }

  if (sunlightFadeFixEnabled) {
    applyDriveVoltages(kSunlightFadeDriveVoltages.gate, kSunlightFadeDriveVoltages.source1,
                       kSunlightFadeDriveVoltages.source2, kSunlightFadeDriveVoltages.source3,
                       kSunlightFadeDriveVoltages.vcom);
    sunlightFadeVoltagesApplied = true;
  } else if (sunlightFadeVoltagesApplied || customLutActive) {
    applyDriveVoltages(kNormalDriveVoltages.gate, kNormalDriveVoltages.source1, kNormalDriveVoltages.source2,
                       kNormalDriveVoltages.source3, kNormalDriveVoltages.vcom);
    sunlightFadeVoltagesApplied = false;
  }

  
  const char* refreshType = (mode == FULL_REFRESH) ? "full" : (mode == HALF_REFRESH) ? "half" : "fast";
  if (Serial) Serial.printf("[%lu]   Powering on display 0x%02X (%s refresh)...\n", millis(), displayMode, refreshType);
  sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
  sendData(displayMode);

  sendCommand(CMD_MASTER_ACTIVATION);

  
  if (Serial) Serial.printf("[%lu]   Waiting for display refresh...\n", millis());
  waitWhileBusy(refreshType);
}

void EInkDisplay::setCustomLUT(const bool enabled, const unsigned char* lutData) {
  if (enabled) {
    if (Serial) Serial.printf("[%lu]   Loading custom LUT...\n", millis());

    
    sendCommand(CMD_WRITE_LUT);
    for (uint16_t i = 0; i < 105; i++) {
      sendData(pgm_read_byte(&lutData[i]));
    }

    if (sunlightFadeFixEnabled) {
      applyDriveVoltages(kSunlightFadeDriveVoltages.gate, kSunlightFadeDriveVoltages.source1,
                         kSunlightFadeDriveVoltages.source2, kSunlightFadeDriveVoltages.source3,
                         kSunlightFadeDriveVoltages.vcom);
      sunlightFadeVoltagesApplied = true;
    } else {
      applyDriveVoltages(pgm_read_byte(&lutData[105]), pgm_read_byte(&lutData[106]), pgm_read_byte(&lutData[107]),
                         pgm_read_byte(&lutData[108]), pgm_read_byte(&lutData[109]));
      sunlightFadeVoltagesApplied = false;
    }

    customLutActive = true;
    if (Serial) Serial.printf("[%lu]   Custom LUT loaded\n", millis());
  } else {
    customLutActive = false;
    if (Serial) Serial.printf("[%lu]   Custom LUT disabled\n", millis());
  }
}

void EInkDisplay::setSunlightFadeFixEnabled(const bool enabled) { sunlightFadeFixEnabled = enabled; }

void EInkDisplay::applyDriveVoltages(const uint8_t gate, const uint8_t source1, const uint8_t source2,
                                     const uint8_t source3, const uint8_t vcom) {
  sendCommand(CMD_GATE_VOLTAGE);
  sendData(gate);

  sendCommand(CMD_SOURCE_VOLTAGE);
  sendData(source1);
  sendData(source2);
  sendData(source3);

  sendCommand(CMD_WRITE_VCOM);
  sendData(vcom);
}

void EInkDisplay::deepSleep() {
  if (Serial) Serial.printf("[%lu]   Preparing display for deep sleep...\n", millis());

  
  
  if (isScreenOn) {
    sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
    sendData(CTRL1_BYPASS_RED);  

    sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
    sendData(0x03);  

    sendCommand(CMD_MASTER_ACTIVATION);

    
    waitWhileBusy(" display power-down");

    isScreenOn = false;
  }

  
  if (Serial) Serial.printf("[%lu]   Entering deep sleep mode...\n", millis());
  sendCommand(CMD_DEEP_SLEEP);
  sendData(0x01);  
}

void EInkDisplay::saveFrameBufferAsPBM(const char* filename) {
#ifndef ARDUINO
  const uint8_t* buffer = getFrameBuffer();

  std::ofstream file(filename, std::ios::binary);
  if (!file) {
    if (Serial) Serial.printf("Failed to open %s for writing\n", filename);
    return;
  }

  
  
  
  const int DISPLAY_WIDTH_LOCAL = DISPLAY_WIDTH;    
  const int DISPLAY_HEIGHT_LOCAL = DISPLAY_HEIGHT;  
  const int DISPLAY_WIDTH_BYTES_LOCAL = DISPLAY_WIDTH_LOCAL / 8;

  file << "P4\n";  
  file << DISPLAY_HEIGHT_LOCAL << " " << DISPLAY_WIDTH_LOCAL << "\n";

  
  std::vector<uint8_t> rotatedBuffer((DISPLAY_HEIGHT_LOCAL / 8) * DISPLAY_WIDTH_LOCAL, 0);

  for (int outY = 0; outY < DISPLAY_WIDTH_LOCAL; outY++) {
    for (int outX = 0; outX < DISPLAY_HEIGHT_LOCAL; outX++) {
      int inX = outY;
      int inY = DISPLAY_HEIGHT_LOCAL - 1 - outX;

      int inByteIndex = inY * DISPLAY_WIDTH_BYTES_LOCAL + (inX / 8);
      int inBitPosition = 7 - (inX % 8);
      bool isWhite = (buffer[inByteIndex] >> inBitPosition) & 1;

      int outByteIndex = outY * (DISPLAY_HEIGHT_LOCAL / 8) + (outX / 8);
      int outBitPosition = 7 - (outX % 8);
      if (!isWhite) {  
        rotatedBuffer[outByteIndex] |= (1 << outBitPosition);
      }
    }
  }

  file.write(reinterpret_cast<const char*>(rotatedBuffer.data()), rotatedBuffer.size());
  file.close();
  if (Serial) Serial.printf("Saved framebuffer to %s\n", filename);
#else
  (void)filename;
  if (Serial) Serial.println("saveFrameBufferAsPBM is not supported on Arduino builds.");
#endif
}
