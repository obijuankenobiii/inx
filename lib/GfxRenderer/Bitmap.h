#pragma once

/**
 * @file Bitmap.h
 * @brief Public interface and types for Bitmap.
 */

#include <SdFat.h>

#include <cstdint>

#include "BitmapUtil.h"

#pragma pack(push, 1)
struct BmpHeader {
  struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
  } fileHeader;
  struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
  } infoHeader;
  struct RgbQuad {
    uint8_t rgbBlue;
    uint8_t rgbGreen;
    uint8_t rgbRed;
    uint8_t rgbReserved;
  };
  RgbQuad colors[2];
};
#pragma pack(pop)

/** Error diffusion when converting high-color BMP rows to 2bpp (ignored for native 1/2bpp). */
enum class BitmapDitherMode : uint8_t { None = 0, FloydSteinberg = 1, Atkinson = 2 };

/** Maps a stored image-dither value (0..2, same order as `BitmapDitherMode`); invalid values → None. */
inline BitmapDitherMode bitmapDitherModeFromSetting(uint8_t v) {
  if (v > static_cast<uint8_t>(BitmapDitherMode::Atkinson)) {
    return BitmapDitherMode::None;
  }
  return static_cast<BitmapDitherMode>(v);
}

enum class BmpReaderError : uint8_t {
  Ok = 0,
  FileInvalid,
  SeekStartFailed,

  NotBMP,
  DIBTooSmall,

  BadPlanes,
  UnsupportedBpp,
  UnsupportedCompression,

  BadDimensions,
  ImageTooLarge,
  PaletteTooLarge,

  SeekPixelDataFailed,
  BufferTooSmall,
  OomRowBuffer,
  ShortReadRow,
};

class Bitmap {
 public:
  static const char* errorToString(BmpReaderError err);

  explicit Bitmap(FsFile& file, BitmapDitherMode ditherMode = BitmapDitherMode::None) : file(file), ditherMode(ditherMode) {}
  ~Bitmap();
  BmpReaderError parseHeaders();
  BmpReaderError readNextRow(uint8_t* data, uint8_t* rowBuffer) const;
  BmpReaderError rewindToData() const;
  int getWidth() const { return width; }
  int getHeight() const { return height; }
  bool isTopDown() const { return topDown; }
  bool hasGreyscale() const { return bpp > 1; }
  int getRowBytes() const { return rowBytes; }
  bool is1Bit() const { return bpp == 1; }
  uint16_t getBpp() const { return bpp; }

 private:
  static uint16_t readLE16(FsFile& f);
  static uint32_t readLE32(FsFile& f);

  FsFile& file;
  BitmapDitherMode ditherMode = BitmapDitherMode::None;
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t bfOffBits = 0;
  uint16_t bpp = 0;
  uint32_t colorsUsed = 0;
  bool nativePalette = false;  
  int rowBytes = 0;
  uint8_t paletteLum[256] = {};

  
  mutable int16_t* errorCurRow = nullptr;
  mutable int16_t* errorNextRow = nullptr;
  mutable int prevRowY = -1;  

  mutable AtkinsonDitherer* atkinsonDitherer = nullptr;
  mutable FloydSteinbergDitherer* fsDitherer = nullptr;
};