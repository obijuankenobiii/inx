#ifndef PngToBmpConverter_h
#define PngToBmpConverter_h

#include <Arduino.h>
#include <SdFat.h>

class PngToBmpConverter {
public:
    // Convert PNG to 1-bit BMP
    static bool pngFileTo1BitBmpStream(FsFile& pngFile, Print& bmpOut);
    static bool pngFileTo1BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut,
                                               int targetMaxWidth, int targetMaxHeight,
                                               bool cropToFill = true);
    static bool pngFileTo1BitBmpStreamCentered(FsFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight,
                                               bool cropToFill = true);
    /** 2bpp BMP (four ink levels) with Floyd–Steinberg; matches JPEG thumbnail look for library/recent covers. */
    static bool pngFileTo2BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                              bool cropToFill = true);
    
    // Thumbnail functions
    static bool pngFileToThumbnailBmp(FsFile& pngFile, Print& bmpOut,
                                      int targetMaxWidth = 0, int targetMaxHeight = 0);
    static bool pngFileTo1BitThumbnailBmp(FsFile& pngFile, Print& bmpOut,
                                          int targetMaxWidth = 0, int targetMaxHeight = 0);
    
    // Debug function to print PNG header info
    static void printPngInfo(uint8_t* pngBuffer, size_t fileSize);
};

#endif