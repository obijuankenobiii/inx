#pragma once

/**
 * @file TocNavParser.h
 * @brief Public interface and types for TocNavParser.
 */

#include <Print.h>
#include <expat.h>

#include <string>

class BookMetadataCache;



class TocNavParser final : public Print {
  enum ParserState {
    START,
    IN_HTML,
    IN_BODY,
    IN_NAV_TOC,  
    IN_OL,       
    IN_LI,       
    IN_ANCHOR,   
  };

  const std::string& baseContentPath;
  size_t remainingSize;
  XML_Parser parser = nullptr;
  ParserState state = START;
  BookMetadataCache* cache;

  
  uint8_t olDepth = 0;
  
  std::string currentLabel;
  std::string currentHref;

  static void startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void characterData(void* userData, const XML_Char* s, int len);
  static void endElement(void* userData, const XML_Char* name);

 public:
  explicit TocNavParser(const std::string& baseContentPath, const size_t xmlSize, BookMetadataCache* cache)
      : baseContentPath(baseContentPath), remainingSize(xmlSize), cache(cache) {}
  ~TocNavParser() override;

  bool setup();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
