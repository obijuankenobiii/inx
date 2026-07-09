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
  uint32_t lastSleepImage;
  uint32_t sleepImageShuffleSeed;
  ~Session() = default;

  static Session& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();
};

#define APP_STATE Session::getInstance()
