/**
 * @file Utf8.cpp
 * @brief Definitions for Utf8.
 */

#include "Utf8.h"

int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;          
  if ((c >> 5) == 0x6) return 2;   
  if ((c >> 4) == 0xE) return 3;   
  if ((c >> 3) == 0x1E) return 4;  
  return 1;                        
}

uint32_t utf8NextCodepoint(const unsigned char** string) {
  if (**string == 0) {
    return 0;
  }

  const int bytes = utf8CodepointLen(**string);
  const uint8_t* chr = *string;
  *string += bytes;

  if (bytes == 1) {
    return chr[0];
  }

  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);  

  for (int i = 1; i < bytes; i++) {
    cp = (cp << 6) | (chr[i] & 0x3F);
  }

  return cp;
}

size_t utf8RemoveLastChar(std::string& str) {
  if (str.empty()) return 0;
  size_t pos = str.size() - 1;
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  str.resize(pos);
  return pos;
}


void utf8TruncateChars(std::string& str, const size_t numChars) {
  for (size_t i = 0; i < numChars && !str.empty(); ++i) {
    utf8RemoveLastChar(str);
  }
}
