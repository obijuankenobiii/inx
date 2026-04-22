#pragma once

/**
 * @file JpegToBmpConverter.h
 * @brief Public interface and types for JpegToBmpConverter.
 */

class FsFile;
class Print;
class ZipFile;

class JpegToBmpConverter {
  static unsigned char jpegReadCallback(unsigned char* pBuf, unsigned char buf_size,
                                        unsigned char* pBytes_actually_read, void* pCallback_data);
  static bool jpegFileToBmpStreamInternal(class FsFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                          bool oneBit, bool quickMode = false);
  
  static bool jpegFileToBmpStreamInternalCentered(class FsFile& jpegFile, Print& bmpOut, int targetWidth,
                                                  int targetHeight, bool oneBit, bool quickMode = false,
                                                  bool cropToFill = true);

 public:
  static bool jpegFileToBmpStream(FsFile& jpegFile, Print& bmpOut);
  
  static bool jpegFileToBmpStreamWithSize(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  
  static bool jpegFileTo1BitBmpStream(FsFile& jpegFile, Print& bmpOut);
  
  static bool jpegFileTo1BitBmpStreamWithSize(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  
  static bool jpegFileToBmpStreamQuick(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);

  
  static bool jpegFileTo1BitBmpStreamCentered(FsFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                            bool cropToFill = true);
  static bool jpegFileToBmpStreamCentered(FsFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                          bool cropToFill = true);

  /** EPUB body images: 2-bit BMP matching web reader (contain 500×820, FS dither, 42/127/212). */
  static bool jpegFileToEpubWebStyle2BitBmpStream(FsFile& jpegFile, Print& bmpOut);

  /**
   * Resize a 1-bit BMP file to new dimensions
   * @param bmpFile Source BMP file (must be 1-bit)
   * @param bmpOut Destination stream for resized BMP
   * @param targetWidth Target width in pixels
   * @param targetHeight Target height in pixels
   * @return true if successful
   */
  static bool resizeBitmap(FsFile& bmpFile, Print& bmpOut, int targetWidth, int targetHeight);

  /**
   * Convert JPEG to 2-bit BMP with clean quantization (no dithering)
   * Ideal for thumbnails where artifacts are problematic
   */
  bool jpegFileToThumbnailBmp(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth = 120, int targetMaxHeight = 160);

  /**
   * Convert JPEG to 1-bit BMP with clean thresholding (no dithering)
   * Ultra-clean for small thumbnails
   */
  bool jpegFileTo1BitThumbnailBmp(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth = 80, int targetMaxHeight = 120);

  /**
   * Decodes a JPEG but only processes the TOP portion of the source image,
   * scaling it to fill the targetMaxWidth/Height.
   * * @param jpegFile Source file handle
   * @param bmpOut Destination stream (file or buffer)
   * @param targetMaxWidth Width of the output BMP
   * @param targetMaxHeight Height of the output BMP
   * @param verticalCropPercent 0.5f for top half, 0.75f for top 3/4, etc.
   * @return true if successful
   */
  bool jpegFileToTopCropBmp(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                            float verticalCropPercent = 0.5f);
};