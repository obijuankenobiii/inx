/**
 * @file JpegToBmpConverter.cpp
 * @brief Definitions for JpegToBmpConverter.
 */

#include "JpegToBmpConverter.h"

#include <HardwareSerial.h>
#include <SdFat.h>
#include <picojpeg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "BitmapUtil.h"
#include "Bitmap.h"

struct JpegReadContext {
  FsFile& file;
  uint8_t buffer[512];
  size_t bufferPos;
  size_t bufferFilled;
};

constexpr bool USE_8BIT_OUTPUT = false;
constexpr bool USE_FLOYD_STEINBERG = true;
constexpr bool USE_PRESCALE = true;
constexpr int TARGET_MAX_WIDTH = 480;   
constexpr int TARGET_MAX_HEIGHT = 800;  

inline void write16(
  Print& out, 
  const uint16_t value) 
{
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
}

inline void write32(
  Print& out, 
  const uint32_t value) 
{
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

inline void write32Signed(
  Print& out, 
  const int32_t value) 
{
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

static void writeBmpHeader1bit(
  Print& bmpOut, 
  const int width, 
  const int height) 
{
  const int bytesPerRow = (width + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 62 + imageSize;
  
  bmpOut.write('B'); 
  bmpOut.write('M');
  write32(bmpOut, fileSize); 
  write32(bmpOut, 0); 
  write32(bmpOut, 62);
  write32(bmpOut, 40); 
  write32Signed(bmpOut, width); 
  write32Signed(bmpOut, -height);
  write16(bmpOut, 1); 
  write16(bmpOut, 1); 
  write32(bmpOut, 0);
  write32(bmpOut, imageSize); 
  write32(bmpOut, 2835); 
  write32(bmpOut, 2835);
  write32(bmpOut, 2); 
  write32(bmpOut, 2);
  
  uint8_t palette[8] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  for (const uint8_t i : palette) bmpOut.write(i);
}

static void writeBmpHeader2bit(
  Print& bmpOut, 
  const int width, 
  const int height) 
{
  const int bytesPerRow = (width * 2 + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  
  bmpOut.write('B'); 
  bmpOut.write('M');
  write32(bmpOut, 70 + imageSize); 
  write32(bmpOut, 0); 
  write32(bmpOut, 70);
  write32(bmpOut, 40); 
  write32Signed(bmpOut, width); 
  write32Signed(bmpOut, -height);
  write16(bmpOut, 1); 
  write16(bmpOut, 2); 
  write32(bmpOut, 0);
  write32(bmpOut, imageSize); 
  write32(bmpOut, 2835); 
  write32(bmpOut, 2835);
  write32(bmpOut, 4); 
  write32(bmpOut, 4);
  
  uint8_t palette[16] = {0,0,0,0, 85,85,85,0, 170,170,170,0, 255,255,255,0};
  for (const uint8_t i : palette) bmpOut.write(i);
}

static bool isUnsupportedJpeg(
  FsFile& file) 
{
  const uint64_t originalPos = file.position();
  file.seek(0);
  uint8_t buf[2];
  bool isProgressive = false;
  
  while (file.read(buf, 1) == 1) 
  {
    if (buf[0] != 0xFF) continue;
    if (file.read(buf, 1) != 1) break;
    
    while (buf[0] == 0xFF) 
    { 
      if (file.read(buf, 1) != 1) break; 
    }
    
    const uint8_t marker = buf[0];
    if (marker == 0xC2 || marker == 0xC9 || marker == 0xCA) 
    { 
      isProgressive = true; 
      break; 
    }
    
    if (marker == 0xC0 || marker == 0xC1) 
    { 
      isProgressive = false; 
      break; 
    }
    
    if (marker != 0xD8 && marker != 0xD9 && marker != 0x01 && !(marker >= 0xD0 && marker <= 0xD7)) 
    {
      if (file.read(buf, 2) != 2) break;
      const uint16_t len = (buf[0] << 8) | buf[1];
      if (len < 2) break;
      file.seek(file.position() + len - 2);
    }
  }
  
  file.seek(originalPos);
  return isProgressive;
}

unsigned char JpegToBmpConverter::jpegReadCallback(
  unsigned char* pBuf, 
  const unsigned char buf_size,
  unsigned char* pBytes_actually_read, 
  void* pCallback_data) 
{
  auto* context = static_cast<JpegReadContext*>(pCallback_data);
  
  if (!context || !context->file) return PJPG_STREAM_READ_ERROR;
  
  if (context->bufferPos >= context->bufferFilled) 
  {
    context->bufferFilled = context->file.read(context->buffer, sizeof(context->buffer));
    context->bufferPos = 0;
    
    if (context->bufferFilled == 0) 
    { 
      *pBytes_actually_read = 0; 
      return 0; 
    }
  }
  
  const size_t available = context->bufferFilled - context->bufferPos;
  const size_t toRead = std::min((size_t)buf_size, available);
  
  memcpy(pBuf, context->buffer + context->bufferPos, toRead);
  context->bufferPos += toRead;
  *pBytes_actually_read = (unsigned char)toRead;
  
  return 0;
}

bool JpegToBmpConverter::jpegFileToBmpStreamInternal(
  FsFile& jpegFile, 
  Print& bmpOut, 
  int targetWidth, 
  int targetHeight,
  bool oneBit, 
  bool quickMode) 
{
  if (isUnsupportedJpeg(jpegFile)) return false;

  JpegReadContext context = {.file = jpegFile, .bufferPos = 0, .bufferFilled = 0};
  pjpeg_image_info_t imageInfo;
  
  if (pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0) != 0) return false;

  int outWidth = imageInfo.m_width;
  int outHeight = imageInfo.m_height;
  uint32_t scaleX_fp = 65536;
  uint32_t scaleY_fp = 65536;

  if (targetWidth > 0 && (imageInfo.m_width > targetWidth || imageInfo.m_height > targetHeight)) 
  {
    float scale = std::min(
      (float)targetWidth / imageInfo.m_width, 
      (float)targetHeight / imageInfo.m_height);
    
    outWidth = (int)(imageInfo.m_width * scale);
    outHeight = (int)(imageInfo.m_height * scale);
    scaleX_fp = (uint32_t)(((uint64_t)imageInfo.m_width << 16) / outWidth);
    scaleY_fp = (uint32_t)(((uint64_t)imageInfo.m_height << 16) / outHeight);
  }

  int bytesPerRow;
  if (oneBit) 
  {
    writeBmpHeader1bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth + 31) / 32 * 4;
  } 
  else 
  {
    writeBmpHeader2bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth * 2 + 31) / 32 * 4;
  }

  
  uint8_t* rowBuffer = (uint8_t*)malloc(bytesPerRow);
  uint8_t* mcuRowBuffer =
      (uint8_t*)malloc(static_cast<size_t>(imageInfo.m_width) * static_cast<size_t>(imageInfo.m_MCUHeight));
  
  
  struct PixelAccumulator {
    uint32_t sum;
    uint16_t count;
  };
  
  PixelAccumulator* accum = new PixelAccumulator[outWidth]();
  
  
  struct ScaleMapEntry {
    uint16_t startX;
    uint16_t endX;
    uint16_t count;
  };
  
  ScaleMapEntry* scaleMap = new ScaleMapEntry[outWidth];
  
  for (int ox = 0; ox < outWidth; ox++) {
    int sxS = (ox * scaleX_fp) >> 16;
    int sxE = ((ox + 1) * scaleX_fp) >> 16;
    if (sxE > imageInfo.m_width) sxE = imageInfo.m_width;
    scaleMap[ox].startX = sxS;
    scaleMap[ox].endX = sxE;
    scaleMap[ox].count = sxE - sxS;
  }
  
  
  uint32_t* outYThresholds = new uint32_t[outHeight + 1];
  for (int oy = 0; oy <= outHeight; oy++) {
    outYThresholds[oy] = (uint32_t)oy * scaleY_fp;
  }

  FloydSteinbergDitherer* ditherer = nullptr;
  Atkinson1BitDitherer* ditherer1bit = nullptr;
  
  if (!oneBit && !quickMode) 
  {
    ditherer = new FloydSteinbergDitherer(outWidth);
  }
  
  if (oneBit) 
  {
    ditherer1bit = new Atkinson1BitDitherer(outWidth);
  }

  int currentOutY = 0;
  int nextOutYIndex = 1; 
  
  
  uint8_t* bitMasks = nullptr;
  uint8_t* shiftAmounts = nullptr;
  
  if (oneBit) {
    
    bitMasks = new uint8_t[outWidth];
    for (int x = 0; x < outWidth; x++) {
      bitMasks[x] = 1 << (7 - (x % 8));
    }
  } else {
    
    shiftAmounts = new uint8_t[outWidth];
    for (int x = 0; x < outWidth; x++) {
      shiftAmounts[x] = 6 - ((x * 2) % 8);
    }
  }

  
  const int mcuWidth = imageInfo.m_MCUWidth;
  const int mcuHeight = imageInfo.m_MCUHeight;
  const int imgWidth = imageInfo.m_width;
  const int imgHeight = imageInfo.m_height;
  const bool isGrayscale = (imageInfo.m_comps == 1);

  
  for (int mcuY = 0; mcuY < imageInfo.m_MCUSPerCol; mcuY++) 
  {
    for (int mcuX = 0; mcuX < imageInfo.m_MCUSPerRow; mcuX++) 
    {
      if (pjpeg_decode_mcu() != 0) break;
      
      if (isGrayscale) {
        
        for (int bY = 0; bY < mcuHeight; bY++) {
          uint8_t* destRow = mcuRowBuffer + bY * imgWidth;
          for (int bX = 0; bX < mcuWidth; bX++) {
            int pX = mcuX * mcuWidth + bX;
            if (pX >= imgWidth) continue;
            
            int off = (bY/8 * (mcuWidth/8) + bX/8)*64 + (bY%8)*8 + (bX%8);
            destRow[pX] = imageInfo.m_pMCUBufR[off];
          }
        }
      } else {
        
        
        const uint8_t* rBuf = imageInfo.m_pMCUBufR;
        const uint8_t* gBuf = imageInfo.m_pMCUBufG;
        const uint8_t* bBuf = imageInfo.m_pMCUBufB;
        
        for (int bY = 0; bY < mcuHeight; bY++) {
          uint8_t* destRow = mcuRowBuffer + bY * imgWidth;
          for (int bX = 0; bX < mcuWidth; bX++) {
            int pX = mcuX * mcuWidth + bX;
            if (pX >= imgWidth) continue;
            
            int off = (bY/8 * (mcuWidth/8) + bX/8)*64 + (bY%8)*8 + (bX%8);
            
            destRow[pX] = (rBuf[off] * 77 + gBuf[off] * 150 + bBuf[off] * 29) >> 8;
          }
        }
      }
    }

    
    for (int y = 0; y < mcuHeight && (mcuY * mcuHeight + y) < imgHeight; y++) 
    {
      uint8_t* srcRow = mcuRowBuffer + y * imgWidth;
      
      
      for (int ox = 0; ox < outWidth; ox++) {
        const ScaleMapEntry& map = scaleMap[ox];
        uint32_t sum = 0;
        uint8_t* srcPtr = srcRow + map.startX;
        
        
        int count = map.count;
        int i = 0;
        for (; i + 3 < count; i += 4) {
          sum += srcPtr[i] + srcPtr[i+1] + srcPtr[i+2] + srcPtr[i+3];
        }
        for (; i < count; i++) {
          sum += srcPtr[i];
        }
        
        accum[ox].sum += sum;
        accum[ox].count += count;
      }

      
      int currentSrcY = mcuY * mcuHeight + y;
      while (nextOutYIndex <= outHeight && currentSrcY >= (outYThresholds[nextOutYIndex] >> 16)) {
        
        memset(rowBuffer, 0, bytesPerRow);
        
        if (oneBit) {
          
          for (int x = 0; x < outWidth; x++) {
            uint8_t gray = (accum[x].count > 0) ? (uint8_t)(accum[x].sum / accum[x].count) : 0;
            uint8_t bit;
            
            if (ditherer1bit) {
              bit = ditherer1bit->processPixel(gray, x);
            } else {
              bit = (gray > 127);
            }
            
            if (bit) {
              rowBuffer[x >> 3] |= bitMasks[x];
            }
          }
        } else {
          
          if (quickMode) {
            
            for (int x = 0; x < outWidth; x++) {
              uint8_t gray = (accum[x].count > 0) ? (uint8_t)(accum[x].sum / accum[x].count) : 0;
              uint8_t twoBit = gray >> 6; 
              int byteIdx = (x * 2) >> 3;
              rowBuffer[byteIdx] |= (twoBit << shiftAmounts[x]);
            }
          } else if (ditherer) {
            
            for (int x = 0; x < outWidth; x++) {
              uint8_t gray = (accum[x].count > 0) ? (uint8_t)(accum[x].sum / accum[x].count) : 0;
              uint8_t twoBit = ditherer->processPixel(gray, x);
              int byteIdx = (x * 2) >> 3;
              rowBuffer[byteIdx] |= (twoBit << shiftAmounts[x]);
            }
          }
        }
        
        bmpOut.write(rowBuffer, bytesPerRow);
        
        if (ditherer) ditherer->nextRow();
        if (ditherer1bit) ditherer1bit->nextRow();
        
        currentOutY++;
        nextOutYIndex++;
        
        
        memset(accum, 0, outWidth * sizeof(PixelAccumulator));
      }
    }
  }
  
  
  free(rowBuffer); 
  free(mcuRowBuffer);
  delete[] accum;
  delete[] scaleMap;
  delete[] outYThresholds;
  
  if (bitMasks) delete[] bitMasks;
  if (shiftAmounts) delete[] shiftAmounts;
  
  if (ditherer) delete ditherer;
  if (ditherer1bit) delete ditherer1bit;
  
  return true;
}

bool JpegToBmpConverter::jpegFileTo1BitBmpStreamCentered(
  FsFile& jpegFile, 
  Print& bmpOut, 
  int targetWidth, 
  int targetHeight,
  bool cropToFill) 
{
  return jpegFileToBmpStreamInternalCentered(
    jpegFile, 
    bmpOut, 
    targetWidth, 
    targetHeight, 
    true, 
    false,
    cropToFill);
}

bool JpegToBmpConverter::jpegFileToBmpStreamCentered(
  FsFile& jpegFile, 
  Print& bmpOut, 
  int targetWidth, 
  int targetHeight,
  bool cropToFill) 
{
  return jpegFileToBmpStreamInternalCentered(
    jpegFile, 
    bmpOut, 
    targetWidth, 
    targetHeight, 
    false, 
    false,
    cropToFill);
}

bool JpegToBmpConverter::jpegFileToBmpStreamInternalCentered(
  FsFile& jpegFile, 
  Print& bmpOut, 
  int targetWidth, 
  int targetHeight,
  bool oneBit, 
  bool quickMode,
  bool cropToFill) 
{
  if (isUnsupportedJpeg(jpegFile)) return false;

  JpegReadContext context = {.file = jpegFile, .bufferPos = 0, .bufferFilled = 0};
  pjpeg_image_info_t imageInfo;
  
  if (pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0) != 0) return false;

  int outWidth = imageInfo.m_width;
  int outHeight = imageInfo.m_height;
  uint32_t scaleX_fp = 65536;
  uint32_t scaleY_fp = 65536;
  int srcOffsetX = 0;
  int srcOffsetY = 0;
  int cropSrcWidth = imageInfo.m_width;
  int cropSrcHeight = imageInfo.m_height;

  if (targetWidth > 0 && targetHeight > 0) 
  {
    float scaleX = (float)targetWidth / imageInfo.m_width;
    float scaleY = (float)targetHeight / imageInfo.m_height;
    if (cropToFill) {
      
      float scale = std::max(scaleX, scaleY);
      cropSrcWidth = (int)(targetWidth / scale);
      cropSrcHeight = (int)(targetHeight / scale);
      srcOffsetX = (imageInfo.m_width - cropSrcWidth) / 2;
      srcOffsetY = (imageInfo.m_height - cropSrcHeight) / 2;
      outWidth = targetWidth;
      outHeight = targetHeight;
    } else {
      
      float scale = std::min(scaleX, scaleY);
      if (scale > 1.0f) {
        scale = 1.0f;
      }
      cropSrcWidth = imageInfo.m_width;
      cropSrcHeight = imageInfo.m_height;
      srcOffsetX = 0;
      srcOffsetY = 0;
      outWidth = std::max(1, static_cast<int>(std::lround(imageInfo.m_width * scale)));
      outHeight = std::max(1, static_cast<int>(std::lround(imageInfo.m_height * scale)));
    }
    scaleX_fp = (uint32_t)(cropSrcWidth << 16) / outWidth;
    scaleY_fp = (uint32_t)(cropSrcHeight << 16) / outHeight;
  }

  int bytesPerRow;
  if (oneBit) 
  {
    writeBmpHeader1bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth + 31) / 32 * 4;
  } 
  else 
  {
    writeBmpHeader2bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth * 2 + 31) / 32 * 4;
  }

  uint8_t* rowBuffer = (uint8_t*)malloc(bytesPerRow);
  uint8_t* mcuRowBuffer =
      (uint8_t*)malloc(static_cast<size_t>(imageInfo.m_width) * static_cast<size_t>(imageInfo.m_MCUHeight));
  uint32_t* rowAccum = new uint32_t[outWidth]();
  uint16_t* rowCount = new uint16_t[outWidth]();
  
  FloydSteinbergDitherer* ditherer = nullptr;
  Atkinson1BitDitherer* ditherer1bit = nullptr;
  
  if (!oneBit && !quickMode) 
  {
    ditherer = new FloydSteinbergDitherer(outWidth);
  }
  
  if (oneBit) 
  {
    ditherer1bit = new Atkinson1BitDitherer(outWidth);
  }

  int currentOutY = 0;
  uint32_t nextOutY_srcStart = scaleY_fp;
  int srcYEnd = srcOffsetY + cropSrcHeight;

  for (int mcuY = 0; mcuY < imageInfo.m_MCUSPerCol; mcuY++) 
  {
    for (int mcuX = 0; mcuX < imageInfo.m_MCUSPerRow; mcuX++) 
    {
      if (pjpeg_decode_mcu() != 0) break;
      
      for (int bY = 0; bY < imageInfo.m_MCUHeight; bY++) 
      {
        for (int bX = 0; bX < imageInfo.m_MCUWidth; bX++) 
        {
          int pX = mcuX * imageInfo.m_MCUWidth + bX;
          if (pX >= imageInfo.m_width) continue;
          
          int off = (bY/8 * (imageInfo.m_MCUWidth/8) + bX/8)*64 + (bY%8)*8 + (bX%8);
          uint8_t g;
          
          if (imageInfo.m_comps == 1) 
          {
            g = imageInfo.m_pMCUBufR[off];
          } 
          else 
          {
            g = rgbToGray(
              imageInfo.m_pMCUBufR[off], 
              imageInfo.m_pMCUBufG[off], 
              imageInfo.m_pMCUBufB[off]);
          }
          
          mcuRowBuffer[bY * imageInfo.m_width + pX] = g;
        }
      }
    }

    for (int y = 0; y < imageInfo.m_MCUHeight && (mcuY * imageInfo.m_MCUHeight + y) < imageInfo.m_height; y++) 
    {
      int currentSrcY = mcuY * imageInfo.m_MCUHeight + y;
      
      if (currentSrcY < srcOffsetY || currentSrcY >= srcYEnd) continue;
      
      uint8_t* srcRow = mcuRowBuffer + y * imageInfo.m_width;
      
      for (int ox = 0; ox < outWidth; ox++) 
      {
        int sx = srcOffsetX + ((ox * scaleX_fp) >> 16);
        int sxEnd = srcOffsetX + cropSrcWidth;
        
        if (sx < imageInfo.m_width && sx >= srcOffsetX && sx < sxEnd) 
        {
          rowAccum[ox] += srcRow[sx];
          rowCount[ox]++;
        }
      }

      if (((uint32_t)((mcuY * imageInfo.m_MCUHeight + y) + 1) << 16) >= nextOutY_srcStart && currentOutY < outHeight) 
      {
        memset(rowBuffer, 0, bytesPerRow);
        
        for (int x = 0; x < outWidth; x++) 
        {
          uint8_t gray = 0;
          if (rowCount[x] > 0) 
          {
            gray = (uint8_t)(rowAccum[x] / rowCount[x]);
          }
          
          if (oneBit) 
          {
            uint8_t bit;
            if (ditherer1bit) 
            {
              bit = ditherer1bit->processPixel(gray, x);
            } 
            else 
            {
              bit = (gray > 127);
            }
            rowBuffer[x / 8] |= (bit << (7 - (x % 8)));
          } 
          else 
          {
            uint8_t twoBit;
            if (quickMode) 
            {
              twoBit = (gray >> 6);
            } 
            else if (ditherer) 
            {
              twoBit = ditherer->processPixel(gray, x);
            } 
            else 
            {
              twoBit = (gray >> 6);
            }
            rowBuffer[(x * 2) / 8] |= (twoBit << (6 - ((x * 2) % 8)));
          }
        }
        
        bmpOut.write(rowBuffer, bytesPerRow);
        
        if (ditherer) ditherer->nextRow();
        if (ditherer1bit) ditherer1bit->nextRow();
        
        currentOutY++;
        memset(rowAccum, 0, outWidth * 4);
        memset(rowCount, 0, outWidth * 2);
        nextOutY_srcStart = (uint32_t)(currentOutY + 1) * scaleY_fp;
      }
    }
  }
  
  free(rowBuffer); 
  free(mcuRowBuffer);
  delete[] rowAccum; 
  delete[] rowCount;
  
  if (ditherer) delete ditherer;
  if (ditherer1bit) delete ditherer1bit;
  
  return true;
}

bool JpegToBmpConverter::jpegFileToBmpStream(
  FsFile& jpegFile, 
  Print& bmpOut) 
{
  return jpegFileToBmpStreamInternal(
    jpegFile, 
    bmpOut, 
    TARGET_MAX_WIDTH, 
    TARGET_MAX_HEIGHT, 
    false);
}

bool JpegToBmpConverter::jpegFileToBmpStreamWithSize(
  FsFile& jpegFile, 
  Print& bmpOut, 
  int targetMaxWidth, 
  int targetMaxHeight) 
{
  return jpegFileToBmpStreamInternal(
    jpegFile, 
    bmpOut, 
    targetMaxWidth, 
    targetMaxHeight, 
    false);
}

bool JpegToBmpConverter::jpegFileTo1BitBmpStream(
  FsFile& jpegFile, 
  Print& bmpOut) 
{
  return jpegFileToBmpStreamInternal(
    jpegFile, 
    bmpOut, 
    TARGET_MAX_WIDTH, 
    TARGET_MAX_HEIGHT, 
    true);
}

bool JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(
  FsFile& jpegFile, 
  Print& bmpOut, 
  int targetMaxWidth, 
  int targetMaxHeight) 
{
  return jpegFileToBmpStreamInternal(
    jpegFile, 
    bmpOut, 
    targetMaxWidth, 
    targetMaxHeight, 
    true);
}

bool JpegToBmpConverter::jpegFileToBmpStreamQuick(
  FsFile& jpegFile, 
  Print& bmpOut, 
  int targetMaxWidth, 
  int targetMaxHeight) 
{
  return jpegFileToBmpStreamInternal(
    jpegFile, 
    bmpOut, 
    targetMaxWidth, 
    targetMaxHeight, 
    false, 
    true);
}

bool JpegToBmpConverter::jpegFileToThumbnailBmp(
  FsFile& jpegFile, 
  Print& bmpOut, 
  int targetMaxWidth, 
  int targetMaxHeight) 
{
  if (isUnsupportedJpeg(jpegFile)) return false;

  JpegReadContext context = {.file = jpegFile, .bufferPos = 0, .bufferFilled = 0};
  pjpeg_image_info_t imageInfo;
  
  if (pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0) != 0) return false;

  
  int outWidth = targetMaxWidth;
  int outHeight = targetMaxHeight;
  uint32_t scaleX_fp = 65536;
  uint32_t scaleY_fp = 65536;
  int srcOffsetX = 0;
  int srcOffsetY = 0;
  int cropSrcWidth = imageInfo.m_width;
  int cropSrcHeight = imageInfo.m_height;

  if (targetMaxWidth > 0 && targetMaxHeight > 0) {
    
    const float scaleX = static_cast<float>(targetMaxWidth) / static_cast<float>(imageInfo.m_width);
    const float scaleY = static_cast<float>(targetMaxHeight) / static_cast<float>(imageInfo.m_height);
    float scale = std::min(scaleX, scaleY);
    if (scale > 1.0f) {
      scale = 1.0f;
    }
    outWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(imageInfo.m_width) * scale)));
    outHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(imageInfo.m_height) * scale)));
    cropSrcWidth = imageInfo.m_width;
    cropSrcHeight = imageInfo.m_height;
    srcOffsetX = 0;
    srcOffsetY = 0;
    scaleX_fp = (uint32_t)(cropSrcWidth << 16) / static_cast<uint32_t>(outWidth);
    scaleY_fp = (uint32_t)(cropSrcHeight << 16) / static_cast<uint32_t>(outHeight);
  }

  
  const int bytesPerRow = (outWidth * 2 + 31) / 32 * 4;
  const int imageSize = bytesPerRow * outHeight;
  
  bmpOut.write('B'); bmpOut.write('M');
  write32(bmpOut, 70 + imageSize);
  write32(bmpOut, 0);
  write32(bmpOut, 70);
  write32(bmpOut, 40);
  write32Signed(bmpOut, outWidth);
  write32Signed(bmpOut, -outHeight);
  write16(bmpOut, 1);
  write16(bmpOut, 2);
  write32(bmpOut, 0);
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);
  write32(bmpOut, 2835);
  write32(bmpOut, 4);
  write32(bmpOut, 4);
  
  
  uint8_t palette[16] = {
      50, 50, 50, 0,     
      96, 96, 96, 0,     
      160, 160, 160, 0,  
      224, 224, 224, 0   
  };
  for (const uint8_t i : palette) bmpOut.write(i);

  
  uint8_t* rowBuffer = (uint8_t*)malloc(bytesPerRow);
  uint8_t* mcuRowBuffer =
      (uint8_t*)malloc(static_cast<size_t>(imageInfo.m_width) * static_cast<size_t>(imageInfo.m_MCUHeight));
  uint32_t* rowAccum = new uint32_t[outWidth]();
  uint16_t* rowCount = new uint16_t[outWidth]();

  
  int16_t* errorBuffer = new int16_t[outWidth * 2]();
  
  int currentOutY = 0;
  uint32_t nextOutY_srcStart = scaleY_fp;
  int srcYEnd = srcOffsetY + cropSrcHeight;

  for (int mcuY = 0; mcuY < imageInfo.m_MCUSPerCol; mcuY++) 
  {
    for (int mcuX = 0; mcuX < imageInfo.m_MCUSPerRow; mcuX++) 
    {
      if (pjpeg_decode_mcu() != 0) break;
      
      for (int bY = 0; bY < imageInfo.m_MCUHeight; bY++) 
      {
        for (int bX = 0; bX < imageInfo.m_MCUWidth; bX++) 
        {
          int pX = mcuX * imageInfo.m_MCUWidth + bX;
          if (pX >= imageInfo.m_width) continue;
          
          int off = (bY/8 * (imageInfo.m_MCUWidth/8) + bX/8)*64 + (bY%8)*8 + (bX%8);
          uint8_t g;
          
          if (imageInfo.m_comps == 1) 
          {
            g = imageInfo.m_pMCUBufR[off];
          } 
          else 
          {
            g = rgbToGray(
              imageInfo.m_pMCUBufR[off], 
              imageInfo.m_pMCUBufG[off], 
              imageInfo.m_pMCUBufB[off]);
          }
          
          mcuRowBuffer[bY * imageInfo.m_width + pX] = g;
        }
      }
    }

    for (int y = 0; y < imageInfo.m_MCUHeight && (mcuY * imageInfo.m_MCUHeight + y) < imageInfo.m_height; y++) 
    {
      int currentSrcY = mcuY * imageInfo.m_MCUHeight + y;
      
      
      if (currentSrcY < srcOffsetY || currentSrcY >= srcYEnd) continue;
      
      uint8_t* srcRow = mcuRowBuffer + y * imageInfo.m_width;
      
      
      for (int ox = 0; ox < outWidth; ox++) 
      {
        
        int sx = srcOffsetX + ((ox * scaleX_fp) >> 16);
        
        
        if (sx >= srcOffsetX && sx < srcOffsetX + cropSrcWidth && sx < imageInfo.m_width) 
        {
          rowAccum[ox] += srcRow[sx];
          rowCount[ox]++;
        }
      }

      
      if (((uint32_t)((mcuY * imageInfo.m_MCUHeight + y) + 1) << 16) >= nextOutY_srcStart && currentOutY < outHeight) 
      {
        memset(rowBuffer, 0, bytesPerRow);
        
        int16_t* currentError = &errorBuffer[(currentOutY & 1) * outWidth];
        int16_t* nextError = &errorBuffer[((currentOutY + 1) & 1) * outWidth];
        
        for (int x = 0; x < outWidth; x++) 
        {
          uint8_t gray = 0;
          if (rowCount[x] > 0) 
          {
            gray = (uint8_t)(rowAccum[x] / rowCount[x]);
          }
          
          
          int16_t corrected = gray + currentError[x];
          if (corrected < 0) corrected = 0;
          if (corrected > 255) corrected = 255;
          
          
          uint8_t twoBit;
          uint8_t quantized;
          
          if (corrected < 48) {
            twoBit = 0; 
            quantized = 0;
          } else if (corrected < 112) {
            twoBit = 1; 
            quantized = 85;
          } else if (corrected < 192) {
            twoBit = 2; 
            quantized = 170;
          } else {
            twoBit = 3; 
            quantized = 255;
          }
          
          
          int16_t error = corrected - quantized;
          
          if (x < outWidth - 1) {
            currentError[x + 1] += (error * 7) / 16;
          }
          nextError[x] += (error * 5) / 16;
          if (x > 0) {
            nextError[x - 1] += (error * 3) / 16;
          }
          if (x < outWidth - 1) {
            nextError[x + 1] += (error * 1) / 16;
          }
          
          
          rowBuffer[(x * 2) / 8] |= (twoBit << (6 - ((x * 2) % 8)));
        }
        
        bmpOut.write(rowBuffer, bytesPerRow);
        
        currentOutY++;
        memset(rowAccum, 0, outWidth * sizeof(uint32_t));
        memset(rowCount, 0, outWidth * sizeof(uint16_t));
        nextOutY_srcStart = (uint32_t)(currentOutY + 1) * scaleY_fp;
        
        if (currentOutY < outHeight) {
          memset(nextError, 0, outWidth * sizeof(int16_t));
        }
      }
    }
  }
  
  free(rowBuffer); 
  free(mcuRowBuffer);
  delete[] rowAccum; 
  delete[] rowCount;
  delete[] errorBuffer;
  
  return true;
}

bool JpegToBmpConverter::jpegFileTo1BitThumbnailBmp(
  FsFile& jpegFile, 
  Print& bmpOut, 
  int targetMaxWidth, 
  int targetMaxHeight) 
{
  if (isUnsupportedJpeg(jpegFile)) return false;

  JpegReadContext context = {.file = jpegFile, .bufferPos = 0, .bufferFilled = 0};
  pjpeg_image_info_t imageInfo;
  
  if (pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0) != 0) return false;

  int outWidth = imageInfo.m_width;
  int outHeight = imageInfo.m_height;
  uint32_t scaleX_fp = 65536;
  uint32_t scaleY_fp = 65536;

  if (targetMaxWidth > 0 && (imageInfo.m_width > targetMaxWidth || imageInfo.m_height > targetMaxHeight)) 
  {
    float scale = std::min(
      (float)targetMaxWidth / imageInfo.m_width, 
      (float)targetMaxHeight / imageInfo.m_height);
    
    outWidth = (int)(imageInfo.m_width * scale);
    outHeight = (int)(imageInfo.m_height * scale);
    scaleX_fp = (uint32_t)(imageInfo.m_width << 16) / outWidth;
    scaleY_fp = (uint32_t)(imageInfo.m_height << 16) / outHeight;
  }

  
  const int bytesPerRow = (outWidth + 31) / 32 * 4;
  const int imageSize = bytesPerRow * outHeight;
  
  bmpOut.write('B'); bmpOut.write('M');
  write32(bmpOut, 62 + imageSize);
  write32(bmpOut, 0);
  write32(bmpOut, 62);
  write32(bmpOut, 40);
  write32Signed(bmpOut, outWidth);
  write32Signed(bmpOut, -outHeight);
  write16(bmpOut, 1);
  write16(bmpOut, 1);
  write32(bmpOut, 0);
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);
  write32(bmpOut, 2835);
  write32(bmpOut, 2);
  write32(bmpOut, 2);
  
  uint8_t palette[8] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  for (const uint8_t i : palette) bmpOut.write(i);

  uint8_t* rowBuffer = (uint8_t*)malloc(bytesPerRow);
  uint8_t* mcuRowBuffer =
      (uint8_t*)malloc(static_cast<size_t>(imageInfo.m_width) * static_cast<size_t>(imageInfo.m_MCUHeight));
  uint32_t* rowAccum = new uint32_t[outWidth]();
  uint16_t* rowCount = new uint16_t[outWidth]();
  
  
  int16_t* errorBuffer = new int16_t[outWidth * 2]();

  int currentOutY = 0;
  uint32_t nextOutY_srcStart = scaleY_fp;

  for (int mcuY = 0; mcuY < imageInfo.m_MCUSPerCol; mcuY++) 
  {
    for (int mcuX = 0; mcuX < imageInfo.m_MCUSPerRow; mcuX++) 
    {
      if (pjpeg_decode_mcu() != 0) break;
      
      for (int bY = 0; bY < imageInfo.m_MCUHeight; bY++) 
      {
        for (int bX = 0; bX < imageInfo.m_MCUWidth; bX++) 
        {
          int pX = mcuX * imageInfo.m_MCUWidth + bX;
          if (pX >= imageInfo.m_width) continue;
          
          int off = (bY/8 * (imageInfo.m_MCUWidth/8) + bX/8)*64 + (bY%8)*8 + (bX%8);
          uint8_t g;
          
          if (imageInfo.m_comps == 1) 
          {
            g = imageInfo.m_pMCUBufR[off];
          } 
          else 
          {
            g = rgbToGray(
              imageInfo.m_pMCUBufR[off], 
              imageInfo.m_pMCUBufG[off], 
              imageInfo.m_pMCUBufB[off]);
          }
          
          mcuRowBuffer[bY * imageInfo.m_width + pX] = g;
        }
      }
    }

    for (int y = 0; y < imageInfo.m_MCUHeight && (mcuY * imageInfo.m_MCUHeight + y) < imageInfo.m_height; y++) 
    {
      uint8_t* srcRow = mcuRowBuffer + y * imageInfo.m_width;
      
      for (int ox = 0; ox < outWidth; ox++) 
      {
        int sxS = (ox * scaleX_fp) >> 16;
        int sxE = ((ox + 1) * scaleX_fp) >> 16;
        
        for (int sx = sxS; sx < sxE && sx < imageInfo.m_width; sx++) 
        {
          rowAccum[ox] += srcRow[sx];
          rowCount[ox]++;
        }
      }

      if (((uint32_t)((mcuY * imageInfo.m_MCUHeight + y) + 1) << 16) >= nextOutY_srcStart && currentOutY < outHeight) 
      {
        memset(rowBuffer, 0, bytesPerRow);
        
        int16_t* currentError = &errorBuffer[(currentOutY & 1) * outWidth];
        int16_t* nextError = &errorBuffer[((currentOutY + 1) & 1) * outWidth];
        
        for (int x = 0; x < outWidth; x++) 
        {
          uint8_t gray = 0;
          if (rowCount[x] > 0) 
          {
            gray = (uint8_t)(rowAccum[x] / rowCount[x]);
          }
          
          
          int16_t corrected = gray + currentError[x];
          if (corrected < 0) corrected = 0;
          if (corrected > 255) corrected = 255;
          
          
          uint8_t bit;
          uint8_t quantized;
          
          if (corrected < 128) {
            bit = 1; 
            quantized = 0;
          } else {
            bit = 0; 
            quantized = 255;
          }
          
          
          int16_t error = corrected - quantized;
          
          if (x < outWidth - 1) {
            currentError[x + 1] += (error * 7) / 16;
          }
          nextError[x] += (error * 5) / 16;
          if (x > 0) {
            nextError[x - 1] += (error * 3) / 16;
          }
          if (x < outWidth - 1) {
            nextError[x + 1] += (error * 1) / 16;
          }
          
          rowBuffer[x / 8] |= (bit << (7 - (x % 8)));
        }
        
        bmpOut.write(rowBuffer, bytesPerRow);
        
        currentOutY++;
        memset(rowAccum, 0, outWidth * 4);
        memset(rowCount, 0, outWidth * 2);
        nextOutY_srcStart = (uint32_t)(currentOutY + 1) * scaleY_fp;
        
        if (currentOutY < outHeight) {
          memset(nextError, 0, outWidth * sizeof(int16_t));
        }
      }
    }
  }
  
  free(rowBuffer); 
  free(mcuRowBuffer);
  delete[] rowAccum; 
  delete[] rowCount;
  delete[] errorBuffer;
  
  return true;
}

/**
 * Decodes a JPEG but only processes the TOP portion of the source image,
 * scaling it to fill the targetMaxWidth/Height.
 * * @param verticalCropPercent 0.5f for top half, 0.75f for top three-quarters, etc.
 */
bool JpegToBmpConverter::jpegFileToTopCropBmp(
  FsFile& jpegFile, 
  Print& bmpOut, 
  int targetMaxWidth, 
  int targetMaxHeight,
  float verticalCropPercent) 
{
  if (isUnsupportedJpeg(jpegFile)) return false;

  JpegReadContext context = {.file = jpegFile, .bufferPos = 0, .bufferFilled = 0};
  pjpeg_image_info_t imageInfo;
  
  if (pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0) != 0) return false;

  int cropSrcWidth = imageInfo.m_width;
  int cropSrcHeight = (int)(imageInfo.m_height * verticalCropPercent);
  
  if (cropSrcHeight <= 0) return false;

  
  uint32_t scaleX_fp = (uint32_t)(((uint64_t)cropSrcWidth << 16) / targetMaxWidth);
  uint32_t scaleY_fp = (uint32_t)(((uint64_t)cropSrcHeight << 16) / targetMaxHeight);

  
  const int bytesPerRow = (targetMaxWidth * 2 + 31) / 32 * 4;
  const int imageSize = bytesPerRow * targetMaxHeight;
  
  bmpOut.write('B'); bmpOut.write('M');
  write32(bmpOut, 70 + imageSize);
  write32(bmpOut, 0); write32(bmpOut, 70); write32(bmpOut, 40);
  write32Signed(bmpOut, targetMaxWidth);
  write32Signed(bmpOut, -targetMaxHeight);
  write16(bmpOut, 1); write16(bmpOut, 2);
  write32(bmpOut, 0); write32(bmpOut, imageSize);
  write32(bmpOut, 2835); write32(bmpOut, 2835);
  write32(bmpOut, 4); write32(bmpOut, 4);
  
  uint8_t palette[16] = {0,0,0,0, 85,85,85,0, 170,170,170,0, 255,255,255,0};
  for (uint8_t i : palette) bmpOut.write(i);

  
  uint8_t* rowBuffer = (uint8_t*)malloc(bytesPerRow);
  uint8_t* mcuRowBuffer =
      (uint8_t*)malloc(static_cast<size_t>(imageInfo.m_width) * static_cast<size_t>(imageInfo.m_MCUHeight));
  int16_t* errorBuffer = new int16_t[targetMaxWidth * 2]();
  
  
  struct ScaleMapEntry {
    uint16_t srcX;
  };
  
  ScaleMapEntry* scaleMap = new ScaleMapEntry[targetMaxWidth];
  for (int x = 0; x < targetMaxWidth; x++) {
    scaleMap[x].srcX = (x * scaleX_fp) >> 16;
    
    if (scaleMap[x].srcX >= cropSrcWidth) scaleMap[x].srcX = cropSrcWidth - 1;
  }

  
  uint8_t* fadeFactors = new uint8_t[targetMaxHeight];
  int gradientStartRow = (targetMaxHeight * 80) / 100;
  int gradientZoneHeight = targetMaxHeight - gradientStartRow;
  
  for (int row = 0; row < targetMaxHeight; row++) {
    if (row > gradientStartRow) {
      fadeFactors[row] = ((row - gradientStartRow) * 128) / gradientZoneHeight;
    } else {
      fadeFactors[row] = 0;
    }
  }

  
  const int mcuWidth = imageInfo.m_MCUWidth;
  const int mcuHeight = imageInfo.m_MCUHeight;
  const int imgWidth = imageInfo.m_width;
  const int imgHeight = imageInfo.m_height;
  const int mcusPerRow = imageInfo.m_MCUSPerRow;
  const bool isGrayscale = (imageInfo.m_comps == 1);
  
  int currentOutY = 0;
  int srcYLimit = cropSrcHeight;
  
  
  uint32_t* srcYThresholds = new uint32_t[targetMaxHeight + 1];
  for (int oy = 0; oy <= targetMaxHeight; oy++) {
    srcYThresholds[oy] = (uint32_t)oy * scaleY_fp;
  }
  int nextSrcYIndex = 1;

  for (int mcuY = 0; mcuY < imageInfo.m_MCUSPerCol; mcuY++) 
  {
    if (mcuY * mcuHeight >= srcYLimit) break;

    
    for (int mcuX = 0; mcuX < mcusPerRow; mcuX++) 
    {
      if (pjpeg_decode_mcu() != 0) break;
      
      
      if (isGrayscale) {
        
        for (int bY = 0; bY < mcuHeight; bY++) {
          uint8_t* destRow = mcuRowBuffer + bY * imgWidth;
          for (int bX = 0; bX < mcuWidth; bX++) {
            int pX = mcuX * mcuWidth + bX;
            if (pX >= imgWidth) continue;
            int off = (bY/8 * (mcuWidth/8) + bX/8)*64 + (bY%8)*8 + (bX%8);
            destRow[pX] = imageInfo.m_pMCUBufR[off];
          }
        }
      } else {
        
        const uint8_t* rBuf = imageInfo.m_pMCUBufR;
        const uint8_t* gBuf = imageInfo.m_pMCUBufG;
        const uint8_t* bBuf = imageInfo.m_pMCUBufB;
        
        for (int bY = 0; bY < mcuHeight; bY++) {
          uint8_t* destRow = mcuRowBuffer + bY * imgWidth;
          for (int bX = 0; bX < mcuWidth; bX++) {
            int pX = mcuX * mcuWidth + bX;
            if (pX >= imgWidth) continue;
            int off = (bY/8 * (mcuWidth/8) + bX/8)*64 + (bY%8)*8 + (bX%8);
            
            destRow[pX] = (rBuf[off] * 77 + gBuf[off] * 150 + bBuf[off] * 29) >> 8;
          }
        }
      }
    }

    
    for (int y = 0; y < mcuHeight; y++) 
    {
      int currentSrcY = mcuY * mcuHeight + y;
      if (currentSrcY >= srcYLimit || currentSrcY >= imgHeight) break;

      uint8_t* srcRow = mcuRowBuffer + y * imgWidth;

      
      while (nextSrcYIndex <= targetMaxHeight && currentSrcY >= (srcYThresholds[nextSrcYIndex] >> 16)) 
      {
        memset(rowBuffer, 0, bytesPerRow);
        
        int16_t* currentError = &errorBuffer[(currentOutY & 1) * targetMaxWidth];
        int16_t* nextError = &errorBuffer[((currentOutY + 1) & 1) * targetMaxWidth];
        
        uint8_t fadeAmount = fadeFactors[currentOutY];
        
        
        int invFade = 255 - fadeAmount;
        
        for (int x = 0; x < targetMaxWidth; x++) 
        {
          int sx = scaleMap[x].srcX;
          uint16_t gray = srcRow[sx];

          
          if (fadeAmount > 0) {
            
            gray = (gray * invFade + (224 * fadeAmount)) >> 8;
          }

          int16_t corrected = (int16_t)gray + currentError[x];
          if (corrected < 0) corrected = 0; 
          if (corrected > 255) corrected = 255;

          
          uint8_t twoBit;
          uint8_t quantized;
          
          
          twoBit = (corrected >> 6) & 0x03;  
          
          
          static const uint8_t quantValues[4] = {0, 85, 170, 255};
          quantized = quantValues[twoBit];

          int16_t err = corrected - quantized;
          
          
          if (x < targetMaxWidth - 1) currentError[x+1] += (err * 7) / 16;
          nextError[x] += (err * 5) / 16;
          if (x > 0) nextError[x-1] += (err * 3) / 16;
          if (x < targetMaxWidth - 1) nextError[x+1] += (err * 1) / 16;

          
          int byteIdx = (x * 2) >> 3;
          int bitShift = 6 - ((x * 2) & 7);
          rowBuffer[byteIdx] |= (twoBit << bitShift);
        }

        bmpOut.write(rowBuffer, bytesPerRow);
        currentOutY++;
        nextSrcYIndex++;
        
        if (currentOutY < targetMaxHeight) {
          memset(nextError, 0, targetMaxWidth * sizeof(int16_t));
        }
      }
    }
  }

  
  free(rowBuffer); 
  free(mcuRowBuffer);
  delete[] errorBuffer;
  delete[] scaleMap;
  delete[] fadeFactors;
  delete[] srcYThresholds;
  
  return true;
}

bool JpegToBmpConverter::jpegFileToEpubWebStyle2BitBmpStream(FsFile& jpegFile, Print& bmpOut) {
  if (isUnsupportedJpeg(jpegFile)) return false;

  JpegReadContext context = {.file = jpegFile, .bufferPos = 0, .bufferFilled = 0};
  pjpeg_image_info_t imageInfo;
  if (pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0) != 0) return false;

  const int sw = imageInfo.m_width;
  const int sh = imageInfo.m_height;
  if (sw <= 0 || sh <= 0) return false;

  int dw = 0;
  int dh = 0;
  epubWebContainDimensionsFloor(sw, sh, 500, 820, &dw, &dh);

  epubWebWrite2BitBmpHeader(bmpOut, dw, dh);

  const int mcuWidth = imageInfo.m_MCUWidth;
  const int mcuHeight = imageInfo.m_MCUHeight;
  const int imgWidth = imageInfo.m_width;
  const int imgHeight = imageInfo.m_height;
  const bool isGrayscale = (imageInfo.m_comps == 1);
  const int mcusPerRow = imageInfo.m_MCUSPerRow;

  uint8_t* mcuRowBuffer =
      (uint8_t*)malloc(static_cast<size_t>(imgWidth) * static_cast<size_t>(mcuHeight));
  uint8_t* grayDw = (uint8_t*)malloc(static_cast<size_t>(dw));
  if (!mcuRowBuffer || !grayDw) {
    free(mcuRowBuffer);
    free(grayDw);
    return false;
  }

  EpubWeb2BitRowPacker packer;
  if (!packer.init(dw)) {
    free(mcuRowBuffer);
    free(grayDw);
    return false;
  }

  auto decodeStripe = [&]() -> bool {
    for (int mcuX = 0; mcuX < mcusPerRow; mcuX++) {
      if (pjpeg_decode_mcu() != 0) return false;

      if (isGrayscale) {
        for (int bY = 0; bY < mcuHeight; bY++) {
          uint8_t* destRow = mcuRowBuffer + bY * imgWidth;
          for (int bX = 0; bX < mcuWidth; bX++) {
            int pX = mcuX * mcuWidth + bX;
            if (pX >= imgWidth) continue;

            int off = (bY / 8 * (mcuWidth / 8) + bX / 8) * 64 + (bY % 8) * 8 + (bX % 8);
            destRow[pX] = imageInfo.m_pMCUBufR[off];
          }
        }
      } else {
        const uint8_t* rBuf = imageInfo.m_pMCUBufR;
        const uint8_t* gBuf = imageInfo.m_pMCUBufG;
        const uint8_t* bBuf = imageInfo.m_pMCUBufB;

        for (int bY = 0; bY < mcuHeight; bY++) {
          uint8_t* destRow = mcuRowBuffer + bY * imgWidth;
          for (int bX = 0; bX < mcuWidth; bX++) {
            int pX = mcuX * mcuWidth + bX;
            if (pX >= imgWidth) continue;

            int off = (bY / 8 * (mcuWidth / 8) + bX / 8) * 64 + (bY % 8) * 8 + (bX % 8);
            destRow[pX] = (rBuf[off] * 77 + gBuf[off] * 150 + bBuf[off] * 29) >> 8;
          }
        }
      }
    }
    return true;
  };

  int stripeMcuY = -1;

  for (int oy = 0; oy < dh; oy++) {
    const int sy = (dh <= 1) ? 0 : std::min(sh - 1, (oy * sh) / dh);

    while (stripeMcuY < 0 || sy < stripeMcuY * mcuHeight || sy >= stripeMcuY * mcuHeight + mcuHeight) {
      stripeMcuY++;
      if (stripeMcuY >= imageInfo.m_MCUSPerCol) {
        packer.freeBuffers();
        free(mcuRowBuffer);
        free(grayDw);
        return false;
      }
      if (!decodeStripe()) {
        packer.freeBuffers();
        free(mcuRowBuffer);
        free(grayDw);
        return false;
      }
    }

    const int rowInStripe = sy - stripeMcuY * mcuHeight;
    if (rowInStripe < 0 || rowInStripe >= mcuHeight || sy >= imgHeight) {
      packer.freeBuffers();
      free(mcuRowBuffer);
      free(grayDw);
      return false;
    }

    const uint8_t* srcRow = mcuRowBuffer + rowInStripe * imgWidth;
    for (int ox = 0; ox < dw; ox++) {
      const int sx = (dw <= 1) ? 0 : std::min(sw - 1, (ox * sw) / dw);
      grayDw[ox] = srcRow[sx];
    }

    if (!packer.writeGrayRow(bmpOut, grayDw)) {
      packer.freeBuffers();
      free(mcuRowBuffer);
      free(grayDw);
      return false;
    }
  }

  packer.freeBuffers();
  free(mcuRowBuffer);
  free(grayDw);
  return true;
}

bool JpegToBmpConverter::resizeBitmap(FsFile& bmpFile, Print& bmpOut, int targetWidth, int targetHeight) {
  Bitmap bitmap(bmpFile);
  
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    return false;
  }
  
  int originalWidth = bitmap.getWidth();
  int originalHeight = bitmap.getHeight();
  
  float scale = std::min((float)targetWidth / originalWidth, (float)targetHeight / originalHeight);
  
  int finalWidth = originalWidth * scale;
  int finalHeight = originalHeight * scale;
  
  
  uint8_t header[70] = {0};
  header[0] = 'B';
  header[1] = 'M';
  int rowSize = ((finalWidth * 2 + 31) / 32) * 4;
  int fileSize = 70 + (rowSize * finalHeight);
  *(int*)&header[2] = fileSize;
  header[10] = 70;
  *(int*)&header[14] = 40;
  *(int*)&header[18] = finalWidth;
  *(int*)&header[22] = -finalHeight;  
  *(short*)&header[26] = 1;
  *(short*)&header[28] = 2;
  *(int*)&header[34] = rowSize * finalHeight;
  *(int*)&header[46] = 4;
  
  uint32_t palette[4] = {0x00000000, 0x55555500, 0xAAAAAA00, 0xFFFFFF00};
  memcpy(&header[54], palette, 16);
  
  bmpOut.write(header, 70);
  
  int outputRowSize = (originalWidth + 3) / 4;
  uint8_t* outputRow = new uint8_t[outputRowSize];
  uint8_t* rowBytes = new uint8_t[bitmap.getRowBytes()];
  uint8_t* finalRow = new uint8_t[rowSize];
  
  bitmap.rewindToData();
  
  for (int bmpY = 0; bmpY < originalHeight; bmpY++) {
    bitmap.readNextRow(outputRow, rowBytes);
    
    int destY = bmpY * scale;
    if (destY >= finalHeight) continue;
    
    if (destY == 0 || destY != (int)((bmpY-1) * scale)) {
      memset(finalRow, 0, rowSize);
    }
    
    for (int bmpX = 0; bmpX < originalWidth; bmpX++) {
      int destX = bmpX * scale;
      if (destX >= finalWidth) continue;
      
      uint8_t val = (outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x03;
      
      int bytePos = destX / 4;
      int shift = 6 - ((destX % 4) * 2);
      finalRow[bytePos] |= (val << shift);
    }
    
    if (destY == finalHeight - 1 || (int)((bmpY+1) * scale) > destY) {
      bmpOut.write(finalRow, rowSize);
    }
  }
  
  delete[] outputRow;
  delete[] rowBytes;
  delete[] finalRow;
  
  return true;
}