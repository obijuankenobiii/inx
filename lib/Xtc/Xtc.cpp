/**
 * @file Xtc.cpp
 * @brief Definitions for Xtc.
 */

/**
 * Xtc.cpp
 *
 * Main XTC ebook class implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "Xtc.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>

bool Xtc::load() {
  Serial.printf("[%lu] [XTC] Loading XTC: %s\n", millis(), filepath.c_str());

  
  parser.reset(new xtc::XtcParser());

  
  xtc::XtcError err = parser->open(filepath.c_str());
  if (err != xtc::XtcError::OK) {
    Serial.printf("[%lu] [XTC] Failed to load: %s\n", millis(), xtc::errorToString(err));
    parser.reset();
    return false;
  }

  loaded = true;
  Serial.printf("[%lu] [XTC] Loaded XTC: %s (%lu pages)\n", millis(), filepath.c_str(), parser->getPageCount());
  return true;
}

bool Xtc::clearCache() const {
  if (!SdMan.exists(cachePath.c_str())) {
    Serial.printf("[%lu] [XTC] Cache does not exist, no action needed\n", millis());
    return true;
  }

  if (!SdMan.removeDir(cachePath.c_str())) {
    Serial.printf("[%lu] [XTC] Failed to clear cache\n", millis());
    return false;
  }

  Serial.printf("[%lu] [XTC] Cache cleared successfully\n", millis());
  return true;
}

void Xtc::setupCacheDir() const {
  if (SdMan.exists(cachePath.c_str())) {
    return;
  }

  
  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') {
      SdMan.mkdir(cachePath.substr(0, i).c_str());
    }
  }
  SdMan.mkdir(cachePath.c_str());
}

std::string Xtc::getTitle() const {
  if (!loaded || !parser) {
    return "";
  }

  
  std::string title = parser->getTitle();
  if (!title.empty()) {
    return title;
  }

  
  size_t lastSlash = filepath.find_last_of('/');
  size_t lastDot = filepath.find_last_of('.');

  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    lastSlash++;
  }

  if (lastDot == std::string::npos || lastDot <= lastSlash) {
    return filepath.substr(lastSlash);
  }

  return filepath.substr(lastSlash, lastDot - lastSlash);
}

std::string Xtc::getAuthor() const {
  if (!loaded || !parser) {
    return "";
  }

  
  return parser->getAuthor();
}

bool Xtc::hasChapters() const {
  if (!loaded || !parser) {
    return false;
  }
  return parser->hasChapters();
}

const std::vector<xtc::ChapterInfo>& Xtc::getChapters() const {
  static const std::vector<xtc::ChapterInfo> kEmpty;
  if (!loaded || !parser) {
    return kEmpty;
  }
  return parser->getChapters();
}

std::string Xtc::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Xtc::generateCoverBmp() const {
  
  if (SdMan.exists(getCoverBmpPath().c_str())) {
    return true;
  }

  if (!loaded || !parser) {
    Serial.printf("[%lu] [XTC] Cannot generate cover BMP, file not loaded\n", millis());
    return false;
  }

  if (parser->getPageCount() == 0) {
    Serial.printf("[%lu] [XTC] No pages in XTC file\n", millis());
    return false;
  }

  
  setupCacheDir();

  
  xtc::PageInfo pageInfo;
  if (!parser->getPageInfo(0, pageInfo)) {
    Serial.printf("[%lu] [XTC] Failed to get first page info\n", millis());
    return false;
  }

  
  const uint8_t bitDepth = parser->getBitDepth();

  
  
  
  size_t bitmapSize;
  if (bitDepth == 2) {
    bitmapSize = ((static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8) * 2;
  } else {
    bitmapSize = ((pageInfo.width + 7) / 8) * pageInfo.height;
  }
  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(bitmapSize));
  if (!pageBuffer) {
    Serial.printf("[%lu] [XTC] Failed to allocate page buffer (%lu bytes)\n", millis(), bitmapSize);
    return false;
  }

  
  size_t bytesRead = const_cast<xtc::XtcParser*>(parser.get())->loadPage(0, pageBuffer, bitmapSize);
  if (bytesRead == 0) {
    Serial.printf("[%lu] [XTC] Failed to load cover page\n", millis());
    free(pageBuffer);
    return false;
  }

  
  FsFile coverBmp;
  if (!SdMan.openFileForWrite("XTC", getCoverBmpPath(), coverBmp)) {
    Serial.printf("[%lu] [XTC] Failed to create cover BMP file\n", millis());
    free(pageBuffer);
    return false;
  }

  
  
  const uint32_t rowSize = ((pageInfo.width + 31) / 32) * 4;  
  const uint32_t imageSize = rowSize * pageInfo.height;
  const uint32_t fileSize = 14 + 40 + 8 + imageSize;  

  
  coverBmp.write('B');
  coverBmp.write('M');
  coverBmp.write(reinterpret_cast<const uint8_t*>(&fileSize), 4);
  uint32_t reserved = 0;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&reserved), 4);
  uint32_t dataOffset = 14 + 40 + 8;  
  coverBmp.write(reinterpret_cast<const uint8_t*>(&dataOffset), 4);

  
  uint32_t dibHeaderSize = 40;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&dibHeaderSize), 4);
  int32_t width = pageInfo.width;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&width), 4);
  int32_t height = -static_cast<int32_t>(pageInfo.height);  
  coverBmp.write(reinterpret_cast<const uint8_t*>(&height), 4);
  uint16_t planes = 1;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&planes), 2);
  uint16_t bitsPerPixel = 1;  
  coverBmp.write(reinterpret_cast<const uint8_t*>(&bitsPerPixel), 2);
  uint32_t compression = 0;  
  coverBmp.write(reinterpret_cast<const uint8_t*>(&compression), 4);
  coverBmp.write(reinterpret_cast<const uint8_t*>(&imageSize), 4);
  int32_t ppmX = 2835;  
  coverBmp.write(reinterpret_cast<const uint8_t*>(&ppmX), 4);
  int32_t ppmY = 2835;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&ppmY), 4);
  uint32_t colorsUsed = 2;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&colorsUsed), 4);
  uint32_t colorsImportant = 2;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&colorsImportant), 4);

  
  
  
  uint8_t black[4] = {0x00, 0x00, 0x00, 0x00};
  coverBmp.write(black, 4);
  
  uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0x00};
  coverBmp.write(white, 4);

  
  
  const size_t dstRowSize = (pageInfo.width + 7) / 8;  

  if (bitDepth == 2) {
    
    
    
    
    
    const size_t planeSize = (static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8;
    const uint8_t* plane1 = pageBuffer;                 
    const uint8_t* plane2 = pageBuffer + planeSize;     
    const size_t colBytes = (pageInfo.height + 7) / 8;  

    
    uint8_t* rowBuffer = static_cast<uint8_t*>(malloc(dstRowSize));
    if (!rowBuffer) {
      free(pageBuffer);
      coverBmp.close();
      return false;
    }

    for (uint16_t y = 0; y < pageInfo.height; y++) {
      memset(rowBuffer, 0xFF, dstRowSize);  

      for (uint16_t x = 0; x < pageInfo.width; x++) {
        
        const size_t colIndex = pageInfo.width - 1 - x;
        const size_t byteInCol = y / 8;
        const size_t bitInByte = 7 - (y % 8);  

        const size_t byteOffset = colIndex * colBytes + byteInCol;
        const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
        const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
        const uint8_t pixelValue = (bit1 << 1) | bit2;

        
        if (pixelValue >= 1) {
          
          const size_t dstByte = x / 8;
          const size_t dstBit = 7 - (x % 8);
          rowBuffer[dstByte] &= ~(1 << dstBit);
        }
      }

      
      coverBmp.write(rowBuffer, dstRowSize);

      
      uint8_t padding[4] = {0, 0, 0, 0};
      size_t paddingSize = rowSize - dstRowSize;
      if (paddingSize > 0) {
        coverBmp.write(padding, paddingSize);
      }
    }

    free(rowBuffer);
  } else {
    
    const size_t srcRowSize = (pageInfo.width + 7) / 8;

    for (uint16_t y = 0; y < pageInfo.height; y++) {
      
      coverBmp.write(pageBuffer + y * srcRowSize, srcRowSize);

      
      uint8_t padding[4] = {0, 0, 0, 0};
      size_t paddingSize = rowSize - srcRowSize;
      if (paddingSize > 0) {
        coverBmp.write(padding, paddingSize);
      }
    }
  }

  coverBmp.close();
  free(pageBuffer);

  Serial.printf("[%lu] [XTC] Generated cover BMP: %s\n", millis(), getCoverBmpPath().c_str());
  return true;
}

std::string Xtc::getThumbBmpPath() const { return cachePath + "/thumb.bmp"; }

bool Xtc::generateThumbBmp() const {
  
  if (SdMan.exists(getThumbBmpPath().c_str())) {
    return true;
  }

  if (!loaded || !parser) {
    Serial.printf("[%lu] [XTC] Cannot generate thumb BMP, file not loaded\n", millis());
    return false;
  }

  if (parser->getPageCount() == 0) {
    Serial.printf("[%lu] [XTC] No pages in XTC file\n", millis());
    return false;
  }

  
  setupCacheDir();

  
  xtc::PageInfo pageInfo;
  if (!parser->getPageInfo(0, pageInfo)) {
    Serial.printf("[%lu] [XTC] Failed to get first page info\n", millis());
    return false;
  }

  
  const uint8_t bitDepth = parser->getBitDepth();

  
  constexpr int THUMB_TARGET_WIDTH = 240;
  constexpr int THUMB_TARGET_HEIGHT = 400;

  
  float scaleX = static_cast<float>(THUMB_TARGET_WIDTH) / pageInfo.width;
  float scaleY = static_cast<float>(THUMB_TARGET_HEIGHT) / pageInfo.height;
  float scale = (scaleX < scaleY) ? scaleX : scaleY;

  
  if (scale >= 1.0f) {
    
    
    if (generateCoverBmp()) {
      FsFile src, dst;
      if (SdMan.openFileForRead("XTC", getCoverBmpPath(), src)) {
        if (SdMan.openFileForWrite("XTC", getThumbBmpPath(), dst)) {
          uint8_t buffer[512];
          while (src.available()) {
            size_t bytesRead = src.read(buffer, sizeof(buffer));
            dst.write(buffer, bytesRead);
          }
          dst.close();
        }
        src.close();
      }
      Serial.printf("[%lu] [XTC] Copied cover to thumb (no scaling needed)\n", millis());
      return SdMan.exists(getThumbBmpPath().c_str());
    }
    return false;
  }

  uint16_t thumbWidth = static_cast<uint16_t>(pageInfo.width * scale);
  uint16_t thumbHeight = static_cast<uint16_t>(pageInfo.height * scale);

  Serial.printf("[%lu] [XTC] Generating thumb BMP: %dx%d -> %dx%d (scale: %.3f)\n", millis(), pageInfo.width,
                pageInfo.height, thumbWidth, thumbHeight, scale);

  
  size_t bitmapSize;
  if (bitDepth == 2) {
    bitmapSize = ((static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8) * 2;
  } else {
    bitmapSize = ((pageInfo.width + 7) / 8) * pageInfo.height;
  }
  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(bitmapSize));
  if (!pageBuffer) {
    Serial.printf("[%lu] [XTC] Failed to allocate page buffer (%lu bytes)\n", millis(), bitmapSize);
    return false;
  }

  
  size_t bytesRead = const_cast<xtc::XtcParser*>(parser.get())->loadPage(0, pageBuffer, bitmapSize);
  if (bytesRead == 0) {
    Serial.printf("[%lu] [XTC] Failed to load cover page for thumb\n", millis());
    free(pageBuffer);
    return false;
  }

  
  FsFile thumbBmp;
  if (!SdMan.openFileForWrite("XTC", getThumbBmpPath(), thumbBmp)) {
    Serial.printf("[%lu] [XTC] Failed to create thumb BMP file\n", millis());
    free(pageBuffer);
    return false;
  }

  
  const uint32_t rowSize = (thumbWidth + 31) / 32 * 4;  
  const uint32_t imageSize = rowSize * thumbHeight;
  const uint32_t fileSize = 14 + 40 + 8 + imageSize;  

  
  thumbBmp.write('B');
  thumbBmp.write('M');
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&fileSize), 4);
  uint32_t reserved = 0;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&reserved), 4);
  uint32_t dataOffset = 14 + 40 + 8;  
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&dataOffset), 4);

  
  uint32_t dibHeaderSize = 40;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&dibHeaderSize), 4);
  int32_t widthVal = thumbWidth;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&widthVal), 4);
  int32_t heightVal = -static_cast<int32_t>(thumbHeight);  
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&heightVal), 4);
  uint16_t planes = 1;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&planes), 2);
  uint16_t bitsPerPixel = 1;  
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&bitsPerPixel), 2);
  uint32_t compression = 0;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&compression), 4);
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&imageSize), 4);
  int32_t ppmX = 2835;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&ppmX), 4);
  int32_t ppmY = 2835;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&ppmY), 4);
  uint32_t colorsUsed = 2;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&colorsUsed), 4);
  uint32_t colorsImportant = 2;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&colorsImportant), 4);

  
  uint8_t palette[8] = {
      0x00, 0x00, 0x00, 0x00,  
      0xFF, 0xFF, 0xFF, 0x00   
  };
  thumbBmp.write(palette, 8);

  
  uint8_t* rowBuffer = static_cast<uint8_t*>(malloc(rowSize));
  if (!rowBuffer) {
    free(pageBuffer);
    thumbBmp.close();
    return false;
  }

  
  uint32_t scaleInv_fp = static_cast<uint32_t>(65536.0f / scale);

  
  const size_t planeSize = (bitDepth == 2) ? ((static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8) : 0;
  const uint8_t* plane1 = (bitDepth == 2) ? pageBuffer : nullptr;
  const uint8_t* plane2 = (bitDepth == 2) ? pageBuffer + planeSize : nullptr;
  const size_t colBytes = (bitDepth == 2) ? ((pageInfo.height + 7) / 8) : 0;
  const size_t srcRowBytes = (bitDepth == 1) ? ((pageInfo.width + 7) / 8) : 0;

  for (uint16_t dstY = 0; dstY < thumbHeight; dstY++) {
    memset(rowBuffer, 0xFF, rowSize);  

    
    uint32_t srcYStart = (static_cast<uint32_t>(dstY) * scaleInv_fp) >> 16;
    uint32_t srcYEnd = (static_cast<uint32_t>(dstY + 1) * scaleInv_fp) >> 16;
    if (srcYStart >= pageInfo.height) srcYStart = pageInfo.height - 1;
    if (srcYEnd > pageInfo.height) srcYEnd = pageInfo.height;
    if (srcYEnd <= srcYStart) srcYEnd = srcYStart + 1;
    if (srcYEnd > pageInfo.height) srcYEnd = pageInfo.height;

    for (uint16_t dstX = 0; dstX < thumbWidth; dstX++) {
      
      uint32_t srcXStart = (static_cast<uint32_t>(dstX) * scaleInv_fp) >> 16;
      uint32_t srcXEnd = (static_cast<uint32_t>(dstX + 1) * scaleInv_fp) >> 16;
      if (srcXStart >= pageInfo.width) srcXStart = pageInfo.width - 1;
      if (srcXEnd > pageInfo.width) srcXEnd = pageInfo.width;
      if (srcXEnd <= srcXStart) srcXEnd = srcXStart + 1;
      if (srcXEnd > pageInfo.width) srcXEnd = pageInfo.width;

      
      uint32_t graySum = 0;
      uint32_t totalCount = 0;

      for (uint32_t srcY = srcYStart; srcY < srcYEnd && srcY < pageInfo.height; srcY++) {
        for (uint32_t srcX = srcXStart; srcX < srcXEnd && srcX < pageInfo.width; srcX++) {
          uint8_t grayValue = 255;  

          if (bitDepth == 2) {
            
            
            if (srcX < pageInfo.width) {
              const size_t colIndex = pageInfo.width - 1 - srcX;
              const size_t byteInCol = srcY / 8;
              const size_t bitInByte = 7 - (srcY % 8);
              const size_t byteOffset = colIndex * colBytes + byteInCol;
              
              if (byteOffset < planeSize) {
                const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
                const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
                const uint8_t pixelValue = (bit1 << 1) | bit2;
                
                
                grayValue = (3 - pixelValue) * 85;  
              }
            }
          } else {
            
            const size_t byteIdx = srcY * srcRowBytes + srcX / 8;
            const size_t bitIdx = 7 - (srcX % 8);
            
            if (byteIdx < bitmapSize) {
              const uint8_t pixelBit = (pageBuffer[byteIdx] >> bitIdx) & 1;
              
              grayValue = pixelBit ? 255 : 0;
            }
          }

          graySum += grayValue;
          totalCount++;
        }
      }

      
      uint8_t avgGray = (totalCount > 0) ? static_cast<uint8_t>(graySum / totalCount) : 255;

      
      uint32_t hash = static_cast<uint32_t>(dstX) * 374761393u + static_cast<uint32_t>(dstY) * 668265263u;
      hash = (hash ^ (hash >> 13)) * 1274126177u;
      const int threshold = static_cast<int>(hash >> 24);           
      const int adjustedThreshold = 128 + ((threshold - 128) / 2);  

      
      uint8_t oneBit = (avgGray >= adjustedThreshold) ? 1 : 0;

      
      const size_t byteIndex = dstX / 8;
      const size_t bitOffset = 7 - (dstX % 8);
      
      if (byteIndex < rowSize) {
        if (oneBit) {
          rowBuffer[byteIndex] |= (1 << bitOffset);  
        } else {
          rowBuffer[byteIndex] &= ~(1 << bitOffset);  
        }
      }
    }

    
    thumbBmp.write(rowBuffer, rowSize);
  }

  free(rowBuffer);
  thumbBmp.close();
  free(pageBuffer);

  Serial.printf("[%lu] [XTC] Generated thumb BMP (%dx%d): %s\n", millis(), thumbWidth, thumbHeight,
                getThumbBmpPath().c_str());
  return true;
}

uint32_t Xtc::getPageCount() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getPageCount();
}

uint16_t Xtc::getPageWidth() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getWidth();
}

uint16_t Xtc::getPageHeight() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getHeight();
}

uint8_t Xtc::getBitDepth() const {
  if (!loaded || !parser) {
    return 1;  
  }
  return parser->getBitDepth();
}

size_t Xtc::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) const {
  if (!loaded || !parser) {
    return 0;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPage(pageIndex, buffer, bufferSize);
}

xtc::XtcError Xtc::loadPageStreaming(uint32_t pageIndex,
                                     std::function<void(const uint8_t* data, size_t size, size_t offset)> callback,
                                     size_t chunkSize) const {
  if (!loaded || !parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPageStreaming(pageIndex, callback, chunkSize);
}

uint8_t Xtc::calculateProgress(uint32_t currentPage) const {
  if (!loaded || !parser || parser->getPageCount() == 0) {
    return 0;
  }
  return static_cast<uint8_t>((currentPage + 1) * 100 / parser->getPageCount());
}

xtc::XtcError Xtc::getLastError() const {
  if (!parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return parser->getLastError();
}
