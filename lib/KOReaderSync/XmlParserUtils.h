#pragma once

/**
 * @file XmlParserUtils.h
 * @brief Public interface and types for XmlParserUtils.
 */

#include <expat.h>


inline void destroyXmlParser(XML_Parser& parser) {
  if (!parser) return;
  XML_StopParser(parser, XML_FALSE);
  XML_SetElementHandler(parser, nullptr, nullptr);
  XML_SetCharacterDataHandler(parser, nullptr);
  XML_ParserFree(parser);
  parser = nullptr;
}
