#pragma once

/**
 * @file BitmapUtil.h
 * @brief Public interface and types for BitmapUtil.
 */

#include <cstdint>
#include <cstring>

class Print;

struct BmpHeader;


uint8_t quantize(int gray, int x, int y);
uint8_t quantizeSimple(int gray);
uint8_t quantize1bit(int gray, int x, int y);
int adjustPixel(int gray);

uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b);

bool bmpTo1BitBmpScaled(const char* srcPath, const char* dstPath, int targetMaxWidth, int targetMaxHeight);

enum class BmpRowOrder { BottomUp, TopDown };

void createBmpHeader(BmpHeader* bmpHeader, int width, int height, BmpRowOrder rowOrder);

class Atkinson1BitDitherer {
 public:
  explicit Atkinson1BitDitherer(int width) : width(width) {
    errorRow0 = new int16_t[width + 4]();  
    errorRow1 = new int16_t[width + 4]();  
    errorRow2 = new int16_t[width + 4]();  
  }

  ~Atkinson1BitDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }

  
  Atkinson1BitDitherer(const Atkinson1BitDitherer& other) = delete;

  
  Atkinson1BitDitherer& operator=(const Atkinson1BitDitherer& other) = delete;

  uint8_t processPixel(int gray, int x) {
    
    gray = adjustPixel(gray);

    
    int adjusted = gray + errorRow0[x + 2];
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    
    uint8_t quantized;
    int quantizedValue;
    if (adjusted < 128) {
      quantized = 0;
      quantizedValue = 0;
    } else {
      quantized = 1;
      quantizedValue = 255;
    }

    
    int error = (adjusted - quantizedValue) >> 3;  

    
    errorRow0[x + 3] += error;  
    errorRow0[x + 4] += error;  
    errorRow1[x + 1] += error;  
    errorRow1[x + 2] += error;  
    errorRow1[x + 3] += error;  
    errorRow2[x + 2] += error;  

    return quantized;
  }

  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};







class AtkinsonDitherer {
 public:
  explicit AtkinsonDitherer(int width) : width(width) {
    errorRow0 = new int16_t[width + 4]();  
    errorRow1 = new int16_t[width + 4]();  
    errorRow2 = new int16_t[width + 4]();  
  }

  ~AtkinsonDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }
  
  AtkinsonDitherer(const AtkinsonDitherer& other) = delete;

  
  AtkinsonDitherer& operator=(const AtkinsonDitherer& other) = delete;

  uint8_t processPixel(int gray, int x) {
    
    int adjusted = gray + errorRow0[x + 2];
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    
    uint8_t quantized;
    int quantizedValue;
    if (false) {  
      if (adjusted < 43) {
        quantized = 0;
        quantizedValue = 0;
      } else if (adjusted < 128) {
        quantized = 1;
        quantizedValue = 85;
      } else if (adjusted < 213) {
        quantized = 2;
        quantizedValue = 170;
      } else {
        quantized = 3;
        quantizedValue = 255;
      }
    } else {  
      if (adjusted < 30) {
        quantized = 0;
        quantizedValue = 15;
      } else if (adjusted < 50) {
        quantized = 1;
        quantizedValue = 30;
      } else if (adjusted < 140) {
        quantized = 2;
        quantizedValue = 80;
      } else {
        quantized = 3;
        quantizedValue = 210;
      }
    }

    
    int error = (adjusted - quantizedValue) >> 3;  

    
    errorRow0[x + 3] += error;  
    errorRow0[x + 4] += error;  
    errorRow1[x + 1] += error;  
    errorRow1[x + 2] += error;  
    errorRow1[x + 3] += error;  
    errorRow2[x + 2] += error;  

    return quantized;
  }

  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};









class FloydSteinbergDitherer {
 public:
  explicit FloydSteinbergDitherer(int width) : width(width), rowCount(0) {
    errorCurRow = new int16_t[width + 2]();  
    errorNextRow = new int16_t[width + 2]();
  }

  ~FloydSteinbergDitherer() {
    delete[] errorCurRow;
    delete[] errorNextRow;
  }

  
  FloydSteinbergDitherer(const FloydSteinbergDitherer& other) = delete;

  
  FloydSteinbergDitherer& operator=(const FloydSteinbergDitherer& other) = delete;

  
  
  uint8_t processPixel(int gray, int x) {
    
    int adjusted = gray + errorCurRow[x + 1];

    
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    
    uint8_t quantized;
    int quantizedValue;
    if (adjusted < 43) {
      quantized = 0;
      quantizedValue = 0;
    } else if (adjusted < 128) {
      quantized = 1;
      quantizedValue = 85;
    } else if (adjusted < 213) {
      quantized = 2;
      quantizedValue = 170;
    } else {
      quantized = 3;
      quantizedValue = 255;
    }

    
    int error = adjusted - quantizedValue;

    
    if (!isReverseRow()) {
      
      
      errorCurRow[x + 2] += (error * 7) >> 4;
      
      errorNextRow[x] += (error * 3) >> 4;
      
      errorNextRow[x + 1] += (error * 5) >> 4;
      
      errorNextRow[x + 2] += (error) >> 4;
    } else {
      
      
      errorCurRow[x] += (error * 7) >> 4;
      
      errorNextRow[x + 2] += (error * 3) >> 4;
      
      errorNextRow[x + 1] += (error * 5) >> 4;
      
      errorNextRow[x] += (error) >> 4;
    }

    return quantized;
  }

  
  void nextRow() {
    
    int16_t* temp = errorCurRow;
    errorCurRow = errorNextRow;
    errorNextRow = temp;
    
    memset(errorNextRow, 0, (width + 2) * sizeof(int16_t));
    rowCount++;
  }

  
  bool isReverseRow() const { return (rowCount & 1) != 0; }

  
  void reset() {
    memset(errorCurRow, 0, (width + 2) * sizeof(int16_t));
    memset(errorNextRow, 0, (width + 2) * sizeof(int16_t));
    rowCount = 0;
  }

 private:
  int width;
  int rowCount;
  int16_t* errorCurRow;
  int16_t* errorNextRow;
};



uint8_t epubWebRgb565ToGray8Rounded(uint16_t rgb565LittleEndian);
void epubWebContainDimensionsFloor(int srcW, int srcH, int maxW, int maxH, int* outW, int* outH);
void epubWebWrite2BitBmpHeader(Print& bmpOut, int width, int height);

struct EpubWeb2BitRowPacker {
  int dw = 0;
  int bytesPerRow = 0;
  uint8_t* rowBuffer = nullptr;
  int16_t* errorBuffers = nullptr;
  int rowIndex = 0;

  bool init(int width);
  void freeBuffers();
  bool writeGrayRow(Print& bmpOut, const uint8_t* grayRow);
};
