/**
 * @file TocNcxParser.cpp
 * @brief Definitions for TocNcxParser.
 */

#include "TocNcxParser.h"

#include <FsHelpers.h>
#include <HardwareSerial.h>

#include "../BookMetadataCache.h"

bool TocNcxParser::setup() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    Serial.printf("[%lu] [TOC] Couldn't allocate memory for parser\n", millis());
    return false;
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  return true;
}

TocNcxParser::~TocNcxParser() {
  if (parser) {
    XML_StopParser(parser, XML_FALSE);                
    XML_SetElementHandler(parser, nullptr, nullptr);  
    XML_SetCharacterDataHandler(parser, nullptr);
    XML_ParserFree(parser);
    parser = nullptr;
  }
}

size_t TocNcxParser::write(const uint8_t data) { return write(&data, 1); }

size_t TocNcxParser::write(const uint8_t* buffer, const size_t size) {
  if (!parser) return 0;

  const uint8_t* currentBufferPos = buffer;
  auto remainingInBuffer = size;

  while (remainingInBuffer > 0) {
    void* const buf = XML_GetBuffer(parser, 1024);
    if (!buf) {
      Serial.printf("[%lu] [TOC] Couldn't allocate memory for buffer\n", millis());
      XML_StopParser(parser, XML_FALSE);                
      XML_SetElementHandler(parser, nullptr, nullptr);  
      XML_SetCharacterDataHandler(parser, nullptr);
      XML_ParserFree(parser);
      parser = nullptr;
      return 0;
    }

    const auto toRead = remainingInBuffer < 1024 ? remainingInBuffer : 1024;
    memcpy(buf, currentBufferPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), remainingSize == toRead) == XML_STATUS_ERROR) {
      Serial.printf("[%lu] [TOC] Parse error at line %lu: %s\n", millis(), XML_GetCurrentLineNumber(parser),
                    XML_ErrorString(XML_GetErrorCode(parser)));
      XML_StopParser(parser, XML_FALSE);                
      XML_SetElementHandler(parser, nullptr, nullptr);  
      XML_SetCharacterDataHandler(parser, nullptr);
      XML_ParserFree(parser);
      parser = nullptr;
      return 0;
    }

    currentBufferPos += toRead;
    remainingInBuffer -= toRead;
    remainingSize -= toRead;
  }
  return size;
}

void XMLCALL TocNcxParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  
  
  
  
  
  
  
  
  
  
  
  
  

  auto* self = static_cast<TocNcxParser*>(userData);

  if (self->state == START && strcmp(name, "ncx") == 0) {
    self->state = IN_NCX;
    return;
  }

  if (self->state == IN_NCX && strcmp(name, "navMap") == 0) {
    self->state = IN_NAV_MAP;
    return;
  }

  
  if ((self->state == IN_NAV_MAP || self->state == IN_NAV_POINT) && strcmp(name, "navPoint") == 0) {
    self->state = IN_NAV_POINT;
    self->currentDepth++;

    self->currentLabel.clear();
    self->currentSrc.clear();
    return;
  }

  if (self->state == IN_NAV_POINT && strcmp(name, "navLabel") == 0) {
    self->state = IN_NAV_LABEL;
    return;
  }

  if (self->state == IN_NAV_LABEL && strcmp(name, "text") == 0) {
    self->state = IN_NAV_LABEL_TEXT;
    return;
  }

  if (self->state == IN_NAV_POINT && strcmp(name, "content") == 0) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "src") == 0) {
        self->currentSrc = atts[i + 1];
        break;
      }
    }
    return;
  }
}

void XMLCALL TocNcxParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<TocNcxParser*>(userData);
  if (self->state == IN_NAV_LABEL_TEXT) {
    self->currentLabel.append(s, len);
  }
}

void XMLCALL TocNcxParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<TocNcxParser*>(userData);

  if (self->state == IN_NAV_LABEL_TEXT && strcmp(name, "text") == 0) {
    self->state = IN_NAV_LABEL;
    return;
  }

  if (self->state == IN_NAV_LABEL && strcmp(name, "navLabel") == 0) {
    self->state = IN_NAV_POINT;
    return;
  }

  if (self->state == IN_NAV_POINT && strcmp(name, "navPoint") == 0) {
    self->currentDepth--;
    if (self->currentDepth == 0) {
      self->state = IN_NAV_MAP;
    }
    return;
  }

  if (self->state == IN_NAV_POINT && strcmp(name, "content") == 0) {
    
    
    
    if (!self->currentLabel.empty() && !self->currentSrc.empty()) {
      std::string href = FsHelpers::normalisePath(self->baseContentPath + self->currentSrc);
      std::string anchor;

      const size_t pos = href.find('#');
      if (pos != std::string::npos) {
        anchor = href.substr(pos + 1);
        href = href.substr(0, pos);
      }

      if (self->cache) {
        self->cache->createTocEntry(self->currentLabel, href, anchor, self->currentDepth);
      }

      
      self->currentLabel.clear();
      self->currentSrc.clear();
    }
  }
}
