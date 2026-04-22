/**
 * @file XtcTypes.h
 * @brief Public interface and types for XtcTypes.
 */

/**
 * XtcTypes.h
 *
 * XTC file format type definitions
 * XTC ebook support for Inx Reader
 *
 * XTC is the native binary ebook format for XTeink X4 e-reader.
 * It stores pre-rendered bitmap images per page.
 *
 * Format based on EPUB2XTC converter by Rafal-P-Mazur
 */

#pragma once

#include <cstdint>
#include <string>

namespace xtc {



constexpr uint32_t XTC_MAGIC = 0x00435458;  

constexpr uint32_t XTCH_MAGIC = 0x48435458;  

constexpr uint32_t XTG_MAGIC = 0x00475458;  

constexpr uint32_t XTH_MAGIC = 0x00485458;  


constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint16_t DISPLAY_HEIGHT = 800;


#pragma pack(push, 1)
struct XtcHeader {
  uint32_t magic;            
  uint8_t versionMajor;      
  uint8_t versionMinor;      
  uint16_t pageCount;        
  uint8_t readDirection;     
  uint8_t hasMetadata;       
  uint8_t hasThumbnails;     
  uint8_t hasChapters;       
  uint32_t currentPage;      
  uint64_t metadataOffset;   
  uint64_t pageTableOffset;  
  uint64_t dataOffset;       
  uint64_t thumbOffset;      
  uint32_t chapterOffset;    
  uint32_t padding;          
};
#pragma pack(pop)


#pragma pack(push, 1)
struct PageTableEntry {
  uint64_t dataOffset;  
  uint32_t dataSize;    
  uint16_t width;       
  uint16_t height;      
};
#pragma pack(pop)



#pragma pack(push, 1)
struct XtgPageHeader {
  uint32_t magic;       
  uint16_t width;       
  uint16_t height;      
  uint8_t colorMode;    
  uint8_t compression;  
  uint32_t dataSize;    
  uint64_t md5;         
  
  
  
  
  
  
  
  
  
  
};
#pragma pack(pop)


struct PageInfo {
  uint32_t offset;   
  uint32_t size;     
  uint16_t width;    
  uint16_t height;   
  uint8_t bitDepth;  
  uint8_t padding;   
};  

struct ChapterInfo {
  std::string name;
  uint16_t startPage;
  uint16_t endPage;
};


enum class XtcError {
  OK = 0,
  FILE_NOT_FOUND,
  INVALID_MAGIC,
  INVALID_VERSION,
  CORRUPTED_HEADER,
  PAGE_OUT_OF_RANGE,
  READ_ERROR,
  WRITE_ERROR,
  MEMORY_ERROR,
  DECOMPRESSION_ERROR,
};


inline const char* errorToString(XtcError err) {
  switch (err) {
    case XtcError::OK:
      return "OK";
    case XtcError::FILE_NOT_FOUND:
      return "File not found";
    case XtcError::INVALID_MAGIC:
      return "Invalid magic number";
    case XtcError::INVALID_VERSION:
      return "Unsupported version";
    case XtcError::CORRUPTED_HEADER:
      return "Corrupted header";
    case XtcError::PAGE_OUT_OF_RANGE:
      return "Page out of range";
    case XtcError::READ_ERROR:
      return "Read error";
    case XtcError::WRITE_ERROR:
      return "Write error";
    case XtcError::MEMORY_ERROR:
      return "Memory allocation error";
    case XtcError::DECOMPRESSION_ERROR:
      return "Decompression error";
    default:
      return "Unknown error";
  }
}

/**
 * Check if filename has XTC/XTCH extension
 */
inline bool isXtcExtension(const char* filename) {
  if (!filename) return false;
  const char* ext = strrchr(filename, '.');
  if (!ext) return false;
  return (strcasecmp(ext, ".xtc") == 0 || strcasecmp(ext, ".xtch") == 0);
}

}  
