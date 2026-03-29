#pragma once

class FsFile;
class Print;
class ZipFile;

class JpegToBmpConverter {
  static unsigned char jpegReadCallback(unsigned char* pBuf, unsigned char buf_size,
                                        unsigned char* pBytes_actually_read, void* pCallback_data);
  static bool jpegFileToBmpStreamInternal(class FsFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                          bool oneBit, bool quickMode = false);
  // NEW: Internal function for centered full-screen fill
  static bool jpegFileToBmpStreamInternalCentered(class FsFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                                  bool oneBit, bool quickMode = false);

 public:
  static bool jpegFileToBmpStream(FsFile& jpegFile, Print& bmpOut);
  // Convert with custom target size (for thumbnails)
  static bool jpegFileToBmpStreamWithSize(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // Convert to 1-bit BMP (black and white only, no grays)
  static bool jpegFileTo1BitBmpStream(FsFile& jpegFile, Print& bmpOut);
  // Convert to 1-bit BMP with custom target size (for thumbnails)
  static bool jpegFileTo1BitBmpStreamWithSize(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // Quick preview mode: simple threshold instead of dithering (faster but lower quality)
  static bool jpegFileToBmpStreamQuick(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  
  // NEW: Centered full-screen fill functions (crops and centers to fit exactly)
  static bool jpegFileTo1BitBmpStreamCentered(FsFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight);
  static bool jpegFileToBmpStreamCentered(FsFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight);

  /**
   * Convert JPEG to 2-bit BMP with clean quantization (no dithering)
   * Ideal for thumbnails where artifacts are problematic
   */
  bool jpegFileToThumbnailBmp(FsFile& jpegFile, Print& bmpOut, 
                              int targetMaxWidth = 120, int targetMaxHeight = 160);

  /**
   * Convert JPEG to 1-bit BMP with clean thresholding (no dithering)
   * Ultra-clean for small thumbnails
   */
  bool jpegFileTo1BitThumbnailBmp(FsFile& jpegFile, Print& bmpOut, 
                                  int targetMaxWidth = 80, int targetMaxHeight = 120);

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
    bool jpegFileToTopCropBmp(
        FsFile& jpegFile, 
        Print& bmpOut, 
        int targetMaxWidth, 
        int targetMaxHeight,
        float verticalCropPercent = 0.5f
    );
};