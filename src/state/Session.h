#pragma once

/**
 * @file Session.h
 * @brief Public interface and types for Session.
 */

#include <iosfwd>
#include <string>

class Session {
  
  static Session instance;

 public:
  std::string lastRead;
  uint8_t lastSleepImage;
  ~Session() = default;

  
  static Session& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();
};


#define APP_STATE Session::getInstance()
