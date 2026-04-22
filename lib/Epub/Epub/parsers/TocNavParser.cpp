/**
 * @file TocNavParser.cpp
 * @brief Definitions for TocNavParser.
 */

#include "TocNavParser.h"

#include <FsHelpers.h>
#include <HardwareSerial.h>

#include "../BookMetadataCache.h"

bool TocNavParser::setup() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    Serial.printf("[%lu] [NAV] Couldn't allocate memory for parser\n", millis());
    return false;
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  return true;
}

TocNavParser::~TocNavParser() {
  if (parser) {
    XML_StopParser(parser, XML_FALSE);
    XML_SetElementHandler(parser, nullptr, nullptr);
    XML_SetCharacterDataHandler(parser, nullptr);
    XML_ParserFree(parser);
    parser = nullptr;
  }
}

size_t TocNavParser::write(const uint8_t data) { return write(&data, 1); }

size_t TocNavParser::write(const uint8_t* buffer, const size_t size) {
  if (!parser) return 0;

  const uint8_t* currentBufferPos = buffer;
  auto remainingInBuffer = size;

  while (remainingInBuffer > 0) {
    void* const buf = XML_GetBuffer(parser, 1024);
    if (!buf) {
      Serial.printf("[%lu] [NAV] Couldn't allocate memory for buffer\n", millis());
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
      Serial.printf("[%lu] [NAV] Parse error at line %lu: %s\n", millis(), XML_GetCurrentLineNumber(parser),
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

void XMLCALL TocNavParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<TocNavParser*>(userData);

  
  if (strcmp(name, "html") == 0) {
    self->state = IN_HTML;
    return;
  }

  if (self->state == IN_HTML && strcmp(name, "body") == 0) {
    self->state = IN_BODY;
    return;
  }

  
  if (self->state >= IN_BODY && strcmp(name, "nav") == 0) {
    bool isTocNav = false;
    for (int i = 0; atts[i]; i += 2) {
      const char* k = atts[i];
      const char* v = atts[i + 1];
      if ((strcmp(k, "epub:type") == 0 || strcmp(k, "type") == 0) && strcmp(v, "toc") == 0) {
        isTocNav = true;
        break;
      }
      if (strcmp(k, "role") == 0 && strcmp(v, "doc-toc") == 0) {
        isTocNav = true;
        break;
      }
    }
    if (isTocNav) {
      self->state = IN_NAV_TOC;
      Serial.printf("[%lu] [NAV] Found nav toc element\n", millis());
      return;
    }
    return;
  }

  
  if (self->state < IN_NAV_TOC) {
    return;
  }

  if (strcmp(name, "ol") == 0) {
    self->olDepth++;
    self->state = IN_OL;
    return;
  }

  if (self->state == IN_OL && strcmp(name, "li") == 0) {
    self->state = IN_LI;
    self->currentLabel.clear();
    self->currentHref.clear();
    return;
  }

  if (self->state == IN_LI && strcmp(name, "a") == 0) {
    self->state = IN_ANCHOR;
    
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "href") == 0) {
        self->currentHref = atts[i + 1];
        break;
      }
    }
    return;
  }
}

void XMLCALL TocNavParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<TocNavParser*>(userData);

  
  if (self->state == IN_ANCHOR) {
    self->currentLabel.append(s, len);
  }
}

void XMLCALL TocNavParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<TocNavParser*>(userData);

  if (strcmp(name, "a") == 0 && self->state == IN_ANCHOR) {
    
    if (!self->currentLabel.empty() && !self->currentHref.empty()) {
      std::string href = FsHelpers::normalisePath(self->baseContentPath + self->currentHref);
      std::string anchor;

      const size_t pos = href.find('#');
      if (pos != std::string::npos) {
        anchor = href.substr(pos + 1);
        href = href.substr(0, pos);
      }

      if (self->cache) {
        
        self->cache->createTocEntry(self->currentLabel, href, anchor, self->olDepth);
      }

      self->currentLabel.clear();
      self->currentHref.clear();
    }
    self->state = IN_LI;
    return;
  }

  if (strcmp(name, "li") == 0 && (self->state == IN_LI || self->state == IN_OL)) {
    self->state = IN_OL;
    return;
  }

  if (strcmp(name, "ol") == 0 && self->state >= IN_NAV_TOC) {
    self->olDepth--;
    if (self->olDepth == 0) {
      self->state = IN_NAV_TOC;
    } else {
      self->state = IN_LI;  
    }
    return;
  }

  if (strcmp(name, "nav") == 0 && self->state >= IN_NAV_TOC) {
    self->state = IN_BODY;
    Serial.printf("[%lu] [NAV] Finished parsing nav toc\n", millis());
    return;
  }
}
