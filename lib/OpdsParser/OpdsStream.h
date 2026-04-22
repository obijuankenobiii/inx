#pragma once

/**
 * @file OpdsStream.h
 * @brief Public interface and types for OpdsStream.
 */

#include <Stream.h>

#include "OpdsParser.h"

class OpdsParserStream : public Stream {
 public:
  explicit OpdsParserStream(OpdsParser& parser);

  
  int available() override;
  int peek() override;
  int read() override;

  virtual size_t write(uint8_t c) override;
  virtual size_t write(const uint8_t* buffer, size_t size) override;

  ~OpdsParserStream() override;

 private:
  OpdsParser& parser;
};
