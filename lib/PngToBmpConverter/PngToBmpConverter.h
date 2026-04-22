/**
 * @file PngToBmpConverter.h
 * @brief Public interface and types for PngToBmpConverter.
 */

#ifndef PngToBmpConverter_h
#define PngToBmpConverter_h

#include <Arduino.h>
#include <SdFat.h>

class PngToBmpConverter {
public:
    
    static bool pngFileTo1BitBmpStream(FsFile& pngFile, Print& bmpOut);
    static bool pngFileTo1BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut,
                                               int targetMaxWidth, int targetMaxHeight,
                                               bool cropToFill = true);
    static bool pngFileTo1BitBmpStreamCentered(FsFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight,
                                               bool cropToFill = true);
    /** 2bpp BMP (four ink levels) with Floyd–Steinberg; matches JPEG thumbnail look for library/recent covers. */
    static bool pngFileTo2BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                              bool cropToFill = true);

    /** EPUB body images: 2-bit BMP matching web reader (contain 500×820, FS dither, 42/127/212). */
    static bool pngFileToEpubWebStyle2BitBmpStream(FsFile& pngFile, Print& bmpOut);
    
    
    static bool pngFileToThumbnailBmp(FsFile& pngFile, Print& bmpOut,
                                      int targetMaxWidth = 0, int targetMaxHeight = 0);
    static bool pngFileTo1BitThumbnailBmp(FsFile& pngFile, Print& bmpOut,
                                          int targetMaxWidth = 0, int targetMaxHeight = 0);
    
    
    static void printPngInfo(uint8_t* pngBuffer, size_t fileSize);
};

#endif